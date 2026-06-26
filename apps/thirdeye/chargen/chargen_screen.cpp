#include "chargen_screen.hpp"

#include "../graphics/graphics.hpp"
#include "../graphics/bitmap.hpp"
#include "../graphics/cps.hpp"
#include "../runtime/internal.hpp" // QuitRequested

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

namespace {

std::vector<uint8_t> readFile(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
}

// Slot frames are baked into the CPS backdrop. Both rows have the same
// layout: left slot at x=17, right slot at x=81, 32-px decorative gap
// between them. Frame lines at y=63/96 (top row) and y=127/160 (bottom row).
// 32x32 portraits centered on the frame line so the portrait's top-row
// stone-blend padding (palette 0xac) merges into the frame visually.
struct Slot { int x, y; };
constexpr Slot kSlots[4] = { {17, 64}, {81, 64}, {17, 128}, {81, 128} };

// 12 race-gender combinations, listed in the order the original chargen
// shows them. Names are uppercase to match FONT6/FONT8 menu rendering.
const std::array<const char *, 12> kRaceNames = { {
	"HUMAN MALE",    "HUMAN FEMALE",
	"ELF MALE",      "ELF FEMALE",
	"HALF-ELF MALE", "HALF-ELF FEMALE",
	"DWARF MALE",    "DWARF FEMALE",
	"GNOME MALE",    "GNOME FEMALE",
	"HALFLING MALE", "HALFLING FEMALE",
} };

// 6 base classes in the order the original chargen shows them. The real
// game filters by race/alignment (a half-elf can't be a paladin, etc.) --
// not yet implemented; the picker accepts any combination for now.
const std::array<const char *, 6> kClassNames = { {
	"FIGHTER", "RANGER", "PALADIN", "MAGE", "CLERIC", "THIEF",
} };

// 9 AD&D alignments in the chargen's listing order.
const std::array<const char *, 9> kAlignmentNames = { {
	"LAWFUL GOOD",    "NEUTRAL GOOD",    "CHAOTIC GOOD",
	"LAWFUL NEUTRAL", "TRUE NEUTRAL",    "CHAOTIC NEUTRAL",
	"LAWFUL EVIL",    "NEUTRAL EVIL",    "CHAOTIC EVIL",
} };

enum class Step {
	EntryScreen,    // 4-slot picker, "Select the box of..." prompt
	PickRace,       // race list in right panel
	PickClass,      // class list
	PickAlignment,  // 9 AD&D alignments
	ShowStats,      // rolled abilities + REROLL/MODIFY/FACES/KEEP
	ModifyStats,    // per-ability +/-, selected stat value shown red
	PickPortrait,   // 4x4 grid of CHARPICS portraits with paging
	EnterName,      // type a name (max 10 chars), Enter commits + writes CREATE.SAV
};

// Ability indices, in the order they appear on screen.
enum Abil { STR=0, INTEL, WIS, DEX, CON, CHA, NUM_ABIL };

struct CharSheet {
	bool filled    = false;  // true when a complete character is rolled
	int  race      = -1;     // 0..11
	int  klass     = -1;     // 0..5
	int  alignment = -1;     // 0..8
	int  abil[NUM_ABIL] = {0, 0, 0, 0, 0, 0};
	int  hp = 0, ac = 10, lvl = 1;
	int  portrait = 1;  // CHARPICS index; placeholder until picker lands
	char name[11] = {0}; // NUL-terminated, max 10 chars
};

struct State {
	Step step             = Step::EntryScreen;
	int  activeSlot       = -1;   // 0..3 when in a sub-step
	int  raceCursor       = 0;    // 0..11 in PickRace
	int  classCursor      = 0;    // 0..5  in PickClass
	int  alignmentCursor  = 0;    // 0..8  in PickAlignment
	int  statCursor       = 0;    // 0..5  in ModifyStats (STR..CHA)
	int  portraitCursor   = 0;    // 0..89 in PickPortrait (linear index)
	int  sparkleFrame     = 0;    // animation tick for active-slot sparkles
	CharSheet party[4];
	bool done             = false; // exit chargen entirely
	bool cancelled        = false; // true if user Esc'd out (return to title)
};

// Roll 3d6. Used for each ability score on the stats screen.
int roll3d6(std::mt19937 &rng) {
	std::uniform_int_distribution<int> d6(1, 6);
	return d6(rng) + d6(rng) + d6(rng);
}

// AD&D 1e CON bonus to HP per hit die. Fighters/Paladins/Rangers get the
// full bonus; other classes cap at +2 per traditional rules, but EOB-era
// simplified -- we use the warrior bonus across the board (the in-game
// HP cap will trim if it matters).
int conHpBonus(int con) {
	if (con <= 3)  return -2;
	if (con <= 5)  return -1;
	if (con <= 14) return  0;
	if (con == 15) return  1;
	if (con == 16) return  2;
	if (con == 17) return  3;
	return  4;  // 18+
}

// HP die per class (chargen UI order: F, R, P, M, C, T).
int classHpDie(int chargenClass) {
	switch (chargenClass) {
	case 0: return 10; // FIGHTER  -- d10
	case 1: return  8; // RANGER   -- d8
	case 2: return 10; // PALADIN  -- d10
	case 3: return  4; // MAGE     -- d4
	case 4: return  8; // CLERIC   -- d8
	case 5: return  6; // THIEF    -- d6
	}
	return 8;
}

// Roll a fresh character sheet for the given race/class/alignment. EOB3's
// campaign opens at ~level 11; we roll N hit dice (one per level) plus the
// CON bonus per HD. Fixed level 11 for now -- the original CHGEN.EXE has
// a configurable start-level we haven't RE'd yet.
void rollStats(CharSheet &c, std::mt19937 &rng) {
	for (int i = 0; i < NUM_ABIL; ++i)
		c.abil[i] = roll3d6(rng);
	c.lvl = 11;
	int die = classHpDie(c.klass);
	int bonusPerHd = conHpBonus(c.abil[CON]);
	std::uniform_int_distribution<int> hpRoll(1, die);
	int hp = 0;
	for (int hd = 0; hd < c.lvl; ++hd)
		hp += hpRoll(rng) + bonusPerHd;
	c.hp = std::max(1, hp);  // never below 1
	c.ac = 10;
}

// Hit-test the four slot rectangles. Returns 0..3 on hit, -1 otherwise.
int slotAt(int x, int y) {
	for (int i = 0; i < 4; ++i) {
		if (x >= kSlots[i].x && x < kSlots[i].x + 32 &&
		    y >= kSlots[i].y && y < kSlots[i].y + 32) {
			return i;
		}
	}
	return -1;
}

// Read a logical (320x200) mouse position from an SDL mouse event.
void mousePos(GRAPHICS::Graphics &gfx, const SDL_Event &e, int &lx, int &ly) {
	int wx = (e.type == SDL_MOUSEMOTION) ? e.motion.x : e.button.x;
	int wy = (e.type == SDL_MOUSEMOTION) ? e.motion.y : e.button.y;
	gfx.mouseToLogical(wx, wy, lx, ly);
}

// Draw text with a drop-shadow at (+1, +1) in a dark palette index, then the
// foreground glyphs over it. Matches the original chargen's text look --
// the engine drew every label this way to make text pop off the stone
// backdrop. paletteIndex==0xff means "default white" (use drawText).
void drawShadowed(GRAPHICS::Graphics &gfx, std::vector<uint8_t> &fnt,
                  const std::string &text, int x, int y,
                  uint8_t fgIndex = 0xff) {
	// Shadow: palette 0 (true black in the chargen palette).
	gfx.drawTextColored(fnt, text, x + 1, y + 1, 0x00);
	// Foreground: white via drawText if fgIndex is sentinel, else tinted.
	if (fgIndex == 0xff)
		gfx.drawText(fnt, text, x, y);
	else
		gfx.drawTextColored(fnt, text, x, y, fgIndex);
}

// Blit a sub-rect of an indexed (8 bpp) source buffer onto the screen.
// Used to grab individual button sprites out of CHARGENB.CPS without
// needing a full Bitmap container around it.
void drawSubIndexed(GRAPHICS::Graphics &gfx,
                    const std::vector<uint8_t> &src, int srcW,
                    int srcX, int srcY, int w, int h,
                    int destX, int destY, bool transparent = false) {
	if (src.empty()) return;
	std::vector<uint8_t> sub(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; ++y) {
		size_t row = static_cast<size_t>(srcY + y) * srcW + srcX;
		std::memcpy(sub.data() + y * w, src.data() + row, w);
	}
	gfx.drawIndexed(sub, w, h, destX, destY, transparent);
}

// Source-rect of the blank blue button sprite inside CHARGENB.CPS (small
// 32x14 button, top-left at native 128,128). The wider PLAY/DELETE
// composite sprites sit next to it; we draw text on top of this blank
// to compose BACK/KEEP/REROLL etc.
constexpr int kButtonW = 32;
constexpr int kButtonH = 14;
constexpr int kButtonSrcX = 128;
constexpr int kButtonSrcY = 128;

// Where the BACK button lands on screen (measured against the dosbox
// class picker -- bottom-right of the right info panel).
constexpr int kBackBtnX = 272;
constexpr int kBackBtnY = 172;

// Stats-screen button layout: 2x2 grid at the bottom-right of the right
// panel (REROLL/MODIFY in the top row, FACES/KEEP in the bottom). Per
// user feedback the buttons are tight against each other (no gap) -- 44
// wide each, side-by-side -- and the gold labels render with a tight
// 6-px pitch so 6-char labels fit centered. Synthesized from the 32-wide
// blank sprite by drawWideButton.
constexpr int kStatsBtnW = 44;
constexpr int kStatsBtnH = 14;
constexpr int kStatsBtnPitch = 6; // tight FONT6 pitch for labels
struct StatsBtn { int x, y; const char *label; };
constexpr StatsBtn kStatsBtns[4] = {
	{ 218, 162, "REROLL" },
	{ 262, 162, "MODIFY" },
	{ 218, 176, "FACES"  },  // = 162 + kStatsBtnH so rows touch
	{ 262, 176, "KEEP"   },
};
enum StatsBtnId { BTN_REROLL=0, BTN_MODIFY, BTN_FACES, BTN_KEEP };

int statsBtnAt(int lx, int ly) {
	for (int i = 0; i < 4; ++i) {
		const auto &b = kStatsBtns[i];
		if (lx >= b.x && lx < b.x + kStatsBtnW &&
		    ly >= b.y && ly < b.y + kStatsBtnH)
			return i;
	}
	return -1;
}

// Modify-screen button row: +, -, OK at bottom-right of the right panel.
constexpr int kModBtnY = 172;
struct ModBtn { int x, w; const char *label; };
constexpr ModBtn kModBtns[3] = {
	{ 220, 22, "+"  },
	{ 242, 22, "-"  },  // tight against +
	{ 264, 42, "OK" },  // tight against -
};
enum ModBtnId { BTN_PLUS=0, BTN_MINUS, BTN_OK };

int modBtnAt(int lx, int ly) {
	if (ly < kModBtnY || ly >= kModBtnY + kStatsBtnH) return -1;
	for (int i = 0; i < 3; ++i) {
		if (lx >= kModBtns[i].x && lx < kModBtns[i].x + kModBtns[i].w)
			return i;
	}
	return -1;
}

// Portrait carousel: horizontal strip of 4 portraits with two arrow buttons
// stacked vertically to the left (prev on top, next on bottom). The cursor
// portrait is the leftmost visible cell; the arrows scroll the visible
// window through all 90 portraits.
constexpr int kPortraitVisible = 4;
constexpr int kPortraitStride = 32;
// Arrows use the full 32-wide blank-blue sprite so their right border
// isn't cropped (a 16-wide crop showed only the left half). 16 tall each
// and stacked exactly touching so the strip reads as one tall button
// pair, matching the height of the 32-tall portrait row.
constexpr int kArrowX = 142;        // +2 right per user feedback
constexpr int kArrowYTop = 64;
constexpr int kArrowYBot = 80;      // = kArrowYTop + kArrowH (no gap)
constexpr int kArrowW = 32;
constexpr int kArrowH = 16;
// Strip starts after the arrows; 4 * 32 = 128 wide, fits in the panel.
// +1 right + 1 down nudge per user feedback for visual centering.
constexpr int kPortraitStripX = 177;
constexpr int kPortraitStripY = 65;

void portraitCellXY(int cellIdx, int &x, int &y) {
	x = kPortraitStripX + cellIdx * kPortraitStride;
	y = kPortraitStripY;
}

int portraitCellAt(int lx, int ly) {
	if (ly < kPortraitStripY || ly >= kPortraitStripY + 32) return -1;
	int col = (lx - kPortraitStripX) / kPortraitStride;
	if (col < 0 || col >= kPortraitVisible) return -1;
	return col;
}

bool inArrow(int lx, int ly, int y) {
	return lx >= kArrowX && lx < kArrowX + kArrowW &&
	       ly >= y && ly < y + kArrowH;
}

void drawBackButton(GRAPHICS::Graphics &gfx,
                    const std::vector<uint8_t> &chargenbPx,
                    std::vector<uint8_t> &fnt6Bytes,
                    std::vector<uint8_t> &fntBytes) {
	drawSubIndexed(gfx, chargenbPx, 320, kButtonSrcX, kButtonSrcY,
	               kButtonW, kButtonH, kBackBtnX, kBackBtnY);
	// "BACK" centered on the 32-wide button using a tight 6-px pitch (FONT6
	// glyphs are 8-wide but only ~5 cols visible). 4 chars * 6 = 24 px,
	// centred at (32 - 24) / 2 = 4. Gold (palette 0x1b).
	auto &fnt = fnt6Bytes.empty() ? fntBytes : fnt6Bytes;
	gfx.drawTextColored(fnt, "BACK", kBackBtnX + 4, kBackBtnY + 4, 0x1b, 6);
}

bool inBackButton(int lx, int ly) {
	return lx >= kBackBtnX && lx < kBackBtnX + kButtonW &&
	       ly >= kBackBtnY && ly < kBackBtnY + kButtonH;
}

// Synthesize a wider button by tiling the 32-wide blank sprite: left half
// (16 cols) + right half (16 cols) + the seam column repeated to fill the
// middle. Works because the blank blue button is a left-bordered + middle
// gradient + right-bordered design -- stretching the mid column reads as
// "wider button" without an obvious seam.
void drawWideButton(GRAPHICS::Graphics &gfx,
                    const std::vector<uint8_t> &src,
                    int destX, int destY, int destW) {
	if (src.empty() || destW < kButtonW) return;
	constexpr int halfW = kButtonW / 2; // 16
	// Left half from sprite cols 0..15
	drawSubIndexed(gfx, src, 320, kButtonSrcX, kButtonSrcY,
	               halfW, kButtonH, destX, destY);
	// Right half from sprite cols 16..31
	drawSubIndexed(gfx, src, 320, kButtonSrcX + halfW, kButtonSrcY,
	               halfW, kButtonH, destX + destW - halfW, destY);
	// Middle stretch: column-strip from the sprite's seam at col 15.
	int midSpan = destW - 2 * halfW;
	for (int dx = 0; dx < midSpan; ++dx) {
		drawSubIndexed(gfx, src, 320, kButtonSrcX + halfW - 1, kButtonSrcY,
		               1, kButtonH, destX + halfW + dx, destY);
	}
}

// --- rendering -------------------------------------------------------

// Repaint the whole screen from the CPS backdrop + party portraits +
// step-specific overlay. Cheaper than tracking dirty rects, and the
// chargen screen isn't perf-sensitive (one update per input event).
void render(GRAPHICS::Graphics &gfx,
            const std::vector<uint8_t> &picBytes,
            std::vector<uint8_t> &fntBytes,
            std::vector<uint8_t> &fnt6Bytes,
            const GRAPHICS::Cps &backdrop,
            const GRAPHICS::Cps &chargenb,
            const std::vector<uint8_t> &previewIdxPlaceholder,
            const State &state) {
	// Backdrop: full CPS or grey fallback.
	if (!backdrop.pixels.empty())
		gfx.drawIndexed(backdrop.pixels, backdrop.width, backdrop.height, 0, 0);
	else
		gfx.fillRect(0, 0, WIDTH - 1, HEIGHT - 1, 0);

	// Slot portraits. Only filled slots draw a portrait -- empty slots show
	// the bare backdrop. The active slot during a sub-step gets a sparkles
	// overlay (extracted from CHARGENB.CPS's top decorative strip) so the
	// user can see which slot they're working on.
	auto picCopy = picBytes;
	for (int i = 0; i < 4; ++i) {
		bool isActive = (state.step != Step::EntryScreen && state.activeSlot == i);
		if (isActive && !chargenb.pixels.empty()) {
			// Sparkles: CHARGENB.CPS top strip (y=0..31, x=0..319) holds a
			// row of star sprites. We slice a 32x32 window from it and
			// shift the source X over time (driven by state.sparkleFrame
			// in the main loop) so the stars appear to twinkle. Palette
			// index 0 is transparent so the slot frame shows through.
			constexpr int kStripFrames = 320 / 32; // 10 distinct windows
			int srcX = (state.sparkleFrame % kStripFrames) * 32;
			drawSubIndexed(gfx, chargenb.pixels, 320, srcX, 0, 32, 32,
			               kSlots[i].x, kSlots[i].y,
			               /*transparent=*/true);
			continue;
		}
		if (!state.party[i].filled) continue;
		uint16_t portraitIdx = static_cast<uint16_t>(state.party[i].portrait);
		gfx.drawImage(picCopy, portraitIdx,
		              kSlots[i].x, kSlots[i].y,
		              /*transparency=*/false, /*mirror=*/0, /*cacheId=*/0);
	}

	// Step-specific right-panel content.
	switch (state.step) {
	case Step::EntryScreen: {
		if (fntBytes.empty()) break;
		// Body text positioned to match dosbox: drop-shadowed (+1,+1 black
		// under white), starts at x=156, y=90, with 10-px line spacing.
		drawShadowed(gfx, fntBytes, "Select the box of",  156,  90);
		drawShadowed(gfx, fntBytes, "the character you",  156, 100);
		drawShadowed(gfx, fntBytes, "wish to create or",  156, 110);
		drawShadowed(gfx, fntBytes, "view.",               156, 120);
		break;
	}
	case Step::PickRace:
	case Step::PickClass:
	case Step::PickAlignment: {
		if (fntBytes.empty()) break;
		// Header + list layout is shared across pickers; the per-step bits
		// are the header text, the entry array, and which cursor selects.
		const char *header;
		const char *const *entries;
		int numEntries, cursor;
		switch (state.step) {
		case Step::PickRace:
			header     = "SELECT RACE:";
			entries    = kRaceNames.data();
			numEntries = static_cast<int>(kRaceNames.size());
			cursor     = state.raceCursor;
			break;
		case Step::PickClass:
			header     = "SELECT CLASS:";
			entries    = kClassNames.data();
			numEntries = static_cast<int>(kClassNames.size());
			cursor     = state.classCursor;
			break;
		case Step::PickAlignment:
		default:
			header     = "SELECT ALIGNMENT:";
			entries    = kAlignmentNames.data();
			numEntries = static_cast<int>(kAlignmentNames.size());
			cursor     = state.alignmentCursor;
			break;
		}
		// Header in FONT8 (taller), drop-shadowed in light blue
		// (palette 0x90 = 80,196,252). Matches the dosbox accent colour
		// for picker titles.
		drawShadowed(gfx, fntBytes, header, 144, 70, 0x90);
		// Entries in FONT6 (uppercase, tighter). 8-px line spacing matches
		// dosbox exactly; the drop shadow adds 1 px, so going tighter
		// collides (FONT6 glyphs are 6 tall).
		auto &listFnt = fnt6Bytes.empty()
			? const_cast<std::vector<uint8_t> &>(fntBytes) : fnt6Bytes;
		const int kListX = 144;
		const int kListY0 = 82;
		const int kListStep = 8;
		for (int i = 0; i < numEntries; ++i) {
			int y = kListY0 + i * kListStep;
			// Selected row in red (palette 0x12 = 220,48,44). Header is
			// blue, selected entry is red -- the dosbox convention.
			uint8_t fg = (i == cursor) ? 0x12 : 0xff;
			drawShadowed(gfx, listFnt, entries[i], kListX, y, fg);
		}
		// BACK button at the bottom-right of the right panel. Sprite from
		// CHARGENB.CPS, gold "BACK" text overlaid. Click handler treats it
		// as Esc for the active picker.
		if (!chargenb.pixels.empty())
			drawBackButton(gfx, chargenb.pixels, fnt6Bytes, fntBytes);
		break;
	}
	case Step::PickPortrait: {
		// Horizontal carousel: 4 portraits visible, 2 arrow buttons stacked
		// to the left. Cursor scrolls through the 90 CHARPICS portraits;
		// the leftmost visible cell holds the cursor portrait.
		int total = static_cast<int>(GRAPHICS::Bitmap(picBytes).getNumberOfBitmaps());
		int strip0 = state.portraitCursor;
		auto picCopy = picBytes;
		// Draw 4 visible portraits (wrap if past the end).
		for (int i = 0; i < kPortraitVisible; ++i) {
			int idx = (strip0 + i) % total;
			int x, y; portraitCellXY(i, x, y);
			gfx.drawImage(picCopy, static_cast<uint16_t>(idx), x, y,
			              /*transparency=*/false, 0, 0);
		}
		// Cursor outline around cell 0 (the chosen portrait).
		{
			int x, y; portraitCellXY(0, x, y);
			gfx.fillRect(x - 1, y - 1, x + 32, y - 1, 0x90);
			gfx.fillRect(x - 1, y + 32, x + 32, y + 32, 0x90);
			gfx.fillRect(x - 1, y - 1, x - 1, y + 32, 0x90);
			gfx.fillRect(x + 32, y - 1, x + 32, y + 32, 0x90);
		}
		// Two arrow buttons stacked left: prev (top), next (bottom). Full
		// 32-wide blank-blue sprite (so the right border isn't cropped).
		// Gold "<" / ">" centered in the button.
		if (!chargenb.pixels.empty()) {
			drawSubIndexed(gfx, chargenb.pixels, 320, kButtonSrcX, kButtonSrcY,
			               kArrowW, kArrowH, kArrowX, kArrowYTop);
			drawSubIndexed(gfx, chargenb.pixels, 320, kButtonSrcX, kButtonSrcY,
			               kArrowW, kArrowH, kArrowX, kArrowYBot);
			if (!fntBytes.empty()) {
				// Glyph is ~5 px visually; centre in 32-wide button.
				gfx.drawTextColored(fntBytes, "<", kArrowX + 13, kArrowYTop + 4, 0x1b);
				gfx.drawTextColored(fntBytes, ">", kArrowX + 13, kArrowYBot + 4, 0x1b);
			}
		}
		// Race + class labels + LARGE stats below. All text shifted right
		// +16 px from previous layout per user feedback.
		if (!fntBytes.empty()) {
			const auto &c = state.party[state.activeSlot];
			const char *raceName  = (c.race  >= 0) ? kRaceNames[c.race]   : "";
			const char *className = (c.klass >= 0) ? kClassNames[c.klass] : "";
			// All carousel-page text moved up 2 px per user feedback.
			drawShadowed(gfx, fntBytes, raceName,  192, 106);
			drawShadowed(gfx, fntBytes, className, 204, 118);
			// 10-px line height; name + value at fixed X each so the
			// AC/HP/LVL values stay column-aligned despite LVL being
			// one char longer than AC/HP.
			auto statLine = [&](int row, const char *name, int value) {
				char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
				int y = 130 + row * 10;
				drawShadowed(gfx, fntBytes, name, 156, y);
				drawShadowed(gfx, fntBytes, valBuf, 188, y);
			};
			auto rightLine = [&](int row, const char *name, int value) {
				char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
				int y = 130 + row * 10;
				drawShadowed(gfx, fntBytes, name, 240, y);
				drawShadowed(gfx, fntBytes, valBuf, 280, y);
			};
			statLine(0, "STR ", c.abil[STR]);
			statLine(1, "INT ", c.abil[INTEL]);
			statLine(2, "WIS ", c.abil[WIS]);
			statLine(3, "DEX ", c.abil[DEX]);
			statLine(4, "CON ", c.abil[CON]);
			statLine(5, "CHA ", c.abil[CHA]);
			rightLine(0, "AC ",  c.ac);
			rightLine(1, "HP ",  c.hp);
			rightLine(2, "LVL ", c.lvl);
		}
		break;
	}
	case Step::EnterName:
	case Step::ShowStats:
	case Step::ModifyStats: {
		if (fntBytes.empty()) break;
		const auto &c = state.party[state.activeSlot];
		const bool modifying = (state.step == Step::ModifyStats);
		// Layout (consistent across ShowStats / ModifyStats / EnterName).
		// Portrait sits INSIDE the stone window, then the Name row, race,
		// class, the 6-row stat block, and the button grid (original
		// position y=162). Everything below the portrait is shifted down
		// by 8 px from the previous layout per user request.
		//   portrait    y=66 .. 98
		//   Name row    y=100         (blank in ShowStats/Modify, prompt in EnterName)
		//   Race        y=110
		//   Class       y=120
		//   Stats       y=130..175    (6 rows, 9-px FONT8 pitch)
		//   Buttons     y=162         (rows 5-6 of stats overlap the button area)
		gfx.drawImage(const_cast<std::vector<uint8_t> &>(picBytes),
		              static_cast<uint16_t>(c.portrait),
		              220, 66, /*transparency=*/false, 0, 0);
		const char *raceName  = (c.race  >= 0) ? kRaceNames[c.race]   : "";
		const char *className = (c.klass >= 0) ? kClassNames[c.klass] : "";
		drawShadowed(gfx, fntBytes, raceName,  192, 110);
		drawShadowed(gfx, fntBytes, className, 204, 120);
		// Ability scores in FONT8 (larger), 2 columns. 10-px line height
		// keeps FONT8's 8-tall glyphs from colliding while still hugging
		// the class label tightly (first stat row at y=102, right under
		// the class label). Name + value at fixed X each so right-column
		// AC/HP/LVL values column-align despite name lengths differing.
		auto abilLine = [&](int row, const char *name, int value, bool sel) {
			char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
			int y = 130 + row * 9;
			drawShadowed(gfx, fntBytes, name, 156, y);
			uint8_t fg = sel ? 0x12 : 0xff;
			drawShadowed(gfx, fntBytes, valBuf, 188, y, fg);
		};
		auto rightLine = [&](int row, const char *name, int value) {
			char valBuf[8]; std::snprintf(valBuf, sizeof(valBuf), "%2d", value);
			int y = 130 + row * 9;
			drawShadowed(gfx, fntBytes, name, 240, y);
			drawShadowed(gfx, fntBytes, valBuf, 280, y);
		};
		auto &listFnt = fnt6Bytes.empty() ? fntBytes : fnt6Bytes;
		for (int i = 0; i < NUM_ABIL; ++i) {
			static const char *names[NUM_ABIL] = {
				"STR ", "INT ", "WIS ", "DEX ", "CON ", "CHA "
			};
			abilLine(i, names[i], c.abil[i],
			         modifying && state.statCursor == i);
		}
		rightLine(0, "AC ",  c.ac);
		rightLine(1, "HP ",  c.hp);
		rightLine(2, "LVL ", c.lvl);
		// Button row(s) -- different per sub-state.
		if (state.step == Step::EnterName) {
			// "Name:" label sits BETWEEN portrait and race (y=82, the
			// dedicated name slot in the shared layout). Blue label
			// (palette 0x90, matches picker headers), typed text + cursor
			// in red. Centered horizontally with the race/class labels.
			drawShadowed(gfx, fntBytes, "Name:", 156, 100, 0x90);
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%s_", c.name);
			gfx.drawTextColored(fntBytes, buf, 200, 100, 0x12);
		} else if (modifying) {
			if (!chargenb.pixels.empty()) {
				for (const auto &b : kModBtns) {
					drawWideButton(gfx, chargenb.pixels, b.x, kModBtnY, b.w);
					int textW = static_cast<int>(std::strlen(b.label)) * kStatsBtnPitch;
					int tx = b.x + (b.w - textW) / 2;
					gfx.drawTextColored(listFnt, b.label,
					                    tx, kModBtnY + 4, 0x1b,
					                    kStatsBtnPitch);
				}
			}
		} else {
			if (!chargenb.pixels.empty()) {
				for (const auto &b : kStatsBtns) {
					drawWideButton(gfx, chargenb.pixels, b.x, b.y, kStatsBtnW);
					int textW = static_cast<int>(std::strlen(b.label)) * kStatsBtnPitch;
					int tx = b.x + (kStatsBtnW - textW) / 2;
					gfx.drawTextColored(listFnt, b.label, tx, b.y + 4, 0x1b,
					                    kStatsBtnPitch);
				}
			}
		}
		break;
	}
	}
}

// --- CREATE.SAV writer -----------------------------------------------
//
// Overlay our chargen-rolled fields onto the bundled CREATE.SAV template.
// Field offsets within each 345-byte PC record (rec_off = attr - 2, with
// the record starting at file 0x16 + pc*345). Verified live by tracing
// the xfer SOP's player_attrib calls in --debug mode:
//
//   rec  attr  size  field
//     0     2    11   name (NUL-padded)
//    12    14     1   STR
//    14    16     1   STR% (exceptional)
//    16    18     1   INT
//    18    20     1   WIS
//    20    22     1   DEX
//    22    24     1   CON
//    24    26     1   CHA
//    25-26 -      2   HP cur (unused by xfer -- set from hmax - lost_hp)
//    27    29     2   hmax (HP max)
//    31    33     1   race      (0..11 in our chargen order matches Bob=0)
//    32    34     1   classes   (single-class 0..5; multi-class uses higher)
//    33    35     1   alignment (0..8)
//    34    36     1   portrait  (CHARPICS index)
//    36    38     1   levels[0] (primary class level)
//    37    39     1   levels[1]
//    38    40     1   levels[2]
//    39    41     4   experience[0] (u32)
//    43    45     4   experience[1] (-1 if single-class)
//    47    49     4   experience[2] (-1 if single-class)
//
// Class remap: our chargen UI lists FIGHTER/RANGER/PALADIN/MAGE/CLERIC/
// THIEF, but the EOB1 byte uses F/M/C/T/P/R. Map at write time.
//
// HP / XP fall back to plausible level-11 fighter values (matching the
// default party's Bob); class-aware tables are a follow-up.
bool writeCreateSav(const std::filesystem::path &chargenDir,
                    const State &state) {
	auto path = chargenDir / "CREATE.SAV";
	auto bytes = readFile(path);
	if (bytes.size() < 0x894) {
		std::cout << "  [chargen: " << path
		          << " missing or too small to overlay]" << std::endl;
		return false;
	}
	constexpr size_t kPcBase = 0x16;
	constexpr size_t kPcStride = 345;
	// Chargen class index (UI order) -> EOB1 class byte. F=0,M=1,C=2,T=3,P=4,R=5.
	// Multi-class (default party's Alice has class byte 6 = some F/T variant)
	// uses values 6+; the exact bitmask/table isn't RE'd yet, so our chargen
	// only emits single-class (0..5). TODO: dual/multi-class UI + encoding.
	constexpr int kClassToEob1[6] = { 0, 5, 4, 1, 2, 3 };
	// Base XP values per EOB1 class @ level 11 (rough -- adequate for HUD
	// purposes; the game accepts a wide range here, won't reject the party).
	auto baseXp = [](int eob1Class) -> uint32_t {
		switch (eob1Class) {
		case 0: return 750000;  // FIGHTER
		case 1: return 600000;  // MAGE
		case 2: return 375000;  // CLERIC
		case 3: return 500000;  // THIEF
		case 4: return 750000;  // PALADIN
		case 5: return 750000;  // RANGER
		}
		return 750000;
	};
	auto patchU8 = [&](size_t off, uint8_t v) { bytes[off] = v; };
	auto patchU16 = [&](size_t off, uint16_t v) {
		bytes[off]     = v & 0xFF;
		bytes[off + 1] = (v >> 8) & 0xFF;
	};
	auto patchI32 = [&](size_t off, int32_t v) {
		bytes[off]     = v & 0xFF;
		bytes[off + 1] = (v >> 8) & 0xFF;
		bytes[off + 2] = (v >> 16) & 0xFF;
		bytes[off + 3] = (v >> 24) & 0xFF;
	};
	for (int pc = 0; pc < 4; ++pc) {
		const auto &c = state.party[pc];
		if (!c.filled) continue;
		size_t rec = kPcBase + pc * kPcStride;
		// Name (11 bytes NUL-padded).
		std::memset(bytes.data() + rec, 0, 11);
		std::memcpy(bytes.data() + rec, c.name,
		            std::min<size_t>(10, std::strlen(c.name)));
		// Ability pairs (max, current); write both bytes to the same value.
		auto writePair = [&](size_t pairOff, int val) {
			uint8_t v = static_cast<uint8_t>(std::max(0, std::min(99, val)));
			patchU8(rec + pairOff,     v);
			patchU8(rec + pairOff + 1, v);
		};
		writePair(11, c.abil[STR]);
		writePair(13, 0);              // STR%
		writePair(15, c.abil[INTEL]);
		writePair(17, c.abil[WIS]);
		writePair(19, c.abil[DEX]);
		writePair(21, c.abil[CON]);
		writePair(23, c.abil[CHA]);
		// HP: write both cur (rec+25) and max (rec+27) -- the xfer reads
		// only max, but Bob's template has them equal so let's match.
		uint16_t hp = static_cast<uint16_t>(std::max(0, c.hp));
		patchU16(rec + 25, hp);
		patchU16(rec + 27, hp);
		// race / class / alignment / portrait
		int eob1Class = (c.klass >= 0 && c.klass < 6) ? kClassToEob1[c.klass] : 0;
		patchU8(rec + 31, static_cast<uint8_t>(std::max(0, c.race)));
		patchU8(rec + 32, static_cast<uint8_t>(eob1Class));
		patchU8(rec + 33, static_cast<uint8_t>(std::max(0, c.alignment)));
		patchU8(rec + 34, static_cast<uint8_t>(c.portrait & 0xff));
		// levels[3]: single-class -> [c.lvl, 0, 0]
		patchU8(rec + 36, static_cast<uint8_t>(c.lvl));
		patchU8(rec + 37, 0);
		patchU8(rec + 38, 0);
		// experience[3]: XP1 = class-baseline, XP2/XP3 = -1
		patchI32(rec + 39, static_cast<int32_t>(baseXp(eob1Class)));
		patchI32(rec + 43, -1);
		patchI32(rec + 47, -1);
	}
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) {
		std::cout << "  [chargen: failed to open " << path
		          << " for write]" << std::endl;
		return false;
	}
	f.write(reinterpret_cast<const char *>(bytes.data()),
	        static_cast<std::streamsize>(bytes.size()));
	// Flush before checking: write() buffers, and a disk-full / I/O failure
	// only surfaces in the stream state after the buffer's been drained. A
	// successful open is not enough -- the SOP transfer reads this file
	// byte-for-byte and a short / corrupt CREATE.SAV silently mis-rolls the
	// imported party.
	f.flush();
	if (!f) {
		std::cout << "  [chargen: write to " << path << " failed (disk full?)]"
		          << std::endl;
		return false;
	}
	std::cout << "  [chargen: wrote " << bytes.size() << " bytes to "
	          << path << "]" << std::endl;
	return true;
}

