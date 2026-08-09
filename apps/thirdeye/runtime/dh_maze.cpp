#include "internal.hpp"

#include <chrono>
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
constexpr uint8_t kDownStair = 0x19;    // U+2193 -- the way to the next level
constexpr uint8_t kSpikeTrap = 0x0F;    // U+263C
constexpr uint8_t kSpinner = 0x1D;      // U+2194
constexpr uint8_t kFloorPit = 0xB0;     // U+2591
constexpr uint8_t kIllusionary = 0xB1;  // U+2592 -- looks solid, walk through it
constexpr uint8_t kSolidWallDoor = 0xB2;// U+2593 -- looks solid, a button opens it
constexpr uint8_t kArchV = 0xBA;        // U+2551
constexpr uint8_t kArchH = 0xCD;        // U+2550
constexpr uint8_t kWindow = 0xCE;       // U+256C
constexpr uint8_t kPillar = 0xDD;       // U+258C
constexpr uint8_t kTeleporter = 0xEF;   // U+2229

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
// A connected component of open floor, bounded by doors. `depth` is how many
// doors lie between it and the party entry -- 1325:3004 uses it to guarantee a
// lock's key is never hidden behind that same lock.
struct Region { int depth = 0; int size = 0; };

struct Level {
	uint8_t *grid = nullptr;
	std::vector<uint8_t> used = std::vector<uint8_t>(kW * kW, 0);
	Rect rooms[20] = {};
	int roomCount = 0;
	int doors = 0;
	std::vector<Region> regions;
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
			// Claim the arrival cell as well. MAZE gets there eventually
			// (1325:1b5e marks it before the item passes), but by then a door
			// can already have been written on top of the player.
			lv.used[outRow * kW + outCol] = 1;
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

// ----------------------------------------------------- regions + records ----

// A door-like glyph: the region walk stops at these, and crossing one starts a
// new region one step "deeper" into the level. That depth is what decides how
// far in a key may be hidden (1325:3004).
bool isDoorGlyph(uint8_t t) {
	return t == kDoorV || t == kDoorH || t == kArchV || t == kArchH ||
	       t == kSolidWallDoor;
}

// 1325:01f0 -- "is this open floor?" Open floor carries its region id as a
// letter, 'A' + id, so the test is a range check. Cells that are carved but
// not yet labelled read 0xFF and fail it.
bool isFloorGlyph(uint8_t t) { return t >= 0x41 && t <= 0x7C; }

// 1325:1736 -- strip the region letters back to unlabelled floor.
void unlabel(Level &lv) {
	for (int i = 0; i < 0x400; ++i)
		if (isFloorGlyph(lv.grid[i])) lv.grid[i] = kFloor;
}

// 1325:13d8 -- label every reachable floor cell with its region id.
//
// ponytail: this is the one piece of the tail we did NOT transcribe
// instruction-for-instruction -- MAZE's walk is a hand-rolled frontier array
// we could not fully trace (dh_research/MAZE/FEATURES.md §2 marks it PARTIAL).
// What is reproduced is its *shape*, which is what every later pass actually
// consumes: connected components of open floor, split at doors, each carrying
// the number of doors between it and the party entry. Region ids therefore
// won't match a real MAZE run cell-for-cell even though everything built on
// top of them behaves the same way.
void labelRegions(Level &lv, int entryRow, int entryCol) {
	unlabel(lv);
	lv.regions.clear();

	// Seed from the party's arrival cell so depths are measured from where the
	// player actually starts. If something has been built on top of it, fall
	// back to any floor at all rather than giving up -- a level with no
	// regions has no keys, no stairs and no monsters.
	int seedCell = entryRow * kW + entryCol;
	if (lv.grid[seedCell] != kFloor) {
		seedCell = -1;
		for (int i = 0; i < 0x400 && seedCell < 0; ++i)
			if (lv.grid[i] == kFloor) seedCell = i;
		if (seedCell < 0) return;
	}

	// Breadth-first over regions, so a region's depth is the smallest number
	// of doors between it and the entry.
	std::vector<std::pair<int, int>> pending{ { seedCell, 0 } };
	while (!pending.empty()) {
		std::vector<std::pair<int, int>> next;
		for (const auto &[seed, depth] : pending) {
			if (lv.grid[seed] != kFloor) continue;   // already claimed
			if (lv.regions.size() > 60) return;      // 'A'+id must stay <= '|'
			const int id = static_cast<int>(lv.regions.size());
			lv.regions.push_back(Region{ depth, 0 });
			// Flood this region, queueing whatever lies past each door.
			std::vector<int> stack{ seed };
			lv.grid[seed] = static_cast<uint8_t>(0x41 + id);
			while (!stack.empty()) {
				const int p = stack.back(); stack.pop_back();
				++lv.regions[id].size;
				const int py = p / kW, px = p % kW;
				for (int d = 0; d < 4; ++d) {
					const int ny = py + kDRow[d], nx = px + kDCol[d];
					if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) continue;
					const int q = ny * kW + nx;
					if (lv.grid[q] == kFloor) {
						lv.grid[q] = static_cast<uint8_t>(0x41 + id);
						stack.push_back(q);
					} else if (isDoorGlyph(lv.grid[q])) {
						// Step through the door; the far side is deeper.
						const int fy = ny + kDRow[d], fx = nx + kDCol[d];
						if (fy < 0 || fx < 0 || fy >= kW || fx >= kW) continue;
						next.emplace_back(fy * kW + fx, depth + 1);
					}
				}
			}
		}
		pending.swap(next);
		// Floor that no door leads to is still floor: give it its own region
		// at the deepest depth seen, so later passes can use it. MAZE reaches
		// the same end by retrying the carve and, failing that, teleporters.
		if (pending.empty() && lv.regions.size() <= 60) {
			for (int i = 0; i < 0x400; ++i)
				if (lv.grid[i] == kFloor) {
					pending.emplace_back(i, static_cast<int>(lv.regions.size()));
					break;
				}
		}
	}
}

// Cells belonging to a region, for the "put something in region N" passes
// (1325:1be7 / 1325:1c25).
std::vector<int> regionCells(const Level &lv, int id) {
	std::vector<int> out;
	const uint8_t want = static_cast<uint8_t>(0x41 + id);
	for (int i = 0; i < 0x400; ++i) if (lv.grid[i] == want) out.push_back(i);
	return out;
}

bool pickRegionCell(const Level &lv, R250 &rng, int id, int &row, int &col) {
	const std::vector<int> cells = regionCells(lv, id);
	if (cells.empty()) return false;
	const int p = cells[static_cast<size_t>(rng.range(0, static_cast<int>(cells.size()) - 1))];
	row = p / kW;
	col = p % kW;
	return true;
}

