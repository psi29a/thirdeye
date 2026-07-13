// Automap overlay -- see automap.hpp for the shape.
//
// Sources of truth we read at tick():
//   kernel   (class 1382): B:x@243, B:y@244, B:fdir@245, B:lvlnum@246
//   dungeon  (class 1381): B:lvlmap@0..1023 (32x32 wall bytes -- 0xFF = floor,
//                          anything else blocks passage), W:lvlobj@1024..7167
//                          (three planes of 32x32 int16 heads, plane*2048 + y*64 + x*2)
//
// FoV: the SOP already computes the party's field of view every frame and
// stores the resulting 18 view cells in the dungeon class -- x[7172..7189],
// y[7190..7207], visible[7208..7225]. We read that array verbatim, which
// gives us exactly what the player sees in the 3D view: hackable trees /
// hidden doors block vision correctly because the SOP already accounts for
// blocking objects on lvlobj plane 0, not just wall bytes in lvlmap.
// Party's own cell + immediate walls are also visible from the 3D view, so
// we still mark the party cell + its 8 neighbours for "what the party
// stands next to" (adjacent walls read as known).

#include "automap.hpp"

#include "graphics/graphics.hpp"
#include "resources/res.hpp"
#include "vm/objects.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace THIRDEYE::automap {