// --- input handling --------------------------------------------------

void handleEntryEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                      State &state) {
	if (e.type == SDL_KEYDOWN) {
		auto k = e.key.keysym.sym;
		if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_p) {
			state.done = true;          // proceed to game
		} else if (k == SDLK_ESCAPE) {
			// Cancel: hand control back to the engine with a "go to title
			// menu" signal so the boot loop can reset mem[1264] = INTR.
			state.done      = true;
			state.cancelled = true;
		}
	} else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		int s = slotAt(lx, ly);
		if (s >= 0) {
			state.step       = Step::PickRace;
			state.activeSlot = s;
			state.raceCursor = (state.party[s].race >= 0)
			                        ? state.party[s].race : 0;
		}
	}
}

// Shared list-picker input handler. Returns true if the user committed,
// false if cancelled (Esc) or no transition.
struct PickerResult { bool committed = false; bool cancelled = false; };

PickerResult handleListPicker(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                              int &cursor, int numEntries) {
	PickerResult res;
	if (e.type == SDL_KEYDOWN) {
		auto k = e.key.keysym.sym;
		if (k == SDLK_UP)
			cursor = (cursor + numEntries - 1) % numEntries;
		else if (k == SDLK_DOWN)
			cursor = (cursor + 1) % numEntries;
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER)
			res.committed = true;
		else if (k == SDLK_ESCAPE)
			res.cancelled = true;
	} else if (e.type == SDL_MOUSEMOTION) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		// List rendered at y=82 + i*8 (FONT6, 8-px line height).
		if (lx >= 140 && lx < 305 && ly >= 82 && ly < 82 + 8 * numEntries) {
			int idx = (ly - 82) / 8;
			if (idx >= 0 && idx < numEntries)
				cursor = idx;
		}
	} else if (e.type == SDL_MOUSEBUTTONDOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		// BACK button takes priority — it's overlaid on the panel.
		if (inBackButton(lx, ly)) {
			res.cancelled = true;
		} else if (lx >= 140 && lx < 305 && ly >= 82 &&
		           ly < 82 + 8 * numEntries) {
			int idx = (ly - 82) / 8;
			if (idx >= 0 && idx < numEntries) {
				cursor = idx;
				res.committed = true;
			}
		}
	}
	return res;
}

void handleRacePickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                         State &state) {
	auto r = handleListPicker(e, gfx, state.raceCursor,
	                          static_cast<int>(kRaceNames.size()));
	if (r.committed) {
		state.party[state.activeSlot].race = state.raceCursor;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " race set to " << kRaceNames[state.raceCursor]
		          << "]" << std::endl;
		// Advance to class picker; the active slot stays the same. Reset
		// the class cursor to the previously-chosen class if any, else 0.
		state.classCursor = (state.party[state.activeSlot].klass >= 0)
		                        ? state.party[state.activeSlot].klass : 0;
		state.step = Step::PickClass;
	} else if (r.cancelled) {
		state.step       = Step::EntryScreen;
		state.activeSlot = -1;
	}
}

void handleClassPickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                          State &state) {
	auto r = handleListPicker(e, gfx, state.classCursor,
	                          static_cast<int>(kClassNames.size()));
	if (r.committed) {
		state.party[state.activeSlot].klass = state.classCursor;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " class set to " << kClassNames[state.classCursor]
		          << "]" << std::endl;
		// Advance to alignment picker; active slot stays.
		state.alignmentCursor =
			(state.party[state.activeSlot].alignment >= 0)
			    ? state.party[state.activeSlot].alignment : 0;
		state.step = Step::PickAlignment;
	} else if (r.cancelled) {
		// Step back to race picker (so user can change race without
		// retracing everything). Active slot stays.
		state.step = Step::PickRace;
	}
}

void handleAlignmentPickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                              State &state, std::mt19937 &rng) {
	auto r = handleListPicker(e, gfx, state.alignmentCursor,
	                          static_cast<int>(kAlignmentNames.size()));
	if (r.committed) {
		state.party[state.activeSlot].alignment = state.alignmentCursor;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " alignment set to "
		          << kAlignmentNames[state.alignmentCursor] << "]"
		          << std::endl;
		// Roll stats + advance to portrait carousel. After the user picks
		// a portrait, the flow lands on ShowStats with REROLL/MODIFY/
		// FACES/KEEP buttons.
		rollStats(state.party[state.activeSlot], rng);
		state.portraitCursor =
			(state.party[state.activeSlot].portrait >= 0)
				? state.party[state.activeSlot].portrait : 0;
		state.step = Step::PickPortrait;
	} else if (r.cancelled) {
		state.step = Step::PickClass;
	}
}

void enterNameStep(State &state) {
	// Pre-seed name input with the previous name (if any) so KEEP -> name
	// retains what was typed; otherwise blank.
	state.step = Step::EnterName;
	// SDL_StartTextInput must be enabled to receive SDL_TEXTINPUT events.
	SDL_StartTextInput();
}

void handleStatsEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                      State &state, std::mt19937 &rng) {
	if (e.type == SDL_KEYDOWN) {
		auto k = e.key.keysym.sym;
		if (k == SDLK_r)
			rollStats(state.party[state.activeSlot], rng);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER ||
		         k == SDLK_k) {
			// KEEP -> proceed to name entry. The slot only flips to
			// `filled` once a name is committed and CREATE.SAV is written.
			enterNameStep(state);
		} else if (k == SDLK_ESCAPE) {
			// Back to alignment.
			state.step = Step::PickAlignment;
		}
	} else if (e.type == SDL_MOUSEBUTTONDOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		int btn = statsBtnAt(lx, ly);
		switch (btn) {
		case BTN_REROLL:
			rollStats(state.party[state.activeSlot], rng);
			break;
		case BTN_MODIFY:
			state.statCursor = 0;
			state.step = Step::ModifyStats;
			break;
		case BTN_FACES:
			state.portraitCursor = state.party[state.activeSlot].portrait;
			state.step = Step::PickPortrait;
			break;
		case BTN_KEEP:
			enterNameStep(state);
			break;
		default: break;
		}
	}
}