// 1325:0389 -- a random cell matching `want`; kAnyFloor means "any open floor".
constexpr int kAnyFloor = -1;
bool pickRandomCell(const Level &lv, R250 &rng, int want, int &row, int &col) {
	for (int tries = 0x800; tries > 0; --tries) {
		row = rng.range(0, 31);
		col = rng.range(0, 31);
		const uint8_t t = lv.grid[row * kW + col];
		if (want == kAnyFloor ? isFloorGlyph(t) : t == static_cast<uint8_t>(want))
			return true;
	}
	return false;
}

// 1325:0242 / 1325:028d -- neighbour masks over a predicate. Bit order is
// N=1 E=2 S=4 W=8 (DGROUP:0x228); the 8-way table adds the diagonals.
template <typename Pred>
uint8_t mask4(const Level &lv, int row, int col, Pred pred) {
	uint8_t m = 0;
	for (int d = 0; d < 4; ++d) {
		const int ny = row + kDRow[d], nx = col + kDCol[d];
		if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) continue;
		if (pred(lv.grid[ny * kW + nx])) m = static_cast<uint8_t>(m | (1u << d));
	}
	return m;
}

template <typename Pred>
bool all8(const Level &lv, int row, int col, Pred pred) {
	for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx) {
			if (!dy && !dx) continue;
			const int ny = row + dy, nx = col + dx;
			if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) return false;
			if (!pred(lv.grid[ny * kW + nx])) return false;
		}
	return true;
}

// 1325:0117 -- a FREQ_* setting byte becomes a count: roll a percentage, then
// roll dice. Every table is 8 entries of {chance%, dice, sides}; MAZE indexes
// them unchecked, but the shipped settings never exceed 7 so we clamp.
int freqCount(R250 &rng, const uint8_t (*table)[3], int setting) {
	const uint8_t *e = table[setting < 0 ? 0 : (setting > 7 ? 7 : setting)];
	if (rng.range(0, 99) < e[0]) return rng.roll(e[1], e[2], 0);
	return 0;
}

// The seven tables, verified byte-for-byte against DGROUP (file 0x7d70+ds).
constexpr uint8_t kFreqHint[8][3] = {   // ds 0x94
	{0,1,1}, {25,1,1}, {50,1,1}, {75,1,1}, {80,1,2}, {90,1,2}, {100,1,2}, {100,1,3} };
constexpr uint8_t kFreqIllusion[8][3] = {  // ds 0xac
	{0,1,1}, {50,1,1}, {75,1,2}, {80,1,3}, {90,1,4}, {95,1,5}, {100,1,6}, {100,1,7} };
constexpr uint8_t kFreqTreasure[8][3] = {  // ds 0xc4
	{50,1,2}, {100,1,2}, {100,2,2}, {100,4,2}, {100,4,3}, {100,4,4}, {100,6,3}, {100,8,3} };
constexpr uint8_t kFreqRations[8][3] = {   // ds 0xdc
	{33,1,2}, {50,1,2}, {80,1,2}, {90,1,2}, {100,1,2}, {100,2,2}, {100,2,3}, {100,2,4} };
constexpr uint8_t kFreqMonsters[8][3] = {  // ds 0xf4
	{100,6,5}, {100,8,5}, {100,10,5}, {100,6,10}, {100,10,7}, {100,16,5}, {100,10,9}, {100,10,10} };
constexpr uint8_t kFreqTraps[8][3] = {     // ds 0x10c
	{0,1,1}, {20,1,1}, {40,1,1}, {60,1,1}, {80,1,1}, {100,1,1}, {100,1,2}, {100,2,2} };
constexpr uint8_t kFreqPits[8][3] = {      // ds 0x124 -- byte-identical to traps,
	{0,1,1}, {20,1,1}, {40,1,1}, {60,1,1}, {80,1,1}, {100,1,1}, {100,1,2}, {100,2,2} };
                                           // but a separate table in the binary

} // namespace

// ------------------------------------------------------- the generator ----

namespace {

// Everything one dungeon needs while it is being built. In MAZE these are all
// DGROUP globals; grouping them keeps the passes readable and, more usefully,
// makes the whole generator a pure function of (seed, settings).
struct Gen {
	uint8_t *chunks = nullptr;
	int levels = 0;
	uint32_t seed = 0;
	const uint8_t *set = nullptr;      // the 12-byte SETTINGS.DAT struct
	DungeonOut *out = nullptr;

	// 1325:3004's low-water marks: a key/gem/activator is never hidden deeper
	// than the shallowest lock that uses it. Sizes are the real ones --
	// 15 keys at DS:0xAF1, 19 gems at 0xADE, 6 activators at 0xAD8.
	uint8_t lowKey[15] = {}, lowGem[19] = {}, lowAct[6] = {};

	Level lv;                          // the level currently being built
	int level = 0;

	LevelInfo &info() { return out->info[static_cast<size_t>(level)]; }
	std::vector<FeatureRecord> &feats() {
		return out->features[static_cast<size_t>(level)];
	}

	// 1325:121d -- emit a feature record (cap 1000 per level).
	int emitFeature(uint8_t type, uint16_t p2, uint16_t p3, uint8_t mask,
	                int lvlIdx, int row, int col) {
		auto &list = out->features[static_cast<size_t>(lvlIdx)];
		if (list.size() >= 1000) return -1;
		list.push_back(FeatureRecord{ static_cast<uint8_t>(row),
		                              static_cast<uint8_t>(col),
		                              static_cast<uint8_t>(lvlIdx), mask, type,
		                              p2, p3 });
		return static_cast<int>(list.size()) - 1;
	}

	// 1325:11af -- emit an item record (cap 900 for the whole dungeon).
	void emitItem(uint8_t type, uint8_t aux, int lvlIdx, int row, int col) {
		if (out->items.size() >= 900) return;
		out->items.push_back(ItemRecord{ static_cast<uint8_t>(row),
		                                 static_cast<uint8_t>(col),
		                                 static_cast<uint8_t>(lvlIdx), type, aux });
	}

	// 1325:22cb -- the n-th feature of a given type, wrapping. MAZE loops
	// forever when the level has no such record; we give up instead.
	bool nthFeature(int n, uint8_t type, int &row, int &col) {
		std::vector<const FeatureRecord *> hits;
		for (const auto &f : feats()) if (f.type == type) hits.push_back(&f);
		if (hits.empty()) return false;
		const FeatureRecord *f = hits[static_cast<size_t>(n) % hits.size()];
		row = f->y;
		col = f->x;
		return true;
	}