namespace {

constexpr int kGridW = 32;
constexpr int kGridH = 32;
constexpr int kCellBits = kGridW * kGridH;              // 1024
constexpr int kCellBytes = kCellBits / 8;               // 128
constexpr int kLevels = 15;                             // 0 unused + 14 real
constexpr uint16_t kKernelCls = 1382;
constexpr uint16_t kDungeonCls = 1381;

// The automap paints in raw RGB (via Graphics::fillRectRGB), bypassing the
// palette so colours stay stable across dungeon-specific palette swaps. The
// text/borders that go through drawText/drawLine still use palette indices
// -- those regions are pure white/frame accents that render consistently.
struct RGB { uint8_t r, g, b; };
constexpr RGB kColBg        = {   0,   0,   0 }; // fog
constexpr RGB kColFloor     = { 235, 205, 145 }; // parchment cream
constexpr RGB kColWall      = { 128, 128, 128 }; // grey
constexpr RGB kColParty     = { 220,  40,  40 }; // red arrow on cream backing
// Stairs draw as a tiny two-tone staircase glyph (see drawStairs), not a
// flat dot -- the greys read as "structure" against both floor and wall.
constexpr RGB kColStairLight = { 200, 200, 200 };
constexpr RGB kColStairDark  = {  70,  70,  70 };
constexpr RGB kColItem      = { 175,  80, 200 }; // purple
constexpr RGB kColActivator = {  70, 130, 230 }; // blue
constexpr RGB kColTrap      = {  70, 200,  90 }; // green
constexpr RGB kColNotable   = { 210,  90,  60 }; // red-orange (generic feature)
constexpr uint8_t kColLegend    = 0x0f; // palette index -- text uses drawText
constexpr uint8_t kColBorderIdx = 0x90; // palette index -- for drawLine borders

// Grid geometry on the 320x200 screen. Cell 5px -> 160x160 grid, left-aligned
// with a small margin; legend column on the right.
constexpr int kCell = 5;
constexpr int kOx = 8;    // grid left
constexpr int kOy = 20;   // grid top (title bar above)
constexpr int kLegendX = 8 + kGridW * kCell + 12;

struct State {
	bool open = false;
	// Bit b of visited[level * kCellBytes + y*4 + x/8] with b = x&7.
	std::array<uint8_t, kLevels * kCellBytes> visited{};
	// Per-category sticky flags -- set on first sight, never cleared, so a
	// cell keeps its breadcrumb after the thing goes away (chest looted,
	// tree chopped, lever pulled).
	std::array<uint8_t, kLevels * kCellBytes> stairs{};
	std::array<uint8_t, kLevels * kCellBytes> items{};
	std::array<uint8_t, kLevels * kCellBytes> activators{};
	std::array<uint8_t, kLevels * kCellBytes> traps{};
	std::array<uint8_t, kLevels * kCellBytes> notable{}; // generic feature
	// Cached party pose (from last tick with dungeon+kernel live).
	int partyX = -1, partyY = -1, partyF = 0, level = 0;
	// Cached current-level lvlmap (32x32 bytes), refreshed each tick.
	std::array<uint8_t, kCellBits> lvlmap{};
	// Per-cell "renders as a wall" cache for the current level, rebuilt each
	// tick. A cell is a wall if its lvlmap byte blocks OR (lvlmap says floor
	// but a plane-0 blocker sits on it -- hackable tree / hidden door). This
	// updates live: chop the tree / open the door and the cell flips to floor
	// on the next tick.
	std::array<uint8_t, kCellBits / 8> wallBits{};
	bool havePose = false;
};
State g;

inline int cellIdx(int x, int y) { return y * kGridW + x; }

inline void bitSet(std::array<uint8_t, kLevels * kCellBytes> &b, int lvl, int x, int y) {
	if (lvl < 0 || lvl >= kLevels || x < 0 || x >= kGridW || y < 0 || y >= kGridH) return;
	int i = cellIdx(x, y);
	b[lvl * kCellBytes + (i >> 3)] |= static_cast<uint8_t>(1u << (i & 7));
}

inline bool bitGet(const std::array<uint8_t, kLevels * kCellBytes> &b, int lvl, int x, int y) {
	if (lvl < 0 || lvl >= kLevels || x < 0 || x >= kGridW || y < 0 || y >= kGridH) return false;
	int i = cellIdx(x, y);
	return (b[lvl * kCellBytes + (i >> 3)] >> (i & 7)) & 1;
}

// Non-floor lvlmap byte -> renders + blocks vision on the map.
inline bool isMapWall(uint8_t b) { return b != 0xFF; }

// Party facing 0=N, 1=E, 2=S, 3=W (EOB DIR_N..DIR_W). Forward + right-strafe
// unit vectors used to lay out the FoV cone in party-relative coordinates.
struct Facing { int fx, fy, rx, ry; };
Facing facingVecs(int fdir) {
	switch (fdir & 3) {
	case 0: return { 0, -1,  1,  0 };
	case 1: return { 1,  0,  0,  1 };
	case 2: return { 0,  1, -1,  0 };
	default:return {-1,  0,  0, -1 };
	}
}

// Read the dungeon's lvlobj head at (x, y, plane). Returns -1 if empty.
int lvlobjHead(VM::ObjectSystem &objects, int dn, int plane, int x, int y) {
	uint32_t off = 1024u + static_cast<uint32_t>(plane) * 2048u
	             + static_cast<uint32_t>(y) * 64u + static_cast<uint32_t>(x) * 2u;
	uint8_t *p = objects.classStaticPtr(dn, kDungeonCls, off, 2);
	if (!p) return -1;
	return static_cast<int16_t>(p[0] | (p[1] << 8));
}

// Feature classes that render as a wall on the map (block movement/vision).
// Trees + hackable trees + all doors (their subclasses cover per-dungeon door
// variants: skull door, gold door, wooden door, ...). Walkable features
// (tombstones, levers, buttons, pressure plates, stairs/teleporters) also
// live on plane 0 but the party can walk over/past them, so they're NOT
// blockers -- only shown as coloured dots by category.
constexpr uint16_t kBlockerBases[] = { 2057, 2060, 2205 };

// Category detection for the plane-0 head. Base class ids from EYE.RES:
//   teleporters (2286) -- stairs & teleporters, "STAIRS" category
//   pressure plates (2232), levers (2241), buttons (2265) -- "ACTIVE"
//   everything else on plane 0 -- generic feature (grave etc.), "MARK"
constexpr uint16_t kStairsBase = 2286;
constexpr uint16_t kActivatorBases[] = { 2232, 2241, 2265 };
// Traps in EYE.RES don't share a single base class -- each variety inherits
// direct from features/solid-wall. Whitelist the known ones. Add more class
// ids here if a new dungeon adds trap types.
constexpr uint16_t kTrapClasses[] = {
	2039, // marble floor spike trap
	2067, // temple dart hole
	2072, // mausoleum ceiling pit
	2075, // mausoleum illusionary pit
	2078, // mausoleum wall spikes
	2081, // marble wall spike holes
	2289, // mausoleum floor pit
	2328, // invisible pit
};

bool plane0BlocksAt(VM::ObjectSystem &objects, int dn, int x, int y) {
	int head = lvlobjHead(objects, dn, 0, x, y);
	if (head < 0) return false;
	uint16_t cls = objects.classOf(head);
	for (uint16_t base : kBlockerBases)
		if (objects.isSubclassOf(cls, base)) return true;
	return false;
}

enum class Feature { None, Stairs, Trap, Activator, Generic };

Feature plane0FeatureAt(VM::ObjectSystem &objects, int dn, int x, int y) {
	int head = lvlobjHead(objects, dn, 0, x, y);
	if (head < 0) return Feature::None;
	uint16_t cls = objects.classOf(head);
	// Blockers (trees, doors) render as walls -- caller draws them without
	// a category dot on top. Traps checked BEFORE blockers so a "solid wall
	// spike trap" (subclass of solid wall) still reads as a TRAP.
	for (uint16_t t : kTrapClasses)
		if (objects.isSubclassOf(cls, t)) return Feature::Trap;
	for (uint16_t base : kBlockerBases)
		if (objects.isSubclassOf(cls, base)) return Feature::None;
	if (objects.isSubclassOf(cls, kStairsBase)) return Feature::Stairs;
	for (uint16_t base : kActivatorBases)
		if (objects.isSubclassOf(cls, base)) return Feature::Activator;
	return Feature::Generic;
}

// Fill a rectangle in raw RGB (palette-independent).
inline void rect(GRAPHICS::Graphics &gfx, int x, int y, int w, int h, RGB c) {
	gfx.fillRectRGB(x, y, x + w - 1, y + h - 1, c.r, c.g, c.b);
}

// Staircase glyph in the cell's 3x3 inner area (px/py = inner top-left):
// descending steps, alternating light/dark grey per tread so it reads as
// stairs rather than a flat dot.
//   . . L
//   . L D
//   L D L
void drawStairs(GRAPHICS::Graphics &gfx, int px, int py) {
	rect(gfx, px + 2, py,     1, 1, kColStairLight);
	rect(gfx, px + 1, py + 1, 1, 1, kColStairLight);
	rect(gfx, px + 2, py + 1, 1, 1, kColStairDark);
	rect(gfx, px,     py + 2, 1, 1, kColStairLight);
	rect(gfx, px + 1, py + 2, 1, 1, kColStairDark);
	rect(gfx, px + 2, py + 2, 1, 1, kColStairLight);
}

// Party arrow -- a 5-pixel triangle pointing along fdir, painted with three
// stacked bars of decreasing width so it reads clearly at 5x5. RGB-based so
// the colour stays stable across dungeon palette swaps.
void drawArrow(GRAPHICS::Graphics &gfx, int cx, int cy, int fdir, RGB col) {
	// Three stack levels: at depth d from tip (0=tip, 2=base), the bar is
	// (2*d + 1) pixels wide and sits perpendicular to facing.
	for (int d = 0; d <= 2; ++d) {
		int half = d; // half-width (excluding centre)
		int px, py, w, h;
		switch (fdir & 3) {
		case 0: // N: tip up, bars widen downward
			px = cx - half; py = cy - 2 + d; w = 2 * half + 1; h = 1; break;
		case 1: // E: tip right, bars narrow rightward
			px = cx + 2 - d; py = cy - half; w = 1; h = 2 * half + 1; break;
		case 2: // S: tip down
			px = cx - half; py = cy + 2 - d; w = 2 * half + 1; h = 1; break;
		default: // W: tip left
			px = cx - 2 + d; py = cy - half; w = 1; h = 2 * half + 1; break;
		}
		rect(gfx, px, py, w, h, col);
	}
}

} // namespace