void handleNameEvent(const SDL_Event &e, State &state,
                     const std::filesystem::path &chargenDir) {
	auto &c = state.party[state.activeSlot];
	if (e.type == SDL_TEXTINPUT) {
		// Accept any byte SDL feeds us -- UTF-8 multi-byte sequences and
		// emoji included. Cap by name buffer size (10 bytes + NUL); a
		// multi-byte char may not fit, in which case we skip the whole
		// SDL_TEXTINPUT to avoid splitting a code point mid-sequence.
		size_t curLen = std::strlen(c.name);
		size_t inLen  = std::strlen(e.text.text);
		if (curLen + inLen <= 10) {
			std::memcpy(c.name + curLen, e.text.text, inLen);
			c.name[curLen + inLen] = '\0';
		}
	} else if (e.type == SDL_KEYDOWN) {
		auto k = e.key.keysym.sym;
		if (k == SDLK_BACKSPACE) {
			// Drop the last UTF-8 code point (trail bytes start with 10xxxxxx).
			size_t len = std::strlen(c.name);
			while (len > 0) {
				unsigned char b = static_cast<unsigned char>(c.name[len - 1]);
				c.name[--len] = '\0';
				if ((b & 0xC0) != 0x80) break; // stop on lead/single byte
			}
		} else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
			// Name cannot be empty: ignore Enter until the user types
			// something. (Esc backs out instead.)
			if (std::strlen(c.name) == 0)
				return;
			SDL_StopTextInput();
			std::cout << "  [chargen: slot " << state.activeSlot
			          << " name=\"" << c.name << "\" -- writing CREATE.SAV]"
			          << std::endl;
			// Only flip `filled` after CREATE.SAV is on disk. Otherwise an
			// I/O failure leaves the UI showing a completed slot while the
			// xfer reads stale bytes -- silently mis-rolling the party.
			if (writeCreateSav(chargenDir, state)) {
				c.filled = true;
				state.step       = Step::EntryScreen;
				state.activeSlot = -1;
			} else {
				std::cout << "  [chargen: keeping slot " << state.activeSlot
				          << " open -- save failed, character not finalised]"
				          << std::endl;
			}
		} else if (k == SDLK_ESCAPE) {
			SDL_StopTextInput();
			state.step = Step::ShowStats; // back to stats
		}
	}
}

void handlePortraitPickEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                             State &state,
                             const std::vector<uint8_t> &picBytes) {
	int total = static_cast<int>(GRAPHICS::Bitmap(const_cast<std::vector<uint8_t> &>(picBytes)).getNumberOfBitmaps());
	if (total <= 0) total = 1;
	auto commit = [&]() {
		state.party[state.activeSlot].portrait = state.portraitCursor;
		std::cout << "  [chargen: slot " << state.activeSlot
		          << " portrait set to " << state.portraitCursor << "]"
		          << std::endl;
		state.step = Step::ShowStats;
	};
	auto scrollPrev = [&]() {
		state.portraitCursor = (state.portraitCursor + total - 1) % total;
	};
	auto scrollNext = [&]() {
		state.portraitCursor = (state.portraitCursor + 1) % total;
	};
	if (e.type == SDL_KEYDOWN) {
		auto k = e.key.keysym.sym;
		if (k == SDLK_LEFT || k == SDLK_UP) scrollPrev();
		else if (k == SDLK_RIGHT || k == SDLK_DOWN) scrollNext();
		else if (k == SDLK_PAGEDOWN)
			state.portraitCursor = std::min(total - 1,
			                                state.portraitCursor + kPortraitVisible);
		else if (k == SDLK_PAGEUP)
			state.portraitCursor = std::max(0,
			                                state.portraitCursor - kPortraitVisible);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) commit();
		else if (k == SDLK_ESCAPE) state.step = Step::ShowStats;
	} else if (e.type == SDL_MOUSEBUTTONDOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		if (inArrow(lx, ly, kArrowYTop)) scrollPrev();
		else if (inArrow(lx, ly, kArrowYBot)) scrollNext();
		else {
			int cell = portraitCellAt(lx, ly);
			if (cell >= 0) {
				// Clicking any visible portrait sets the cursor to it and
				// commits in one step.
				state.portraitCursor = (state.portraitCursor + cell) % total;
				commit();
			}
		}
	}
}