	// 1325:2344 -- where an item goes. Not "any free cell": 40% land on a
	// monster, 25% on a shelf, 35% loose on the floor. Both of those passes
	// run earlier in the tail precisely so this can find them.
	bool pickItemSpot(R250 &rng, int &row, int &col) {
		const int d = rng.range(0, 99);
		if (d < 0x28 && nthFeature(rng.range(0, 0x27), 0x0C, row, col)) return true;
		if (d < 0x41 && nthFeature(rng.range(0, 0x09), 0x1A, row, col)) return true;
		return pickRandomCell(lv, rng, kAnyFloor, row, col);
	}
};

// 1325:2081 -- label the regions, and repair connectivity if the carver left
// floor the party cannot reach.
//
// MAZE retries the labelling up to ten times and, failing that, drops a pair
// of linked teleporters (type 21) to bridge the gap -- which is why type 21 is
// the one feature no FREQ setting controls. We do the same, minus the retries:
// our labeller is deterministic, so retrying it would change nothing.
void passRegions(Gen &g, R250 &rng) {
	labelRegions(g.lv, g.info().entryRow, g.info().entryCol);
	g.info().regionCount = static_cast<int>(g.lv.regions.size());

	if (g.level <= 1) return;          // 1325:1b12 only repairs below level 1
	// Any floor still unlabelled is unreachable from the entry.
	std::vector<int> stranded;
	for (int i = 0; i < 0x400; ++i) if (g.lv.grid[i] == kFloor) stranded.push_back(i);
	if (stranded.empty() || g.lv.regions.empty()) return;

	int here = 0, hereCol = 0;
	if (!pickRegionCell(g.lv, rng, 0, here, hereCol)) return;
	const int there = stranded[static_cast<size_t>(rng.range(0, static_cast<int>(stranded.size()) - 1))];
	const int ty = there / kW, tx = there % kW;
	g.lv.grid[here * kW + hereCol] = kTeleporter;
	g.lv.grid[there] = kTeleporter;
	g.emitFeature(0x15, static_cast<uint16_t>(tx), static_cast<uint16_t>(ty),
	              0x0F, g.level, here, hereCol);
	g.emitFeature(0x15, static_cast<uint16_t>(hereCol), static_cast<uint16_t>(here),
	              0x0F, g.level, ty, tx);
	// Re-label so the newly bridged floor gets a region.
	labelRegions(g.lv, g.info().entryRow, g.info().entryCol);
	g.info().regionCount = static_cast<int>(g.lv.regions.size());
}

// 1325:10a6 -- the down-staircase. Scans regions from the deepest back, for a
// dead-end cell (exactly one open side). The cell gets the ↓ glyph; the cell
// in front of it, and the facing, are what the *next* level's stairs-up record
// points at.
void passStairsDown(Gen &g, R250 &rng) {
	LevelInfo &d = g.info();
	auto open = [](uint8_t t) { return isFloorGlyph(t); };
	for (int r = static_cast<int>(g.lv.regions.size()) - 1; r >= 0; --r) {
		for (int p : regionCells(g.lv, r)) {
			const int y = p / kW, x = p % kW;
			if (g.lv.used[p]) continue;
			const uint8_t m = mask4(g.lv, y, x, open);
			if (m != 1 && m != 2 && m != 4 && m != 8) continue;
			const int dir = (m == 1) ? 0 : (m == 2) ? 1 : (m == 4) ? 2 : 3;
			d.stairRow = y;
			d.stairCol = x;
			d.stairFrontRow = y + kDRow[dir];
			d.stairFrontCol = x + kDCol[dir];
			d.stairFdir = dir;
			g.lv.grid[p] = kDownStair;
			g.lv.used[p] = 1;
			return;
		}
	}
	// No dead end anywhere: fall back to the cell furthest from the entry so
	// the level still has an exit.
	int best = -1, bestP = -1;
	std::vector<int> dist(0x400, -1);
	std::vector<int> q{ d.entryRow * kW + d.entryCol };
	dist[q.front()] = 0;
	for (size_t h = 0; h < q.size(); ++h) {
		const int p = q[h], py = p / kW, px = p % kW;
		for (int dd = 0; dd < 4; ++dd) {
			const int ny = py + kDRow[dd], nx = px + kDCol[dd];
			if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) continue;
			const int nq = ny * kW + nx;
			if (dist[nq] >= 0 || !isFloorGlyph(g.lv.grid[nq])) continue;
			dist[nq] = dist[p] + 1;
			q.push_back(nq);
			if (dist[nq] > best) { best = dist[nq]; bestP = nq; }
		}
	}
	if (bestP < 0) bestP = d.entryRow * kW + d.entryCol;
	d.stairRow = bestP / kW;
	d.stairCol = bestP % kW;
	d.stairFrontRow = d.stairRow;
	d.stairFrontCol = d.stairCol;
	d.stairFdir = rng.range(0, 3);
	g.lv.grid[bestP] = kDownStair;
	g.lv.used[bestP] = 1;
}

// 1325:2132 -- turn some doors into illusionary walls. Note there is no
// failure handling in the original: if no door of the chosen orientation
// exists it writes the glyph wherever the search left off. We skip instead.
void passIllusionaryWalls(Gen &g, R250 &rng) {
	int n = freqCount(rng, kFreqIllusion, g.set[4]);
	while (n-- > 0) {
		const int want = (rng.range(0, 9) < 5) ? kDoorV : kDoorH;
		int y, x;
		if (!pickRandomCell(g.lv, rng, want, y, x)) continue;
		g.lv.grid[y * kW + x] = kIllusionary;
	}
}

// 1325:1cfb -- 2..5 arches per level. An arch is placed like a door (same
// passage test) but never blocks; 330f turns the glyph into a type-23 record.
void passArches(Gen &g, R250 &rng) {
	int n = rng.range(0, 3) + 2;
	for (int tries = 0x800; tries > 0 && n > 0; --tries) {
		if (g.lv.regions.empty()) return;
		const int r = rng.range(0, static_cast<int>(g.lv.regions.size()) - 1);
		int y, x;
		if (!pickRegionCell(g.lv, rng, r, y, x)) continue;
		g.lv.grid[y * kW + x] = kFloor;
		if (placeDoor(g.lv, y, x, /*bounds=*/true, /*spacing=*/2, kArchV, kArchH))
			--n;
		else
			g.lv.grid[y * kW + x] = static_cast<uint8_t>(0x41 + r);
	}
}