void tick(VM::ObjectSystem &objects) {
	int kn = objects.firstObjectOfClass(kKernelCls);
	int dn = objects.firstObjectOfClass(kDungeonCls);
	if (kn < 0 || dn < 0) return;

	uint8_t *xp = objects.classStaticPtr(kn, kKernelCls, 243, 1);
	uint8_t *yp = objects.classStaticPtr(kn, kKernelCls, 244, 1);
	uint8_t *fp = objects.classStaticPtr(kn, kKernelCls, 245, 1);
	uint8_t *lp = objects.classStaticPtr(kn, kKernelCls, 246, 1);
	uint8_t *lm = objects.classStaticPtr(dn, kDungeonCls, 0, kCellBits);
	if (!xp || !yp || !fp || !lp || !lm) return;

	int px = *xp, py = *yp, pf = *fp;
	int lvl = *lp;
	if (lvl < 0 || lvl >= kLevels) return;
	if (px < 0 || px >= kGridW || py < 0 || py >= kGridH) return;

	std::memcpy(g.lvlmap.data(), lm, kCellBits);
	g.partyX = px; g.partyY = py; g.partyF = pf; g.level = lvl;
	g.havePose = true;

	// Rebuild the "wall" cache: lvlmap-wall OR plane-0 blocker on a floor cell.
	g.wallBits.fill(0);
	for (int y = 0; y < kGridH; ++y) {
		for (int x = 0; x < kGridW; ++x) {
			bool wall = isMapWall(g.lvlmap[cellIdx(x, y)]) ||
			            plane0BlocksAt(objects, dn, x, y);
			if (wall) {
				int i = cellIdx(x, y);
				g.wallBits[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
			}
		}
	}

	// Own-visibility ray-cast: for every cell in a 3-wide x 3-deep forward
	// cone plus the immediate side cells at the party's row, draw a line
	// from the party to the target. Mark each cell along the way; stop
	// (marking the blocker itself so the wall/tree face is visible) at the
	// first cell whose lvlmap is non-floor or that carries a plane-0
	// blocker (tree, closed door, hidden door). That matches what the 3D
	// view renders: gaps and diagonals all fill correctly, and nothing
	// leaks past a blocker.
	bitSet(g.visited, lvl, px, py);
	Facing v = facingVecs(pf);
	auto isBlocker = [&](int cx, int cy) {
		return isMapWall(g.lvlmap[cellIdx(cx, cy)]) ||
		       plane0BlocksAt(objects, dn, cx, cy);
	};
	auto cast = [&](int dx, int dy) {
		int tx = px + dx, ty = py + dy;
		if (tx < 0 || tx >= kGridW || ty < 0 || ty >= kGridH) return;
		int steps = std::max(std::abs(dx), std::abs(dy));
		for (int s = 1; s <= steps; ++s) {
			int cx = px + dx * s / steps;
			int cy = py + dy * s / steps;
			bitSet(g.visited, lvl, cx, cy);
			if (s < steps && isBlocker(cx, cy)) break;
		}
	};
	// Immediate side cells (party's row) so adjacent walls read as known.
	cast(v.rx, v.ry);
	cast(-v.rx, -v.ry);
	// 3 columns (left/center/right of facing) x 3 depths ahead.
	for (int d = 1; d <= 3; ++d)
		for (int side = -1; side <= 1; ++side)
			cast(v.fx * d + v.rx * side, v.fy * d + v.ry * side);

	// Categorize each visited cell's plane-0 feature and plane-1 items.
	// Sticky: once flagged, stays flagged (chopped tree stump / picked-up
	// item / triggered lever leave a breadcrumb). Monsters (plane 2) are
	// intentionally not tracked -- would spoil unexplored territory.
	for (int y = 0; y < kGridH; ++y) {
		for (int x = 0; x < kGridW; ++x) {
			if (!bitGet(g.visited, lvl, x, y)) continue;
			if (lvlobjHead(objects, dn, 1, x, y) >= 0)
				bitSet(g.items, lvl, x, y);
			switch (plane0FeatureAt(objects, dn, x, y)) {
			case Feature::Stairs:    bitSet(g.stairs,     lvl, x, y); break;
			case Feature::Trap:      bitSet(g.traps,      lvl, x, y); break;
			case Feature::Activator: bitSet(g.activators, lvl, x, y); break;
			case Feature::Generic:   bitSet(g.notable,    lvl, x, y); break;
			case Feature::None: break;
			}
		}
	}
}

void toggle() { g.open = !g.open; }
void close()  { g.open = false; }
bool isOpen() { return g.open; }

void render(GRAPHICS::Graphics &gfx, RESOURCES::Resource &res) {
	if (!g.open) return;
	// Suspend backdrop writes for the span of our draw calls only. Between
	// frames the SOP updates mBackdrop normally, so on close the backdrop is
	// current and restoreBackdrop() cleanly wipes the overlay footprint. The
	// SOP leaves a clip on the dungeon view rect (~y<120); drop it so our
	// overlay can paint the full 320x200 -- next SOP redraw re-sets its own.
	gfx.suspendBackdrop(true);
	gfx.clearClip();

	// 1. Darken the whole frame (50% via checkerboard black -- palette index 0
	// is stably black across every dungeon palette).
	gfx.hashRect(0, 0, 319, 199, 0);

	// 2. Grid background + frame.
	int gx0 = kOx, gy0 = kOy;
	int gx1 = kOx + kGridW * kCell - 1;
	int gy1 = kOy + kGridH * kCell - 1;
	gfx.fillRectRGB(gx0 - 2, gy0 - 2, gx1 + 2, gy1 + 2,
	                kColBg.r, kColBg.g, kColBg.b);
	gfx.drawLine(gx0 - 2, gy0 - 2, gx1 + 2, gy0 - 2, kColBorderIdx);
	gfx.drawLine(gx0 - 2, gy1 + 2, gx1 + 2, gy1 + 2, kColBorderIdx);
	gfx.drawLine(gx0 - 2, gy0 - 2, gx0 - 2, gy1 + 2, kColBorderIdx);
	gfx.drawLine(gx1 + 2, gy0 - 2, gx1 + 2, gy1 + 2, kColBorderIdx);

	// 3. Cells. Un-visited stay black (fog). Visited: floor or wall square,
	// possibly with a MARK dot on top for notable cells.
	if (g.havePose) {
		for (int y = 0; y < kGridH; ++y) {
			for (int x = 0; x < kGridW; ++x) {
				if (!bitGet(g.visited, g.level, x, y)) continue;
				int px = gx0 + x * kCell;
				int py = gy0 + y * kCell;
				int i = cellIdx(x, y);
				bool wall = (g.wallBits[i >> 3] >> (i & 7)) & 1;
				// Floor + wall both fill the whole cell -- adjacent walls
				// then read as a continuous stone segment rather than a row
				// of separated tiles.
				rect(gfx, px, py, kCell, kCell, wall ? kColWall : kColFloor);
				// Category dot on top (3x3 centred, 1px border of the
				// underlying tile visible around it). Priority: stairs beats
				// items beats activator beats generic mark -- so a chest on
				// a lever is drawn as ITEM, stairs at a lever spot as STAIRS.
				if (bitGet(g.stairs, g.level, x, y)) {
					drawStairs(gfx, px + 1, py + 1);
					continue;
				}
				const RGB *dot = nullptr;
				if      (bitGet(g.traps,      g.level, x, y)) dot = &kColTrap;
				else if (bitGet(g.items,      g.level, x, y)) dot = &kColItem;
				else if (bitGet(g.activators, g.level, x, y)) dot = &kColActivator;
				else if (bitGet(g.notable,    g.level, x, y)) dot = &kColNotable;
				if (dot)
					rect(gfx, px + 1, py + 1, kCell - 2, kCell - 2, *dot);
			}
		}
		// Party arrow on a SEEN-cream backing (so the party cell reads as
		// "we're standing here" rather than "there's a red thing on black").
		int cx = gx0 + g.partyX * kCell + kCell / 2;
		int cy = gy0 + g.partyY * kCell + kCell / 2;
		rect(gfx, cx - 2, cy - 2, 5, 5, kColFloor);
		drawArrow(gfx, cx, cy, g.partyF, kColParty);
	}

	// 4. Title + legend text. EYE.RES doesn't expose fonts by string name (the
	// SOP indexes them by resource number), so we load CHARGEN/FONT8.FNT off
	// disk once and cache. Same file the chargen already reads.
	static std::vector<uint8_t> sFont;
	static bool sFontProbed = false; // probe once -- not every frame on a
	                                 // fontless install (CodeRabbit)
	if (!sFontProbed) {
		sFontProbed = true;
		auto dir = res.resourcePath().parent_path();
		// CHARGEN subdir may be lower-case on some installs; try both.
		std::filesystem::path candidates[] = {
			dir / "CHARGEN" / "FONT8.FNT",
			dir / "chargen" / "FONT8.FNT",
			dir / "FONT8.FNT",
		};
		for (const auto &p : candidates) {
			std::ifstream in(p, std::ios::binary);
			if (!in) continue;
			sFont.assign(std::istreambuf_iterator<char>(in), {});
			break;
		}
	}
	if (!sFont.empty()) {
		char title[32];
		std::snprintf(title, sizeof(title), "LEVEL %d", g.level);
		gfx.drawText(sFont, title, kOx, 6);

		struct Entry { RGB col; const char *label; bool stairGlyph; };
		const Entry entries[] = {
			{ kColParty,     "PARTY",  false },
			{ kColFloor,     "SEEN",   false },
			{ kColWall,      "WALL",   false },
			{ kColBg,        "STAIRS", true  },
			{ kColTrap,      "TRAP",   false },
			{ kColItem,      "ITEM",   false },
			{ kColActivator, "SWITCH", false },
			{ kColNotable,   "MARK",   false },
			{ kColBg,        "FOG",    false },
		};
		int ly = kOy;
		for (const auto &e : entries) {
			rect(gfx, kLegendX, ly, 8, 8, e.col);
			if (e.stairGlyph) {
				// The staircase glyph, scaled x2 in the 8x8 swatch: same
				// pattern as the in-grid icon, 2px per tread pixel.
				rect(gfx, kLegendX + 5, ly + 1, 2, 2, kColStairLight);
				rect(gfx, kLegendX + 3, ly + 3, 2, 2, kColStairLight);
				rect(gfx, kLegendX + 5, ly + 3, 2, 2, kColStairDark);
				rect(gfx, kLegendX + 1, ly + 5, 2, 2, kColStairLight);
				rect(gfx, kLegendX + 3, ly + 5, 2, 2, kColStairDark);
				rect(gfx, kLegendX + 5, ly + 5, 2, 2, kColStairLight);
			}
			gfx.drawLine(kLegendX, ly, kLegendX + 7, ly, kColBorderIdx);
			gfx.drawLine(kLegendX, ly + 7, kLegendX + 7, ly + 7, kColBorderIdx);
			gfx.drawLine(kLegendX, ly, kLegendX, ly + 7, kColBorderIdx);
			gfx.drawLine(kLegendX + 7, ly, kLegendX + 7, ly + 7, kColBorderIdx);
			gfx.drawText(sFont, e.label, kLegendX + 14, ly);
			ly += 12;
		}
		gfx.drawText(sFont, "M CLOSE", kLegendX, ly + 8);
	}

	gfx.suspendBackdrop(false);
}

// Serialization: 4-byte magic + level bitsets (visited + 4 category flags).
// MAP2 (MAP1 had only visited+notable; loading a MAP1 file returns false ->
// the automap starts fogged, no worse than never having saved).
static constexpr char kMagic[4] = { 'M','A','P','3' };

bool saveTo(const std::filesystem::path &file) {
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if (!out) return false;
	out.write(kMagic, 4);
	auto w = [&](const auto &a) {
		out.write(reinterpret_cast<const char *>(a.data()),
		          static_cast<std::streamsize>(a.size()));
	};
	w(g.visited);
	w(g.stairs);
	w(g.traps);
	w(g.items);
	w(g.activators);
	w(g.notable);
	return static_cast<bool>(out);
}

bool loadFrom(const std::filesystem::path &file) {
	std::ifstream in(file, std::ios::binary);
	if (!in) return false;
	char m[4]{};
	in.read(m, 4);
	if (std::memcmp(m, kMagic, 4) != 0) return false;
	// Stage into temporaries and commit only after every read succeeds, so a
	// truncated MAPS file can't leave g.* half-old half-new (CodeRabbit).
	decltype(g.visited) visited, stairs, traps, items, activators, notable;
	auto r = [&](auto &a) {
		in.read(reinterpret_cast<char *>(a.data()),
		        static_cast<std::streamsize>(a.size()));
	};
	r(visited);
	r(stairs);
	r(traps);
	r(items);
	r(activators);
	r(notable);
	if (!in) return false;
	g.visited = visited;
	g.stairs = stairs;
	g.traps = traps;
	g.items = items;
	g.activators = activators;
	g.notable = notable;
	return true;
}

void reset() {
	g.visited.fill(0);
	g.stairs.fill(0);
	g.traps.fill(0);
	g.items.fill(0);
	g.activators.fill(0);
	g.notable.fill(0);
	g.havePose = false;
	g.open = false;
}

} // namespace THIRDEYE::automap