void handleModifyEvent(const SDL_Event &e, GRAPHICS::Graphics &gfx,
                       State &state) {
	auto &c = state.party[state.activeSlot];
	auto bump = [&](int delta) {
		int &v = c.abil[state.statCursor];
		v = std::max(3, std::min(18, v + delta));
	};
	if (e.type == SDL_KEYDOWN) {
		auto k = e.key.keysym.sym;
		if (k == SDLK_UP)
			state.statCursor = (state.statCursor + NUM_ABIL - 1) % NUM_ABIL;
		else if (k == SDLK_DOWN)
			state.statCursor = (state.statCursor + 1) % NUM_ABIL;
		else if (k == SDLK_RIGHT || k == SDLK_PLUS || k == SDLK_EQUALS)
			bump(+1);
		else if (k == SDLK_LEFT || k == SDLK_MINUS)
			bump(-1);
		else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_ESCAPE)
			state.step = Step::ShowStats;
	} else if (e.type == SDL_MOUSEBUTTONDOWN &&
	           e.button.button == SDL_BUTTON_LEFT) {
		int lx, ly;
		mousePos(gfx, e, lx, ly);
		int btn = modBtnAt(lx, ly);
		if (btn == BTN_PLUS)  bump(+1);
		else if (btn == BTN_MINUS) bump(-1);
		else if (btn == BTN_OK)    state.step = Step::ShowStats;
		else {
			// Click on a stat row picks that stat as cursor.
			// Rows at y = 108..108+8*5; left column starts at x=144.
			if (lx >= 144 && lx < 232 && ly >= 108 && ly < 108 + 8 * NUM_ABIL) {
				int row = (ly - 108) / 8;
				if (row >= 0 && row < NUM_ABIL) state.statCursor = row;
			}
		}
	}
}

} // namespace