// 1325:3004 -- the key (or gem, or activator) that opens a lock. It is never
// hidden deeper in the maze than the shallowest lock of its kind, which is
// what makes DH's dungeons solvable rather than merely random.
void passLockItem(Gen &g, R250 &rng, uint8_t kind, int idx, int region) {
	uint8_t *low = nullptr;
	uint8_t item = 0;
	switch (kind) {
	case 9:  low = &g.lowKey[idx % 15]; item = 4; break;
	case 10: low = &g.lowGem[idx % 19]; item = 5; break;
	default: low = &g.lowAct[idx % 6];  item = 6; break;
	}
	int depth = region >= 0 && region < static_cast<int>(g.lv.regions.size())
	            ? g.lv.regions[static_cast<size_t>(region)].depth : 0;
	if (depth < *low) *low = static_cast<uint8_t>(depth);
	else depth = *low;

	int lvlIdx = g.level, y = 0, x = 0;
	if (g.set[11] && g.level > 0 && rng.roll(1, 100, 0) <= 5) {
		// MULTI_LEVEL_PUZZLES_ON: 5% of locks hide their key further up.
		lvlIdx = g.level - rng.roll(1, 3, 0);
		if (lvlIdx < 0) lvlIdx = 0;
		Level other;
		other.grid = g.chunks + static_cast<size_t>(lvlIdx) * 0x400;
		if (!pickRandomCell(other, rng, kAnyFloor, y, x)) {
			lvlIdx = g.level;                 // shallower level had no floor
			if (!pickRandomCell(g.lv, rng, kAnyFloor, y, x)) return;
		}
	} else {
		// 1325:1c25 -- a cell in some region no deeper than `depth`.
		std::vector<int> pool;
		for (int p = 0; p < 0x400; ++p) {
			const uint8_t t = g.lv.grid[p];
			if (!isFloorGlyph(t)) continue;
			const size_t r = static_cast<size_t>(t - 0x41);
			if (r < g.lv.regions.size() && g.lv.regions[r].depth <= depth)
				pool.push_back(p);
		}
		// A lock with no key is an unopenable door, so never give up: if no
		// region is shallow enough, fall back to any floor at all.
		if (pool.empty())
			for (int p = 0; p < 0x400; ++p)
				if (isFloorGlyph(g.lv.grid[p])) pool.push_back(p);
		if (pool.empty()) return;
		const int p = pool[static_cast<size_t>(rng.range(0, static_cast<int>(pool.size()) - 1))];
		y = p / kW;
		x = p % kW;
	}
	g.emitItem(item, static_cast<uint8_t>(idx), lvlIdx, y, x);
}

// 1325:2de5 -- hang a feature on a wall beside a door. Probes the two cells
// flanking the door's axis; the first solid, unclaimed one takes the record.
bool attachToWall(Gen &g, uint8_t type, uint16_t p2, int doorRow, int doorCol,
                  uint8_t doorTile) {
	// A `│` door sits in a north-south wall run and opens east/west, so its
	// opener goes north or south of it -- and vice versa.
	const int dirs[2] = { (doorTile == kDoorV) ? 0 : 1, (doorTile == kDoorV) ? 2 : 3 };
	for (int i = 0; i < 2; ++i) {
		const int d = dirs[i];
		const int y = doorRow + kDRow[d], x = doorCol + kDCol[d];
		if (y < 0 || x < 0 || y >= kW || x >= kW) continue;
		const int p = y * kW + x;
		if (g.lv.used[p] || g.lv.grid[p] != kRock) continue;
		g.lv.used[p] = 1;
		const int side = (d + 2) & 3;      // the face the player sees
		g.emitFeature(type, p2, 0xFFFF, static_cast<uint8_t>(1u << side),
		              g.level, y, x);
		return true;
	}
	return false;
}

// 1325:312c -- give a door something that opens it. 80% get a lock (keyhole /
// gem hole / special activator, each with a matching item planted no deeper
// than the lock), 20% a button or lever. The link is stamped back into the
// door record as 1000 + the opener's record index.
void passDoorOpener(Gen &g, R250 &rng, int doorIdx, uint8_t tile, int row, int col) {
	// The door's own cell holds a door glyph, not a region letter, so take the
	// region from the floor beside it -- and the *shallower* side, because the
	// key has to be reachable before the door it opens. Getting this wrong is
	// how you generate an unsolvable dungeon.
	int region = 0, best = 1 << 30;
	for (int d = 0; d < 4; ++d) {
		const int ny = row + kDRow[d], nx = col + kDCol[d];
		if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) continue;
		const uint8_t t = g.lv.grid[ny * kW + nx];
		if (!isFloorGlyph(t)) continue;
		const size_t r = static_cast<size_t>(t - 0x41);
		if (r < g.lv.regions.size() && g.lv.regions[r].depth < best) {
			best = g.lv.regions[r].depth;
			region = static_cast<int>(r);
		}
	}
	const int r = rng.roll(1, 100, 0);
	if (r < 0x51) {
		if (r <= g.set[5] * 10) {
			const int s = rng.roll(1, 100, 0);
			uint8_t t;
			int idx;
			if (s < 0x47)      { t = 9;  idx = rng.range(0, 14); }
			else if (s < 0x5B) { t = 10; idx = rng.range(0, 18);
			                     if (idx == 5) idx = rng.range(0, 4); }
			else               { t = 11; idx = rng.range(0, 5); }
			if (attachToWall(g, t, static_cast<uint16_t>(idx), row, col, tile)) {
				g.feats()[static_cast<size_t>(doorIdx)].p2 =
					static_cast<uint16_t>(999 + g.feats().size());
				passLockItem(g, rng, t, idx, region);
				return;
			}
		}
	} else {
		const int s = rng.roll(1, 100, 0);
		const uint8_t t = (s < 5) ? 7 : (s < 0x32) ? 6 : 8;
		if (attachToWall(g, t, 0xFFFF, row, col, tile)) {
			g.feats()[static_cast<size_t>(doorIdx)].p3 =
				static_cast<uint16_t>(999 + g.feats().size());
			return;
		}
	}
	// Nothing fit: the door frame itself becomes the button.
	g.emitFeature(2, 0xFFFF, 0xFFFF, (tile == kDoorV) ? 0x0A : 0x05,
	              g.level, row, col);
}

