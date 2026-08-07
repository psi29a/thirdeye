#include "internal.hpp"

#include <cstdint>
#include <cstdlib>
#include <vector>

// MAZE.EXE -- Dungeon Hack's random dungeon generator -- ported to native
// code. HACK.BAT runs the original between phase-one (title menu) and
// phase-two (the game); it reads savegame/SETTINGS.DAT and writes the dungeon
// that phase-two then plays. dh.cpp::ensureSavegameFiles() is the caller;
// this file is the geometry.
//
// Provenance for everything here is ../../../../dh_research/MAZE/ (Ghidra
// decompile + r2 disassembly of the shipped 16-bit binary), read up in
// docs/dungeon_hack_maze.md and dh_research/MAZE/ROOMGEN.md. Functions are
// quoted by their Ghidra `segment:offset`; the application segment is 1325
// and the private RNG lives in segment 1766.
//
// Split out of dh.cpp so the unit tests can link the generator without
// dragging in the whole DH runtime (which needs the engine's globals).
//
// What is NOT here: MAZE's feature-placement tail (15 passes driven by the
// FREQ_* settings that scatter treasure, traps, pits, monsters and the real
// stairs). dh.cpp still synthesises those. Because the down-stairs pass
// (1325:10a6) is part of that tail, we choose the stairs ourselves and feed
// them forward as the next level's entry -- MAZE's chaining, our placement.