bool THIRDEYE::chargen::runChargenScreen(GRAPHICS::Graphics &gfx,
                                         const std::filesystem::path &chargenDir) {
	// Required chargen assets sit beside the game's .RES (e.g. ../data/CHARGEN/).
	// PALETTE.COL: 256-colour 6-bit VGA palette (768 bytes, no header).
	// CHARPICS.BMP: 90 portrait sheet (see graphics/bitmap.cpp).
	// FONT8.FNT: 8x8 bit-packed font with lowercase glyphs (FONT6 is
	//            uppercase-only). The race-picker uses uppercase, but
	//            FONT8 handles both -- one font for the whole flow.
	auto palBytes = readFile(chargenDir / "PALETTE.COL");
	auto picBytes = readFile(chargenDir / "CHARPICS.BMP");
	auto fntBytes = readFile(chargenDir / "FONT8.FNT");  // body text (mixed case)
	auto fnt6Bytes = readFile(chargenDir / "FONT6.FNT"); // menu list (uppercase, tighter)
	if (palBytes.empty() || picBytes.empty()) {
		std::cout << "  [chargen: missing assets in " << chargenDir
		          << " (need PALETTE.COL + CHARPICS.BMP); skipping screen]"
		          << std::endl;
		// Caller proceeds with whatever CREATE.SAV is on disk (accept).
		return true;
	}

	gfx.loadPalette(palBytes, /*isRes=*/false);
	GRAPHICS::Bitmap portraits(picBytes);
	// `picBytes.empty()` (above) doesn't catch a malformed-but-nonzero
	// CHARPICS.BMP -- the parser may decline every header path and leave
	// `numberOfBitmaps == 0`. The portrait picker later does
	// `(strip0 + i) % total`, which divides by zero if we don't bail here.
	if (portraits.getNumberOfBitmaps() == 0) {
		std::cout << "  [chargen: CHARPICS.BMP at " << chargenDir
		          << " parsed to 0 portraits (truncated / unknown format);"
		          << " skipping screen]" << std::endl;
		return true;
	}
	GRAPHICS::Cps backdrop = GRAPHICS::loadCps(chargenDir / "CHARGEN.CPS");
	// CHARGENB.CPS holds the chargen UI sprites (blank blue/red buttons,
	// PLAY/DELETE composites, stone separators, sparkles). We grab the
	// blank blue button at (128,128,32,14) and overlay text to compose
	// BACK / KEEP / REROLL / etc.
	GRAPHICS::Cps chargenb = GRAPHICS::loadCps(chargenDir / "CHARGENB.CPS");

	// Sample portraits shown in empty slots until the per-slot flow finishes.
	// (Once a real character is rolled, we'll use its chosen portrait instead.)
	std::vector<uint8_t> previewIdx = { 1, 2, 3, 4 };

	State state;
	std::mt19937 rng{std::random_device{}()};
	// Headless verification: skip straight into a sub-step to snapshot it.
	if (const char *s = std::getenv("THIRDEYE_CHARGEN_STEP")) {
		std::string step = s;
		if (step == "race") {
			state.step = Step::PickRace; state.activeSlot = 0;
		} else if (step == "class") {
			state.step = Step::PickClass; state.activeSlot = 0;
		} else if (step == "alignment") {
			state.step = Step::PickAlignment; state.activeSlot = 0;
		} else if (step == "stats") {
			state.activeSlot = 0;
			state.party[0].race  = 0;   // HUMAN MALE
			state.party[0].klass = 0;   // FIGHTER
			state.party[0].alignment = 0;
			rollStats(state.party[0], rng);
			state.step = Step::ShowStats;
		} else if (step == "modify") {
			state.activeSlot = 0;
			state.party[0].race  = 0;
			state.party[0].klass = 0;
			state.party[0].alignment = 0;
			rollStats(state.party[0], rng);
			state.step = Step::ModifyStats;
		} else if (step == "portrait" || step == "faces") {
			state.activeSlot = 0;
			state.party[0].race  = 0;
			state.party[0].klass = 0;
			state.party[0].alignment = 0;
			state.step = Step::PickPortrait;
		} else if (step == "name") {
			state.activeSlot = 0;
			state.party[0].race  = 0;
			state.party[0].klass = 0;
			state.party[0].alignment = 0;
			rollStats(state.party[0], rng);
			enterNameStep(state);
		} else if (step == "write") {
			// Headless: roll char + write CREATE.SAV in one shot. `done` only
			// flips on a successful save so a failing chargen-test exits with
			// the loop still running and the operator sees the [chargen: ...
			// failed] log.
			for (int pc = 0; pc < 4; ++pc) {
				state.party[pc].race  = 0;
				state.party[pc].klass = 0;
				state.party[pc].alignment = 0;
				rollStats(state.party[pc], rng);
				std::snprintf(state.party[pc].name,
				              sizeof(state.party[pc].name),
				              "Foo%d", pc + 1);
				state.party[pc].filled = true;
			}
			state.done = writeCreateSav(chargenDir, state);
		}
	}
	render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb, previewIdx, state);
	gfx.update();
	std::cout << "  [chargen: entry screen up -- click a slot to pick a "
	             "race, P/Enter to play, Esc to cancel]" << std::endl;
	if (const char *p = std::getenv("THIRDEYE_DUMP_CHARGEN"))
		gfx.saveScreenshot(p);

	// Event loop. Re-render after any input that mutates state. Cancel and
	// accept both currently fall through to whichever CREATE.SAV is on disk;
	// a proper accept path will write the rolled party out.
	SDL_Event event;
	while (!state.done) {
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_QUIT)
				throw THIRDEYE::runtime::QuitRequested{};
			Step before = state.step;
			int beforeRace = state.raceCursor;
			int beforeClass = state.classCursor;
			int beforeAlign = state.alignmentCursor;
			int beforeStat = state.statCursor;
			int beforePortrait = state.portraitCursor;
			int beforeSlot = state.activeSlot;
			// Snapshot the active char's name length so typed chars / backspace
			// drive a redraw.
			size_t beforeNameLen = (state.activeSlot >= 0)
				? std::strlen(state.party[state.activeSlot].name) : 0;
			int beforeStr = state.party[std::max(0, state.activeSlot)].abil[STR];
			// Snapshot the whole ability vector so MODIFY +/- triggers a redraw
			// even when only one ability changes (and especially when the
			// "STR" element happens to be the same after a +1/-1 round-trip).
			int beforeAbilSum = 0;
			for (int i = 0; i < NUM_ABIL; ++i)
				beforeAbilSum += state.party[std::max(0, state.activeSlot)].abil[i];
			switch (state.step) {
			case Step::EntryScreen:   handleEntryEvent(event, gfx, state); break;
			case Step::PickRace:      handleRacePickEvent(event, gfx, state); break;
			case Step::PickClass:     handleClassPickEvent(event, gfx, state); break;
			case Step::PickAlignment: handleAlignmentPickEvent(event, gfx, state, rng); break;
			case Step::ShowStats:     handleStatsEvent(event, gfx, state, rng); break;
			case Step::ModifyStats:   handleModifyEvent(event, gfx, state); break;
			case Step::PickPortrait:  handlePortraitPickEvent(event, gfx, state, picBytes); break;
			case Step::EnterName:     handleNameEvent(event, state, chargenDir); break;
			}
			int afterStr = state.party[std::max(0, state.activeSlot)].abil[STR];
			int afterAbilSum = 0;
			for (int i = 0; i < NUM_ABIL; ++i)
				afterAbilSum += state.party[std::max(0, state.activeSlot)].abil[i];
			size_t afterNameLen = (state.activeSlot >= 0)
				? std::strlen(state.party[state.activeSlot].name) : 0;
			if (state.step != before || state.raceCursor != beforeRace ||
			    state.classCursor != beforeClass ||
			    state.alignmentCursor != beforeAlign ||
			    state.statCursor != beforeStat ||
			    state.portraitCursor != beforePortrait ||
			    state.activeSlot != beforeSlot ||
			    afterStr != beforeStr ||
			    afterAbilSum != beforeAbilSum ||
			    afterNameLen != beforeNameLen)
				render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb, previewIdx, state);
		}
		// Sparkle animation. Advance the strip every ~150 ms (every 9 pumps
		// at 16 ms each) so the stars twinkle visibly without strobing.
		// Only re-render when we're in a sub-step (the entry screen has no
		// active slot, so nothing to animate).
		static int pumpTick = 0;
		++pumpTick;
		if (state.activeSlot >= 0 && pumpTick % 9 == 0) {
			++state.sparkleFrame;
			render(gfx, picBytes, fntBytes, fnt6Bytes, backdrop, chargenb,
			       previewIdx, state);
		}
		gfx.update();
		SDL_Delay(16);
	}
	return !state.cancelled;
}