// 1325:330f -- walk the finished grid and turn glyphs into feature records.
// This is where doors, arches, illusionary walls and both staircases become
// FEA entries, plus the level's optional magic zone.
void passGridToRecords(Gen &g, R250 &rng) {
	// (a) the magic zone -- likelier the deeper you go, and only if ZONES_ON
	if (g.set[9] && rng.range(0, 99) < g.level * 10) {
		MagicZone &z = g.out->zones[static_cast<size_t>(g.level)];
		z.present = true;
		z.h = static_cast<uint8_t>(rng.roll(3, 3, 0));
		z.w = static_cast<uint8_t>(rng.roll(3, 3, 0));
		z.x = static_cast<uint8_t>(rng.range(0, 32 - z.w));
		z.y = static_cast<uint8_t>(rng.range(0, 32 - z.h));
		z.kind = static_cast<uint8_t>(rng.range(0, 6));
		g.emitFeature(0x0D, 0xFFFF, 0xFFFF, 0x00, g.level, z.y, z.x);
	}

	// (b) doors, each immediately followed by its opener so the link holds
	for (int p = 0; p < 0x400; ++p) {
		const uint8_t t = g.lv.grid[p];
		if (t != kDoorV && t != kDoorH) continue;
		const int y = p / kW, x = p % kW;
		const int idx = g.emitFeature(1, 0xFFFF, 0xFFFF,
		                              (t == kDoorV) ? 0x0A : 0x05, g.level, y, x);
		if (idx >= 0) passDoorOpener(g, rng, idx, t, y, x);
	}

	// (c) everything else the glyphs carry
	for (int p = 0; p < 0x400; ++p) {
		const int y = p / kW, x = p % kW;
		switch (g.lv.grid[p]) {
		case kArchV:       g.emitFeature(23, 0xFFFF, 0xFFFF, 0x0A, g.level, y, x); break;
		case kArchH:       g.emitFeature(23, 0xFFFF, 0xFFFF, 0x05, g.level, y, x); break;
		case kIllusionary: g.emitFeature(3,  0xFFFF, 0xFFFF, 0x0F, g.level, y, x); break;
		case kUpStair:     g.emitFeature(4,  0xFFFF, 0xFFFF, 0x0F, g.level, y, x); break;
		case kDownStair:   g.emitFeature(5,  0xFFFF, 0xFFFF, 0x0F, g.level, y, x); break;
		default: break;
		}
	}
}

// 1325:219b -- at most one door-with-a-plain-button per level becomes a fake
// solid wall that the button still opens. The nastiest trick DH plays.
void passSecretWall(Gen &g, R250 &rng) {
	if (g.set[5] == 0) return;
	if (rng.range(0, 99) >= g.level * 10 + 10) return;
	for (int tries = 100; tries > 0; --tries) {
		const int want = (rng.range(0, 9) < 5) ? kDoorV : kDoorH;
		int y, x;
		if (!pickRandomCell(g.lv, rng, want, y, x)) continue;
		auto &f = g.feats();
		for (size_t i = 0; i + 1 < f.size(); ++i) {
			if (f[i].type != 1 || f[i + 1].type != 6) continue;
			if (f[i].x != x || f[i].y != y) continue;
			g.lv.grid[y * kW + x] = kSolidWallDoor;
			f[i].type = 17;
			f[i].mask = 0x0F;
			return;
		}
	}
}

// 1325:299e -- 1..3 pillars, each on a cell with all eight neighbours open
// (i.e. in the middle of a room). The last one placed is the "special" pillar.
void passPillars(Gen &g, R250 &rng) {
	int n = rng.range(0, 2) + 1;
	auto open = [](uint8_t t) { return isFloorGlyph(t); };
	for (int tries = 200; n > 0 && tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(g.lv, rng, kAnyFloor, y, x)) continue;
		const int p = y * kW + x;
		if (g.lv.used[p]) continue;
		if (!all8(g.lv, y, x, open)) continue;
		g.lv.grid[p] = kPillar;
		g.lv.used[p] = 1;
		--n;
		g.emitFeature(n == 0 ? 30 : 25, 0xFFFF, 0xFFFF, 0x0F, g.level, y, x);
	}
}

// 1325:27a6 -- monsters. The deepest region gets a guard of monster kind 2
// sitting on 1..3 piles of treasure; the rest of the level gets kinds 0 and 1
// according to FREQ_MONSTERS.
void passMonsters(Gen &g, R250 &rng) {
	for (int r = static_cast<int>(g.lv.regions.size()) - 1; r >= 0; --r) {
		int y, x;
		if (!pickRegionCell(g.lv, rng, r, y, x)) continue;
		g.emitFeature(0x0C, 2, 0xFFFF, 0x04, g.level, y, x);
		for (int k = rng.range(0, 2); k >= 0; --k) g.emitItem(3, 0, g.level, y, x);
		break;
	}
	int n = freqCount(rng, kFreqMonsters, g.set[1]);
	// ponytail: MAZE's loop only decrements on success, so it spins forever
	// once every open cell is claimed. Cap the attempts.
	for (int tries = 0x800; n > 0 && tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(g.lv, rng, kAnyFloor, y, x)) break;
		const int p = y * kW + x;
		if (g.lv.used[p]) continue;
		g.lv.used[p] = 1;
		g.emitFeature(0x0C, static_cast<uint16_t>(rng.range(0, 1)), 0xFFFF, 0x04,
		              g.level, y, x);
		--n;
	}
}

// 1325:1f68 -- a random open cell that is NOT a dead end, for a spinner.
bool pickJunction(Level &lv, R250 &rng, int &row, int &col, uint8_t &mask) {
	auto open = [](uint8_t t) { return isFloorGlyph(t); };
	for (int tries = 0x200; tries > 0; --tries) {
		if (!pickRandomCell(lv, rng, kAnyFloor, row, col)) return false;
		const int p = row * kW + col;
		if (lv.used[p]) continue;
		mask = mask4(lv, row, col, open);
		if (mask == 0 || mask == 1 || mask == 2 || mask == 4 || mask == 8) continue;
		lv.used[p] = 1;
		return true;
	}
	return false;
}

// 1325:1db9 -- the far wall at the end of a corridor at least five cells long,
// which is where object and spell holes go (you have to see them coming).
bool pickCorridorEnd(Level &lv, R250 &rng, int &row, int &col, int &facing) {
	for (int tries = 0x200; tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(lv, rng, kAnyFloor, y, x)) return false;
		const int d = rng.range(0, 3);
		// Run to the end of the corridor, then measure it coming back.
		int cy = y, cx = x;
		while (true) {
			const int ny = cy + kDRow[d], nx = cx + kDCol[d];
			if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) break;
			if (!isFloorGlyph(lv.grid[ny * kW + nx])) break;
			cy = ny; cx = nx;
		}
		const int back = (d + 2) & 3;
		int len = 1, by = cy, bx = cx;
		while (true) {
			const int ny = by + kDRow[back], nx = bx + kDCol[back];
			if (ny < 0 || nx < 0 || ny >= kW || nx >= kW) break;
			if (!isFloorGlyph(lv.grid[ny * kW + nx])) break;
			by = ny; bx = nx; ++len;
		}
		if (len <= 4) continue;
		const int wy = cy + kDRow[d], wx = cx + kDCol[d];
		if (wy < 0 || wx < 0 || wy >= kW || wx >= kW) continue;
		const int p = wy * kW + wx;
		if (lv.grid[p] != kRock || lv.used[p]) continue;
		lv.used[p] = 1;
		row = wy; col = wx; facing = back;
		return true;
	}
	return false;
}