namespace THIRDEYE::runtime::dh {

// ---------------------------------------------------------------- RNG ----

// MAZE.EXE's random number generator: R250 (Kirkpatrick-Stoll lagged-Fibonacci
// XOR, lags 250/103), ported byte-for-byte from segment 1766 --
// 1766:0005 = srand, 1766:006e = rand, 1766:00c1 = random(lo,hi).
//
// This has to be exact. Every layout decision MAZE makes is a draw from this
// stream, so a different PRNG means a different dungeon even with identical
// code around it. R250's state is 250 u16 words seeded by a Lehmer LCG, with
// 16 words forced to a staircase bit pattern to guarantee linear independence.
R250::R250(uint32_t seed) {
	for (uint16_t &w : mState) {
		w = static_cast<uint16_t>(seed >> 16);
		seed = seed * 0x015a4e35u + 1u;
	}
	// 1766:0005 tail -- `i = 3; do {...; i += 11} while (i != 0xb3)`, so
	// exactly 16 words (3, 14, ... 168) get bit 15-j set and bits above it
	// cleared. The bound is `!= 179`, not `< 250`: running past it would
	// shift the mask to zero and wipe the state.
	uint16_t mask = 0xFFFF, msb = 0x8000;
	for (int i = 3; i != 0xb3; i += 0x0b) {
		mState[i] = static_cast<uint16_t>((mState[i] & mask) | msb);
		mask = static_cast<uint16_t>(mask >> 1);
		msb = static_cast<uint16_t>(msb >> 1);
	}
}

uint16_t R250::next() {
	const int j = (mIndex < 147) ? mIndex + 103 : mIndex - 147;
	const uint16_t v = static_cast<uint16_t>(mState[mIndex] ^ mState[j]);
	mState[mIndex] = v;
	mIndex = (mIndex < 249) ? mIndex + 1 : 0;
	return v;
}

int R250::range(int lo, int hi) {
	return lo + next() % static_cast<unsigned>(hi - lo + 1);
}

// 1766:00dd -- roll `n` dice of `sides` and add `bonus`.
int R250::roll(int n, int sides, int bonus) {
	for (int i = 0; i < n; ++i) bonus += range(1, sides);
	return bonus;
}

namespace {

// ------------------------------------------------------------- tiles ----

// MAZE's working grid is a 32x32 CP437 *character* map -- SEED.TXT dumps it
// verbatim as text, which is how these were identified. The write pass
// (1325:4017) collapses it: 0xDB stays a wall (with one of two random texture
// bytes), everything else becomes open floor. So doors and stair markers are
// holes in the rock as far as LEVELS.DAT is concerned; the game learns about
// them from the FEA records instead.
constexpr uint8_t kRock  = 0xDB;   // U+2588 full block -- undecided rock
constexpr uint8_t kFloor = 0xFF;   //                      open floor
constexpr uint8_t kDoorV = 0xB3;   // U+2502 -- door across an east-west passage
constexpr uint8_t kDoorH = 0xC4;   // U+2500 -- door across a north-south passage
constexpr uint8_t kUpStair = 0x18; // U+2191 -- the arrival marker

constexpr int kW = 32;

// 1325:05a0's step table at DGROUP:0x258 -- two columns east, two rows north,
// two columns west, two rows south. Halving a step lands on the wall cell
// between two rooms.
constexpr int kStep[4] = { +2, -kW * 2, -2, +kW * 2 };
// The unit-step table that follows it at DGROUP:0x260, in N/E/S/W order.
constexpr int kDRow[4] = { -1, 0, +1, 0 };
constexpr int kDCol[4] = { 0, +1, 0, -1 };

struct Rect { int row, col, h, w; };   // 1325:0678's layout, in that order

// Everything the generator threads through its helpers. In MAZE these are
// DGROUP globals (grid at [0x4ba0], used map at 0x479c, room list at 0x0b00,
// room count at 0x0ba2, door counter in descriptor word [11]).
struct Level {
	uint8_t *grid;
	std::vector<uint8_t> used = std::vector<uint8_t>(kW * kW, 0);
	Rect rooms[20] = {};
	int roomCount = 0;
	int doors = 0;
};

// 1325:0176 -- out of bounds counts as "not rock".
bool isRock(const Level &lv, int row, int col) {
	if (row < 0 || col < 0 || row >= kW || col >= kW) return false;
	return lv.grid[row * kW + col] == kRock;
}

// 1325:0242 -- 4-neighbour rock mask, bit0=N bit1=E bit2=S bit3=W.
uint8_t rockMask4(const Level &lv, int row, int col) {
	uint8_t m = 0;
	for (int i = 0; i < 4; ++i)
		if (isRock(lv, row + kDRow[i], col + kDCol[i]))
			m = static_cast<uint8_t>(m | (1u << i));
	return m;
}

// 1325:028d -- 8-neighbour rock mask; 0xFF means "walled in on all sides".
bool rockAll8(const Level &lv, int row, int col) {
	for (int dr = -1; dr <= 1; ++dr)
		for (int dc = -1; dc <= 1; ++dc)
			if ((dr || dc) && !isRock(lv, row + dr, col + dc))
				return false;
	return true;
}

// 1325:02d8 -- is this cell unclaimed, and floor (or rock) as asked?
bool cellOk(const Level &lv, int row, int col, bool wantFloor) {
	if (row < 0 || col < 0 || row >= kW || col >= kW) return false;
	if (lv.used[row * kW + col]) return false;
	const bool floor = lv.grid[row * kW + col] == kFloor;
	return wantFloor ? floor : !floor;
}

// 1325:033f -- re-roll until a cell qualifies. Unbounded in the original.
void pickCell(Level &lv, R250 &rng, int &row, int &col, bool wantFloor) {
	do {
		row = rng.range(0, 31);
		col = rng.range(0, 31);
	} while (!cellOk(lv, row, col, wantFloor));
}

// 1325:073c -- door spacing. 1 = no orthogonal neighbour already claimed,
// 2 = no diagonal one either. (The original takes col before row.)
bool spacingOk(const Level &lv, int row, int col, int spacing) {
	if (spacing == 0) return true;
	auto claimed = [&](int r, int c) {
		return r >= 0 && c >= 0 && r < kW && c < kW && lv.used[r * kW + c];
	};
	if (claimed(row + 1, col) || claimed(row - 1, col) ||
	    claimed(row, col - 1) || claimed(row, col + 1)) return false;
	if (spacing == 2 &&
	    (claimed(row - 1, col - 1) || claimed(row - 1, col + 1) ||
	     claimed(row + 1, col - 1) || claimed(row + 1, col + 1))) return false;
	return true;
}

// 1325:07ce -- the single choke point every door in the game goes through.
// A door only fits where the rock forms a clean passage: rock north+south
// (mask 5) means an east-west passage, rock east+west (mask 10) a
// north-south one. Anything else -- a corner, a junction, open ground --
// is refused.
bool placeDoor(Level &lv, int row, int col, bool bounds, int spacing,
               uint8_t tileV, uint8_t tileH) {
	if (bounds) {
		if (row <= 0 || col <= 0 || row >= 31 || col >= 31) return false;
		if (!cellOk(lv, row, col, /*wantFloor=*/true)) return false;
	}
	const uint8_t m = rockMask4(lv, row, col);
	if (m != 5 && m != 10) return false;
	if (!spacingOk(lv, row, col, spacing)) return false;
	lv.used[row * kW + col] = 1;
	lv.grid[row * kW + col] = (m == 5) ? tileV : tileH;
	++lv.doors;
	return true;
}

// 1325:087e -- scatter `count` doors anywhere they will fit, at spacing 2.
void scatterDoors(Level &lv, R250 &rng, int count) {
	for (int tries = 0x800; count > 0 && tries > 0; --tries) {
		int row, col;
		pickCell(lv, rng, row, col, /*wantFloor=*/true);
		if (placeDoor(lv, row, col, /*bounds=*/false, /*spacing=*/2,
		              kDoorV, kDoorH))
			--count;
	}
}

// 1325:107a -- top the level up toward (but rarely to) 40 doors.
void topUpDoors(Level &lv, R250 &rng) {
	const int room = 40 - lv.doors;
	if (room > 0) scatterDoors(lv, rng, rng.range(0, room - 1));
}

// 1325:0678 -- fill a rectangle. No clipping in the original; the callers'
// ranges keep it inside the grid.
void fillRect(Level &lv, const Rect &r, uint8_t fill) {
	for (int y = r.row; y < r.row + r.h; ++y)
		for (int x = r.col; x < r.col + r.w; ++x)
			lv.grid[y * kW + x] = fill;
}

// 1325:0412 -- strict AABB overlap test (touching edges count as disjoint).
bool disjoint(const Rect &a, const Rect &b) {
	return !(b.row + b.h > a.row && b.col + b.w > a.col &&
	         a.row + a.h > b.row && a.col + a.w > b.col);
}

// 1325:0473 -- ...against every room placed so far, rooms[0] included. That
// slot is the 3x3 keep-out box around the entry: never filled, it exists
// only so nothing can be dropped on top of the arrival point.
bool fits(const Level &lv, const Rect &t) {
	for (int i = 0; i < lv.roomCount; ++i)
		if (!disjoint(t, lv.rooms[i])) return false;
	return true;
}

// ----------------------------------------------------------- carving ----

// 1325:04ab -- initialise. Everything is rock except row 31 and column 31,
// which get floor. That is not a border: the carver only steps into rock, so
// pre-marking the last row/column stops it walking off the buffer without a
// single bounds check. 1325:056a puts them back afterwards.
void initGrid(Level &lv) {
	for (int y = 0; y < kW; ++y)
		for (int x = 0; x < kW; ++x)
			lv.grid[y * kW + x] = (y == 31 || x == 31) ? kFloor : kRock;
}

// 1325:05a0 -- a stackless recursive backtracker over the odd coordinates
// (1,1)..(29,29), i.e. a 15x15 cell maze. The trick that removes the stack:
// while the walk is inside a cell, that cell holds *the direction it was
// entered from* (0..3, or 4 at the root); it is overwritten with floor on the
// way back out.
void backtrack(Level &lv, R250 &rng, int row, int col) {
	int pos = row * kW + col;
	lv.grid[pos] = 4;                       // root marker: "no parent"
	int d = rng.range(0, 3), d0 = d;
	for (;;) {
		int np;
		while ((np = pos + kStep[d]) >= 0 && lv.grid[np] == kRock) {
			lv.grid[np] = static_cast<uint8_t>(d);
			lv.grid[pos + kStep[d] / 2] = kFloor;  // knock out the wall between
			pos = np;
			d = rng.range(0, 3);
			d0 = d;
		}
		d = (d < 3) ? d + 1 : 0;
		if (d != d0) continue;              // try the next direction
		const uint8_t from = lv.grid[pos];  // all four blocked -- back out
		lv.grid[pos] = kFloor;
		if (from > 3) break;                // reached the root
		pos -= kStep[from];
		d = rng.range(0, 3);
		d0 = d;
	}
}

// 1325:06d1 -- any odd cell still sealed on all eight sides is an unreachable
// pocket; run the backtracker again from there. Column-major, matching the
// original's pointer walk (the inner loop strides two rows).
void fillPockets(Level &lv, R250 &rng) {
	for (int col = 1; col < kW; col += 2)
		for (int row = 1; row < kW; row += 2)
			if (lv.grid[row * kW + col] == kRock && rockAll8(lv, row, col))
				backtrack(lv, rng, row, col);
}

// 1325:056a -- put the walk-off sentinel row/column back to rock.
void sealBorder(Level &lv) {
	for (int i = 0; i < kW; ++i) {
		lv.grid[31 * kW + i] = kRock;
		lv.grid[i * kW + 31] = kRock;
	}
}

// 1325:0c1d -- walk the entry until it lands on a dead end (exactly one open
// orthogonal neighbour), mark it with the up-stairs glyph, and hand back the
// neighbour as the party's arrival cell plus the direction pointing at it.
//
// The party does NOT stand on the marker: it stands on the one open cell
// beside it, facing away down the corridor. Note `open` is the complement of
// the rock mask, so out-of-bounds neighbours read as open -- harmless,
// because the entry is always at an odd coordinate in 1..29.
//
// After 200 failures the original gives up and leaves the entry wherever the
// last re-roll put it, writing no marker. We report that so the caller can
// still produce a valid arrival cell.
bool placeEntry(Level &lv, R250 &rng, int &genRow, int &genCol,
                int &outRow, int &outCol, int &fdir) {
	for (int tries = 0; tries < 200; ++tries) {
		const uint8_t open = static_cast<uint8_t>(rockMask4(lv, genRow, genCol) ^ 0x0F);
		if (open == 1 || open == 2 || open == 4 || open == 8) {
			lv.grid[genRow * kW + genCol] = kUpStair;
			lv.used[genRow * kW + genCol] = 1;
			// 1325:0bb1 -- step one cell along the single set bit.
			fdir = (open == 1) ? 0 : (open == 2) ? 1 : (open == 4) ? 2 : 3;
			outRow = genRow + kDRow[fdir];
			outCol = genCol + kDCol[fdir];
			return true;
		}
		pickCell(lv, rng, genRow, genCol, /*wantFloor=*/true);
	}
	outRow = genRow;
	outCol = genCol;
	fdir = 0;
	return false;
}

// ------------------------------------------------------------- rooms ----

// 1325:0cdc -- punch one or two doorways through a room's wall ring. Rooms
// sit at odd coordinates with odd extents, so every candidate here is an even
// coordinate: exactly the rock the backtracker leaves between corridors.
//
// ponytail: the original never decrements on failure, so it retries until it
// succeeds and can in principle spin forever (ROOMGEN.md §7.3). We cap the
// attempts -- a hang in a dungeon generator is not a faithfulness we want.
void punchDoors(Level &lv, R250 &rng, const Rect &r) {
	int n = (r.h * r.w >= 25 && rng.range(0, 9) < 3) ? 2 : 1;
	for (int tries = 0x400; n > 0 && tries > 0; --tries) {
		int row = 0, col = 0;
		switch (rng.range(0, 3)) {
		case 0: row = r.row - 1;                    col = r.col + rng.range(0, r.w - 1); break;
		case 1: row = r.row + rng.range(0, r.h - 1); col = r.col + r.w;                  break;
		case 2: row = r.row + r.h;                  col = r.col + rng.range(0, r.w - 1); break;
		default: row = r.row + rng.range(0, r.h - 1); col = r.col - 1;                   break;
		}
		if (row < 0 || col < 0 || row >= kW || col >= kW) continue;
		uint8_t &cell = lv.grid[row * kW + col];
		if (cell != kRock) continue;
		cell = kFloor;   // placeDoor's cellOk() demands the cell read floor
		if (placeDoor(lv, row, col, /*bounds=*/true, /*spacing=*/1,
		              kDoorV, kDoorH))
			--n;
		else
			cell = kRock;
	}
}

// 1325:08cb -- ring every room with doors. If a wall cell refuses and it is
// already open floor, step one further out: that is how a room swallowed by
// an existing corridor still gets a door on the next wall out.
//
// The loop starts at rooms[2] in the original, but 1325:0aac's first real
// room is rooms[1] -- so the first accepted room on a zone-2 level never gets
// perimeter doors. Almost certainly an off-by-one, reproduced deliberately:
// "fixing" it would desync us from MAZE's output.
void ringDoors(Level &lv) {
	auto tryDoor = [&](int row, int col) {
		return placeDoor(lv, row, col, /*bounds=*/true, /*spacing=*/1,
		                 kDoorV, kDoorH);
	};
	auto isFloor = [&](int row, int col) {
		return row >= 0 && col >= 0 && row < kW && col < kW &&
		       lv.grid[row * kW + col] == kFloor;
	};
	for (int i = 2; i < lv.roomCount; ++i) {
		const Rect &r = lv.rooms[i];
		for (int x = r.col - 1; x <= r.col + r.w; ++x) {
			if (!tryDoor(r.row - 1, x) && isFloor(r.row - 1, x))
				tryDoor(r.row - 2, x);
			if (!tryDoor(r.row + r.h, x) && isFloor(r.row + r.h, x))
				tryDoor(r.row + r.h + 1, x);
		}
		for (int y = r.row - 1; y <= r.row + r.h; ++y) {
			if (!tryDoor(y, r.col - 1) && isFloor(y, r.col - 1))
				tryDoor(y, r.col - 2);
			if (!tryDoor(y, r.col + r.w) && isFloor(y, r.col + r.w))
				tryDoor(y, r.col + r.w + 1);
		}
	}
}

// 1325:0aac -- zone 2's extra pass: stamp rooms into a maze that has already
// been carved. Deliberately unlike the 0e3c room loop -- dimensions run 1..5
// (1-wide "rooms" are possible), nothing is odd-aligned, and the budget is
// about five times smaller.
void extraRooms(Level &lv, R250 &rng, int genRow, int genCol) {
	lv.rooms[0] = Rect{ genRow - 1, genCol - 1, 3, 3 };
	lv.roomCount = 1;
	int budget = rng.roll(5, 15, -5);
	for (int tries = 0x400; budget > 0 && lv.roomCount < 20 && tries > 0; --tries) {
		Rect t;
		t.h = rng.range(1, 5);
		t.w = rng.range(1, 5);
		t.row = rng.range(2, 30 - t.h);
		t.col = rng.range(2, 30 - t.w);
		if (!fits(lv, t)) continue;
		lv.rooms[lv.roomCount++] = t;
		budget -= t.h * t.w;
		fillRect(lv, t, kFloor);
	}
	ringDoors(lv);
}

// 1325:0e3c -- the room-and-corridor generator behind zones 1, 3 and 4.
// Rooms first, then the backtracker fills whatever rock is left, so the maze
// grows around the rooms rather than the other way round.
//
//   mode 0 (zone 1): rooms only
//   mode 1 (zone 3): two crossing corridors, then rooms
//   mode 2 (zone 4): two crossing corridors, no rooms -- and because the door
//                    loop starts at rooms[3] with only 3 entries, the deepest
//                    level ends up with no doors at all
void genRooms(Level &lv, R250 &rng, int mode, int genRow, int genCol) {
	initGrid(lv);

	lv.rooms[0] = Rect{ genRow - 1, genCol - 1, 3, 3 };   // entry keep-out
	lv.roomCount = 1;

	if (mode != 0) {
		int c = rng.range(0, 4) * 2 + 11;          // one of 11,13,15,17,19
		if (genCol == c) c += 2;                   // never on the entry column
		lv.rooms[lv.roomCount++] = Rect{ 1, c, 29, 1 };
		fillRect(lv, lv.rooms[lv.roomCount - 1], kFloor);

		int r = rng.range(0, 4) * 2 + 11;
		if (genRow == r) r += 2;
		lv.rooms[lv.roomCount++] = Rect{ r, 1, 1, 29 };
		fillRect(lv, lv.rooms[lv.roomCount - 1], kFloor);
	}

	if (mode < 2) {
		// 60d5: 60..300, mean 180 cells of room to place.
		int budget = rng.roll(60, 5, 0);
		// ponytail: a rejected candidate costs no budget in the original, so
		// the loop is bounded only by luck. Cap the attempts.
		for (int tries = 0x1000; budget > 0 && lv.roomCount < 20 && tries > 0;
		     --tries) {
			Rect t;
			t.h = rng.range(1, 2) * 2 + 1;         // 3 or 5
			t.w = rng.range(1, 2) * 2 + 1;
			t.row = rng.range(1, 30 - t.h) | 1;    // odd-aligned
			t.col = rng.range(1, 30 - t.w) | 1;
			if (!fits(lv, t)) continue;
			lv.rooms[lv.roomCount++] = t;
			budget -= t.h * t.w;
			fillRect(lv, t, kFloor);
		}
	}

	backtrack(lv, rng, genRow, genCol);
	fillPockets(lv, rng);
	sealBorder(lv);
}

} // namespace

// -------------------------------------------------------- the dungeon ----

// 1325:3986 + 1325:375f + 1325:4053's finalize pass, run end to end.
//
// Two things make this portable at all. First, MAZE seeds each level
// independently -- 1325:375f opens with `srand(seed + level + 1)` -- so level
// N does not depend on every draw made before it. Second, the zone table is
// decided up front from one stream seeded with the raw seed, before any level
// is generated.
void generateDungeon(uint8_t *chunks, int levels, uint32_t seed, bool waterOn,
                     LevelInfo *info) {
	// --- 1325:3986: zone assignment ---------------------------------
	// Zone picks which of five layout algorithms a level gets. Levels 0-3 are
	// always zone 1, the deepest is zone 4, one random middle level is zone 0,
	// and the rest roll 1..3.
	{
		R250 rng(seed);
		for (int l = 0; l < levels; ++l) { info[l].zone = 1; info[l].water = false; }
		for (int l = 4; l < levels - 1; ++l)
			info[l].zone = static_cast<uint8_t>(rng.range(0, 2) + 1);
		if (levels > 0) info[levels - 1].zone = 4;
		if (waterOn && levels > 8)
			info[rng.range(7, levels - 2)].water = true;
		if (levels > 5) {
			int pick;
			do { pick = rng.range(4, levels - 2); } while (info[pick].water);
			info[pick].zone = 0;
		}
	}

	// --- 1325:375f, once per level ----------------------------------
	int genRow = 0, genCol = 0;
	for (int l = 0; l < levels; ++l) {
		R250 rng(seed + static_cast<uint32_t>(l) + 1u);
		Level lv;
		lv.grid = chunks + static_cast<size_t>(l) * 0x400;

		// 1325:04ab picks the generation entry: level 0 rolls it, deeper
		// levels inherit the level above's stairs-down cell, snapped odd.
		// (genRow/genCol carry that forward from the previous iteration.)
		if (l == 0) {
			genRow = rng.range(0, 14) * 2 + 1;
			genCol = rng.range(0, 14) * 2 + 1;
		} else {
			genRow |= 1;
			genCol |= 1;
		}

		switch (info[l].zone) {
		case 1: genRooms(lv, rng, 0, genRow, genCol); break;
		case 3: genRooms(lv, rng, 1, genRow, genCol); break;
		case 4: genRooms(lv, rng, 2, genRow, genCol); break;
		default:   // zones 0 and 2: a plain maze, zone 2 with rooms stamped on
			initGrid(lv);
			backtrack(lv, rng, genRow, genCol);
			sealBorder(lv);
			break;
		}

		if (info[l].zone == 0 || info[l].zone == 2) {
			placeEntry(lv, rng, genRow, genCol, info[l].entryRow,
			           info[l].entryCol, info[l].fdir);
			if (info[l].zone == 2) extraRooms(lv, rng, genRow, genCol);
		} else {
			placeEntry(lv, rng, genRow, genCol, info[l].entryRow,
			           info[l].entryCol, info[l].fdir);
			const int first = (info[l].zone == 1) ? 1 : 3;
			for (int i = first; i < lv.roomCount; ++i)
				punchDoors(lv, rng, lv.rooms[i]);
		}
		if (info[l].zone != 4) topUpDoors(lv, rng);

		// Stairs down. MAZE gets these from its feature tail (1325:10a6),
		// which isn't ported, so we pick the cell furthest from the arrival
		// point *through the maze* -- straight-line distance always lands in
		// the opposite corner, which would put every level's stairs (and so
		// every deeper level's entry) in one of two fixed spots.
		{
			std::vector<int> dist(0x400, -1);
			std::vector<int> queue{ info[l].entryRow * kW + info[l].entryCol };
			dist[queue.front()] = 0;
			for (size_t head = 0; head < queue.size(); ++head) {
				const int p = queue[head], py = p / kW, px = p % kW;
				for (int d = 0; d < 4; ++d) {
					const int ny = py + kDRow[d], nx = px + kDCol[d];
					if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) continue;
					const int q = ny * kW + nx;
					if (dist[q] >= 0 || lv.grid[q] == kRock) continue;
					dist[q] = dist[p] + 1;
					queue.push_back(q);
				}
			}
			int best = -1;
			info[l].stairRow = info[l].entryRow;
			info[l].stairCol = info[l].entryCol;
			for (int y = 1; y < 31; ++y)
				for (int x = 1; x < 31; ++x)
					if (dist[y * kW + x] > best) {
						best = dist[y * kW + x];
						info[l].stairRow = y;
						info[l].stairCol = x;
					}
		}
		genRow = info[l].stairRow;
		genCol = info[l].stairCol;

		// 1325:4017 -- collapse the character map to the on-disk encoding.
		// MAZE runs this at write time off the global stream rather than the
		// per-level one, so our wall *textures* differ from a real run even
		// where the geometry matches. Nothing but the renderer reads them.
		for (int i = 0; i < 0x400; ++i)
			lv.grid[i] = (lv.grid[i] == kRock)
			             ? static_cast<uint8_t>(rng.range(0, 1))
			             : kFloor;
	}
}

} // namespace THIRDEYE::runtime::dh