// 1325:254d -- traps, split three ways by a d100: spike pits, spinners, and
// holes in the end wall of a long corridor.
void passTraps(Gen &g, R250 &rng) {
	int n = freqCount(rng, kFreqTraps, g.set[6]);
	if (g.level == 0) n /= 3;                 // go easy on the first level
	for (int tries = 0x200; n > 0 && tries > 0; --tries) {
		const int r = rng.roll(1, 100, 0);
		int y, x;
		if (r <= 30) {
			if (!pickRandomCell(g.lv, rng, kAnyFloor, y, x)) continue;
			const int p = y * kW + x;
			if (g.lv.used[p]) continue;
			g.lv.used[p] = 1;
			g.lv.grid[p] = kSpikeTrap;
			g.emitFeature(0x12, 0xFFFF, 0xFFFF, 0x0F, g.level, y, x);
			--n;
		} else if (r <= 60) {
			uint8_t m = 0;
			if (!pickJunction(g.lv, rng, y, x, m)) continue;
			g.lv.grid[y * kW + x] = kSpinner;
			const uint16_t p2 = (m == 5 || m == 10)
			                    ? 2 : static_cast<uint16_t>(rng.range(0, 2) + 1);
			g.emitFeature(0x16, p2, 0xFFFF, 0x0F, g.level, y, x);
			--n;
		} else {
			int facing = 0;
			if (!pickCorridorEnd(g.lv, rng, y, x, facing)) continue;
			uint8_t t;
			uint16_t p2;
			if (rng.range(0, 9) < 5) { t = 0x0F; p2 = static_cast<uint16_t>(rng.range(0, 2)); }
			else                     { t = 0x0E; p2 = static_cast<uint16_t>(rng.range(0, 1)); }
			g.emitFeature(t, p2, 0xFFFF, static_cast<uint8_t>(1u << facing),
			              g.level, y, x);
			--n;
		}
	}
}

// 1325:2a81 -- 2..4 wall shelves. Runs before the item passes on purpose:
// a quarter of all items end up on one.
void passShelves(Gen &g, R250 &rng) {
	int n = rng.range(0, 2) + 2;
	auto open = [](uint8_t t) { return isFloorGlyph(t); };
	for (int tries = 200; n > 0 && tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(g.lv, rng, kRock, y, x)) continue;
		const int p = y * kW + x;
		if (g.lv.used[p]) continue;
		const uint8_t m = mask4(g.lv, y, x, open);
		if (!m) continue;
		int d;
		do { d = rng.range(0, 3); } while (!((1u << d) & m));
		g.lv.used[p] = 1;
		--n;
		g.emitFeature(0x1A, 0xFFFF, 0xFFFF, static_cast<uint8_t>(1u << d),
		              g.level, y, x);
	}
}

// 1325:23be -- hint sheets, one of 80 texts each.
void passHintSheets(Gen &g, R250 &rng) {
	int n = freqCount(rng, kFreqHint, g.set[8]);
	while (n > 0) {
		int y, x;
		if (!g.pickItemSpot(rng, y, x)) break;
		g.emitItem(1, static_cast<uint8_t>(rng.range(0, 0x4F)), g.level, y, x);
		--n;
	}
}

// 1325:24e4 -- rations. See FEATURES.md §4: this is driven by settings byte 3,
// which the binary's own name table mislabels.
void passRations(Gen &g, R250 &rng) {
	int n = freqCount(rng, kFreqRations, g.set[3]);
	while (n > 0) {
		int y, x;
		if (!g.pickItemSpot(rng, y, x)) break;
		const uint8_t aux = (rng.range(0, 99) < g.set[3] * 4) ? 1 : 0;
		g.emitItem(2, aux, g.level, y, x);
		--n;
	}
}

// 1325:2415 -- level 0's two gifts, then coins, then treasure.
void passTreasure(Gen &g, R250 &rng) {
	int y, x;
	if (g.level == 0) {
		if (g.pickItemSpot(rng, y, x)) g.emitItem(9, 0, 0, y, x);    // grappling hook
		if (g.pickItemSpot(rng, y, x)) g.emitItem(10, 0, 0, y, x);   // amulet of return
	}
	int n = rng.range(0, 2) + 1;
	while (n > 0 && g.pickItemSpot(rng, y, x)) { g.emitItem(8, 0, g.level, y, x); --n; }
	n = freqCount(rng, kFreqTreasure, g.set[2]);
	while (n > 0 && g.pickItemSpot(rng, y, x)) { g.emitItem(3, 0, g.level, y, x); --n; }
}

// 1325:289b -- 2..4 windows, punched through a wall that has open floor on
// exactly two opposite sides. The last one placed is the "special" window.
void passWindows(Gen &g, R250 &rng) {
	int n = rng.range(0, 2) + 2;
	auto open = [](uint8_t t) { return isFloorGlyph(t); };
	for (int tries = 100; n > 0 && tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(g.lv, rng, kRock, y, x)) continue;
		const int p = y * kW + x;
		if (g.lv.used[p]) continue;
		if (!spacingOk(g.lv, y, x, 1)) continue;
		const uint8_t m = mask4(g.lv, y, x, open);
		if (m != 5 && m != 10) continue;
		g.lv.grid[p] = kWindow;
		g.lv.used[p] = 1;
		--n;
		g.emitFeature(n == 0 ? 29 : 24, 0xFFFF, 0xFFFF, m, g.level, y, x);
	}
}

// 1325:2b51 -- exactly one healer per level (the first wall slot found), then
// up to 39 wall decorations, then up to 10 floor decorations in corridors.
void passDecorations(Gen &g, R250 &rng) {
	auto open = [](uint8_t t) { return isFloorGlyph(t); };
	int n = 0x28;
	for (int tries = 0x800; n > 0 && tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(g.lv, rng, kRock, y, x)) continue;
		const int p = y * kW + x;
		if (g.lv.used[p]) continue;
		uint8_t m = mask4(g.lv, y, x, open);
		if (!m) continue;
		g.lv.used[p] = 1;
		uint8_t t;
		if (n == 0x28) {
			t = 27;                          // the healer
			int d;
			do { d = rng.range(0, 3); } while (!((1u << d) & m));
			m = static_cast<uint8_t>(1u << d);
		} else {
			t = 16;                          // level decoration
		}
		g.emitFeature(t, 0, 0xFFFF, m, g.level, y, x);
		--n;
	}
	n = 10;
	for (int tries = 100; n > 0 && tries > 0; --tries) {
		int y, x;
		if (!pickRandomCell(g.lv, rng, kAnyFloor, y, x)) continue;
		const int p = y * kW + x;
		if (g.lv.used[p]) continue;
		const uint8_t m = mask4(g.lv, y, x, open);
		if (m != 5 && m != 10) continue;
		g.lv.used[p] = 1;
		g.emitFeature(28, 0, 0xFFFF, 0x0F, g.level, y, x);
		--n;
	}
}

} // namespace

// -------------------------------------------------------- the dungeon ----

// 1325:3986 + 1325:375f + the whole-dungeon passes, run end to end.
//
// Two things make this portable at all. First, MAZE seeds each level
// independently -- 1325:375f opens with `srand(seed + level + 1)` -- so level
// N does not depend on every draw made before it. Second, the zone table is
// decided up front from one stream seeded with the raw seed, before any level
// is generated.
void generateDungeon(uint8_t *chunks, int levels, uint32_t seed,
                     const uint8_t *settings, DungeonOut &out) {
	out.info.assign(static_cast<size_t>(levels), LevelInfo{});
	out.features.assign(static_cast<size_t>(levels), {});
	out.zones.assign(static_cast<size_t>(levels), MagicZone{});
	out.items.clear();

	// 1325:3aee -- a seed of 0 means "roll one": MAZE substitutes the BIOS
	// timer at 0040:006C, and DH's Customization screen writes 0 whenever the
	// seed reads "(random)". Taking the 0 literally would hand every install
	// the same dungeon. Whatever we settle on is reported back so the caller
	// can record it, exactly as MAZE stores it for SEED.TXT and LEVELS.DAT.
	if (seed == 0)
		seed = static_cast<uint32_t>(
		    std::chrono::steady_clock::now().time_since_epoch().count()) | 1u;
	out.seedUsed = seed;

	Gen g;
	g.chunks = chunks;
	g.levels = levels;
	g.seed = seed;
	g.set = settings;
	g.out = &out;
	for (uint8_t &v : g.lowKey) v = 0xFF;
	for (uint8_t &v : g.lowGem) v = 0xFF;
	for (uint8_t &v : g.lowAct) v = 0xFF;

	// --- 1325:3986: zone assignment ---------------------------------
	// Zone picks which of five layout algorithms a level gets. Levels 0-3 are
	// always zone 1, the deepest is zone 4, one random middle level is zone 0,
	// and the rest roll 1..3.
	R250 top(seed);
	for (int l = 0; l < levels; ++l) { out.info[l].zone = 1; out.info[l].water = false; }
	for (int l = 4; l < levels - 1; ++l)
		out.info[l].zone = static_cast<uint8_t>(top.range(0, 2) + 1);
	if (levels > 0) out.info[levels - 1].zone = 4;
	if (settings[10] && levels > 8) out.info[top.range(7, levels - 2)].water = true;
	if (levels > 5) {
		int pick;
		do { pick = top.range(4, levels - 2); } while (out.info[pick].water);
		out.info[pick].zone = 0;
	}

	// --- 1325:375f, once per level ----------------------------------
	int genRow = 0, genCol = 0;
	for (int l = 0; l < levels; ++l) {
		R250 rng(seed + static_cast<uint32_t>(l) + 1u);
		g.level = l;
		g.lv = Level{};
		g.lv.grid = chunks + static_cast<size_t>(l) * 0x400;

		// 1325:04ab picks the generation entry: level 0 rolls it, deeper
		// levels inherit the level above's stairs-down cell, snapped odd.
		if (l == 0) {
			genRow = rng.range(0, 14) * 2 + 1;
			genCol = rng.range(0, 14) * 2 + 1;
		} else {
			genRow |= 1;
			genCol |= 1;
		}

		switch (out.info[l].zone) {
		case 1: genRooms(g.lv, rng, 0, genRow, genCol); break;
		case 3: genRooms(g.lv, rng, 1, genRow, genCol); break;
		case 4: genRooms(g.lv, rng, 2, genRow, genCol); break;
		default:   // zones 0 and 2: a plain maze, zone 2 with rooms stamped on
			initGrid(g.lv);
			backtrack(g.lv, rng, genRow, genCol);
			sealBorder(g.lv);
			break;
		}
		placeEntry(g.lv, rng, genRow, genCol, out.info[l].entryRow,
		           out.info[l].entryCol, out.info[l].fdir);
		if (out.info[l].zone == 2) {
			extraRooms(g.lv, rng, genRow, genCol);
		} else if (out.info[l].zone != 0) {
			const int first = (out.info[l].zone == 1) ? 1 : 3;
			for (int i = first; i < g.lv.roomCount; ++i)
				punchDoors(g.lv, rng, g.lv.rooms[i]);
		}
		if (out.info[l].zone != 4) topUpDoors(g.lv, rng);

		// --- the feature tail, in MAZE's order (r2 0x6b40) ----------
		//   2081 [10a6] 2132 1cfb 330f 219b 299e 27a6 254d 2a81
		//   23be 24e4 2415 289b 2b51
		passRegions(g, rng);
		if (l < levels - 1) passStairsDown(g, rng);
		passIllusionaryWalls(g, rng);
		passArches(g, rng);
		passGridToRecords(g, rng);
		passSecretWall(g, rng);
		passPillars(g, rng);
		passMonsters(g, rng);
		passTraps(g, rng);
		passShelves(g, rng);
		passHintSheets(g, rng);
		passRations(g, rng);
		passTreasure(g, rng);
		passWindows(g, rng);
		passDecorations(g, rng);

		// The water level's helm is planted on a level *above* it.
		if (out.info[l].water && l > 0) {
			const int above = top.range(0, l - 1);
			Level other;
			other.grid = chunks + static_cast<size_t>(above) * 0x400;
			int y, x;
			if (pickRandomCell(other, top, kAnyFloor, y, x))
				g.emitItem(0x0B, static_cast<uint8_t>(l), above, y, x);
		}

		out.info[l].stairFdirValid = (l < levels - 1);
		genRow = out.info[l].stairRow;
		genCol = out.info[l].stairCol;
	}

	// --- 1325:26f6: pits, which need two levels to line up ----------
	{
		R250 rng(seed ^ 0x50495453u);   // MAZE uses the global stream here
		int n = freqCount(rng, kFreqPits, settings[7]);
		for (int tries = 20; n > 0 && tries > 0; --tries) {
			const int L = rng.range(1, levels - 2);
			// 1325:1ef7 -- open on L *and* directly below on L+1.
			Level a, b;
			a.grid = chunks + static_cast<size_t>(L) * 0x400;
			b.grid = chunks + static_cast<size_t>(L + 1) * 0x400;
			int y, x;
			bool ok = false;
			for (int t = 0x800; t > 0 && !ok; --t) {
				if (!pickRandomCell(a, rng, kAnyFloor, y, x)) break;
				ok = isFloorGlyph(b.grid[y * kW + x]);
			}
			if (!ok) continue;
			a.grid[y * kW + x] = kFloorPit;
			// bytes 4..7 of a floor-pit record are a destination tuple; the
			// facing is rolled here rather than at write time (see the note on
			// packFeatureFile).
			const uint16_t face = static_cast<uint16_t>(rng.range(0, 3));
			g.emitFeature(0x13, 0xFFFF, face, 0x0F, L, y, x);
			g.emitFeature(0x14, 0xFFFF, 0xFFFF, 0x0F, L + 1, y, x);
			--n;
		}
	}

	// --- 1325:36a2: the quest objects -------------------------------
	{
		R250 rng(seed ^ 0x51554553u);
		for (int i = 0; i < 4; ++i) {
			const int L = rng.range(4, levels - 1);
			Level a;
			a.grid = chunks + static_cast<size_t>(L) * 0x400;
			int y, x;
			if (pickRandomCell(a, rng, kAnyFloor, y, x))
				g.emitItem(7, static_cast<uint8_t>(i), L, y, x);
		}
		const int L = rng.range(0, 1);
		Level a;
		a.grid = chunks + static_cast<size_t>(L) * 0x400;
		int y, x;
		if (pickRandomCell(a, rng, kAnyFloor, y, x)) g.emitItem(12, 4, L, y, x);
	}

	// --- 1325:4017: collapse the character map to the on-disk encoding.
	// MAZE strips the region letters first (1325:1736) and runs this at write
	// time off the global stream, so our wall *textures* differ from a real
	// run even where the geometry matches. Nothing but the renderer reads them.
	R250 tex(seed ^ 0x54455855u);
	for (int i = 0; i < levels * 0x400; ++i)
		chunks[i] = (chunks[i] == kRock) ? static_cast<uint8_t>(tex.range(0, 1))
		                                 : kFloor;
}

// ------------------------------------------------------- the file writers ----

// 1325:3c76 -- FEA%02d.DAT: an 8-byte header record, one 8-byte body record
// per feature, then an 8-byte all-zero terminator.
//
// The switch that fills bytes 4..7 is indexed by `type - 1` and so only covers
// types 1..22; 23..30 (arches, windows, pillars, shelves, healer, decorations)
// take the default and leave those bytes zero.
std::vector<uint8_t> packFeatureFile(const DungeonOut &d, int level) {
	const LevelInfo &me = d.info[static_cast<size_t>(level)];
	const int levels = static_cast<int>(d.info.size());
	std::vector<uint8_t> out;

	// Header record. Bytes 6 and 7 are never assigned by MAZE -- the buffer is
	// memset to zero and the header path writes only 0..5.
	out.insert(out.end(), {
		0,
		static_cast<uint8_t>(me.entryCol),
		static_cast<uint8_t>(me.entryRow),
		static_cast<uint8_t>(me.fdir),
		static_cast<uint8_t>(me.water ? 1 : 0),
		static_cast<uint8_t>(me.zone == 0 ? 1 : 0),
		0, 0 });

	for (const FeatureRecord &f : d.features[static_cast<size_t>(level)]) {
		uint8_t r[8] = { f.type, f.x, f.y, f.mask, 0, 0, 0, 0 };
		switch (f.type) {
		case 1:   // current door -- two u16 opener links, or 0xFFFF for none
			r[4] = static_cast<uint8_t>(f.p2 & 0xFF);
			r[5] = static_cast<uint8_t>(f.p2 >> 8);
			r[6] = static_cast<uint8_t>(f.p3 & 0xFF);
			r[7] = static_cast<uint8_t>(f.p3 >> 8);
			break;
		case 4:   // stairs up -> where you came from on the level above
			if (level == 0) {
				r[4] = r[5] = r[6] = r[7] = 0xFF;
			} else {
				const LevelInfo &up = d.info[static_cast<size_t>(level - 1)];
				r[4] = static_cast<uint8_t>(up.stairFrontCol);
				r[5] = static_cast<uint8_t>(up.stairFrontRow);
				r[6] = static_cast<uint8_t>(level - 1);
				r[7] = static_cast<uint8_t>(up.stairFdir);
			}
			break;
		case 5:   // stairs down -> where you arrive on the level below
			if (level + 1 < levels) {
				const LevelInfo &dn = d.info[static_cast<size_t>(level + 1)];
				r[4] = static_cast<uint8_t>(dn.entryCol);
				r[5] = static_cast<uint8_t>(dn.entryRow);
				r[6] = static_cast<uint8_t>(level + 1);
				r[7] = static_cast<uint8_t>(dn.fdir);
			}
			break;
		case 9: case 10: case 11:   // keyhole / gem hole / special activator
		case 12:                    // level creature -- p2 is the monster kind
		case 14: case 15:           // object hole / spell hole
		case 22:                    // spinner
			r[4] = static_cast<uint8_t>(f.p2 & 0xFF);
			break;
		case 13: {                  // magic zone -- extent travels separately
			const MagicZone &z = d.zones[static_cast<size_t>(level)];
			r[4] = z.kind;
			r[5] = z.w;
			r[6] = z.h;
			break;
		}
		case 19:                    // floor pit -> the same cell one level down
			r[4] = f.x;
			r[5] = f.y;
			r[6] = static_cast<uint8_t>(level + 1);
			r[7] = static_cast<uint8_t>(f.p3 & 0xFF);
			break;
		case 21:                    // magical teleporter -> its partner
			r[4] = static_cast<uint8_t>(f.p2 & 0xFF);
			r[5] = static_cast<uint8_t>(f.p3 & 0xFF);
			r[6] = static_cast<uint8_t>(level);
			break;
		default:
			break;
		}
		out.insert(out.end(), r, r + 8);
	}
	out.insert(out.end(), 8, 0);          // terminator
	return out;
}

// 1325:3bb0 -- items.dat: the 5-byte source records permuted into 8 bytes,
// then an all-zero terminator.
std::vector<uint8_t> packItemFile(const DungeonOut &d) {
	std::vector<uint8_t> out;
	for (const ItemRecord &i : d.items)
		out.insert(out.end(), { i.type, i.x, i.y, i.level, i.aux, 0, 0, 0 });
	out.insert(out.end(), 8, 0);
	return out;
}

} // namespace THIRDEYE::runtime::dh
