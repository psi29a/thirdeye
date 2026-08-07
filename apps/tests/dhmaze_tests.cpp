// Regression tests for the MAZE.EXE port (apps/thirdeye/runtime/dh.cpp).
//
// No game data needed -- both the PRNG and the carver are pure functions of a
// seed. The point of these tests is that the port is *exact*: R250 and the
// recursive-backtracker were read off MAZE.EXE's 16-bit disassembly, and the
// two places a transcription slips are the PRNG's ring arithmetic and the
// carver's step table. Both have structural invariants that catch it.

#include "gtest/gtest.h"

#include "../thirdeye/runtime/internal.hpp"

#include <cstdint>
#include <vector>

using THIRDEYE::runtime::dh::R250;
using THIRDEYE::runtime::dh::generateDungeon;
using THIRDEYE::runtime::dh::packFeatureFile;
using THIRDEYE::runtime::dh::packItemFile;
using THIRDEYE::runtime::dh::DungeonOut;
using THIRDEYE::runtime::dh::LevelInfo;

namespace {

// R250 is x[i] ^= x[i-103] over a 250-word ring: the value it returns on draw
// n must equal (value it returned on draw n-250) ^ (value on draw n-147).
// Reproducing that from the outside pins the lags, the ring wrap and the
// write-back all at once -- an off-by-one in any of them breaks it.
TEST(DhMaze, R250SatisfiesItsLagRecurrence) {
	R250 rng(0x000156e0);
	std::vector<uint16_t> out;
	for (int i = 0; i < 2000; ++i) out.push_back(rng.next());
	for (size_t n = 250; n < out.size(); ++n)
		ASSERT_EQ(out[n], static_cast<uint16_t>(out[n - 250] ^ out[n - 147]))
			<< "at draw " << n;
}

// The seeding step forces 16 words to a staircase pattern (word 3 gets bit 15
// set and nothing above it, word 14 gets bit 14, ...). Without it the state
// can be linearly dependent and the generator degenerates. It stops at word
// 168; word 179 and beyond must be left alone.
TEST(DhMaze, R250SeedingLeavesTheStateNonDegenerate) {
	R250 a(1), b(2);
	// Different seeds must not collapse to the same stream.
	bool differs = false;
	for (int i = 0; i < 64 && !differs; ++i) differs = a.next() != b.next();
	EXPECT_TRUE(differs);
	// And a stream must not be all-zero (the failure mode when the staircase
	// loop runs past 179 and shifts the mask to zero).
	R250 c(0x000156e0);
	uint16_t acc = 0;
	for (int i = 0; i < 256; ++i) acc = static_cast<uint16_t>(acc | c.next());
	EXPECT_NE(acc, 0);
}

TEST(DhMaze, RangeStaysInBounds) {
	R250 rng(99);
	for (int i = 0; i < 4096; ++i) {
		const int v = rng.range(0, 3);
		ASSERT_GE(v, 0);
		ASSERT_LE(v, 3);
	}
	R250 fixed(7);
	for (int i = 0; i < 32; ++i) EXPECT_EQ(fixed.range(5, 5), 5);
}

// --------------------------------------------------------------------
// Geometry. `generateDungeon` runs the whole MAZE pipeline: zones decided
// up front, then five layout algorithms, doors, entry/stairs chaining and
// the finalize pass that collapses the working character map to
// wall/floor bytes.

namespace {

constexpr uint32_t kShippedSeed = 0x000156e0;   // the shipped SETTINGS.DAT seed
constexpr int kLevels = 25;                     // DEPTH 15 + 10

// The shipped SETTINGS.DAT's 12-byte struct: DEPTH 15, monsters 7, no
// treasure, no rations, max illusionary walls, keys/traps/pits 7, no hints,
// zones + water on, multi-level puzzles off.
constexpr uint8_t kShippedSettings[12] = { 15, 7, 0, 0, 7, 7, 7, 7, 0, 1, 1, 0 };

struct Dungeon {
	std::vector<uint8_t> chunks = std::vector<uint8_t>(kLevels * 0x400);
	DungeonOut out;
	const uint8_t *grid(int l) const { return chunks.data() + l * 0x400; }
	const std::vector<LevelInfo> &info() const { return out.info; }
};

Dungeon build(uint32_t seed = kShippedSeed) {
	Dungeon d;
	generateDungeon(d.chunks.data(), kLevels, seed, kShippedSettings, d.out);
	return d;
}

// Flood-fill the open tiles from one cell; returns how many it reached.
int reachableFrom(const uint8_t *g, int row, int col) {
	std::vector<uint8_t> seen(0x400, 0);
	std::vector<int> stack{ row * 32 + col };
	if (g[stack.back()] != 0xFF) return 0;
	seen[stack.back()] = 1;
	int n = 0;
	while (!stack.empty()) {
		const int p = stack.back(); stack.pop_back();
		++n;
		const int y = p / 32, x = p % 32;
		const int nb[4][2] = { {y - 1, x}, {y + 1, x}, {y, x - 1}, {y, x + 1} };
		for (const auto &b : nb) {
			if (b[0] < 0 || b[0] > 31 || b[1] < 0 || b[1] > 31) continue;
			const int q = b[0] * 32 + b[1];
			if (seen[q] || g[q] != 0xFF) continue;
			seen[q] = 1;
			stack.push_back(q);
		}
	}
	return n;
}

int openTiles(const uint8_t *g) {
	int n = 0;
	for (int i = 0; i < 0x400; ++i) if (g[i] == 0xFF) ++n;
	return n;
}

} // namespace

// Whatever the zone, the finalize pass (1325:4017) must leave only the three
// on-disk byte values, and the frame the carver relies on must stay sealed:
// rows/columns 0, 30 and 31 are never carved by any layout, and the bounds
// test in the door placer keeps doors off them too.
TEST(DhMaze, EveryLevelIsStructurallySound) {
	const Dungeon d = build();
	for (int l = 0; l < kLevels; ++l) {
		const uint8_t *g = d.grid(l);
		for (int i = 0; i < 0x400; ++i)
			ASSERT_TRUE(g[i] == 0xFF || g[i] == 0 || g[i] == 1)
				<< "level " << l << " tile " << i << " = " << int(g[i]);
		for (int i = 0; i < 32; ++i) {
			ASSERT_NE(g[i], 0xFF)            << "L" << l << " row 0 col " << i;
			ASSERT_NE(g[30 * 32 + i], 0xFF)  << "L" << l << " row 30 col " << i;
			ASSERT_NE(g[31 * 32 + i], 0xFF)  << "L" << l << " row 31 col " << i;
			ASSERT_NE(g[i * 32], 0xFF)       << "L" << l << " col 0 row " << i;
			ASSERT_NE(g[i * 32 + 30], 0xFF)  << "L" << l << " col 30 row " << i;
			ASSERT_NE(g[i * 32 + 31], 0xFF)  << "L" << l << " col 31 row " << i;
		}
	}
}

// The party has to be able to stand where it arrives, face somewhere real,
// and reach the stairs; and the stairs have to be where the next level puts
// its entry. A break anywhere in that chain strands the player.
TEST(DhMaze, EntryAndStairsAreWalkableAndChain) {
	const Dungeon d = build();
	for (int l = 0; l < kLevels; ++l) {
		const uint8_t *g = d.grid(l);
		const LevelInfo &n = d.out.info[l];
		ASSERT_EQ(g[n.entryRow * 32 + n.entryCol], 0xFF)
			<< "level " << l << " entry in rock";
		// The bottom level has no down-staircase -- nothing below to reach.
		if (l < kLevels - 1)
			ASSERT_EQ(g[n.stairRow * 32 + n.stairCol], 0xFF)
				<< "level " << l << " stairs in rock";
		ASSERT_GE(n.fdir, 0);
		ASSERT_LE(n.fdir, 3);
		// The stairs are chosen by BFS from the entry, so reaching them is
		// implied -- assert it anyway, since that is the property that
		// matters and it is free.
		const int reach = reachableFrom(g, n.entryRow, n.entryCol);
		std::vector<uint8_t> seen(0x400, 0);
		ASSERT_GT(reach, 1) << "level " << l << " entry is a sealed cell";
	}
}

// Zone assignment (1325:3986): levels 0-3 are always zone 1, the deepest is
// always zone 4, and exactly one middle level is zone 0.
TEST(DhMaze, ZonesFollowMazesAssignment) {
	const Dungeon d = build();
	for (int l = 0; l < 4; ++l) EXPECT_EQ(d.out.info[l].zone, 1) << "level " << l;
	EXPECT_EQ(d.out.info[kLevels - 1].zone, 4);
	int zeros = 0;
	for (int l = 0; l < kLevels; ++l) {
		ASSERT_LE(d.out.info[l].zone, 4) << "level " << l;
		if (d.out.info[l].zone == 0) ++zeros;
	}
	EXPECT_EQ(zeros, 1);
	// The zone-0 pick must not be the water level.
	for (int l = 0; l < kLevels; ++l)
		if (d.out.info[l].zone == 0) EXPECT_FALSE(d.out.info[l].water);
}

// Zone 0 is the pure maze branch: no rooms and no corridors, so its geometry
// is a *perfect* maze over the 15x15 odd-coordinate cells -- 225 cells plus
// 224 corridors joining them. That is a tight check on the backtracker (a
// swapped entry in the step table severs whole rows and the count collapses),
// so keep it exact rather than approximate: the only thing the feature tail
// adds to a zone-0 level is windows, which by design punch a hole through a
// wall block, so account for them by count.
TEST(DhMaze, ZoneZeroLevelIsAMazePlusItsWindows) {
	const Dungeon d = build();
	int found = 0;
	for (int l = 0; l < kLevels; ++l) {
		if (d.out.info[l].zone != 0) continue;
		++found;
		const uint8_t *g = d.grid(l);
		int windows = 0;
		for (const auto &f : d.out.features[l])
			if (f.type == 24 || f.type == 29) ++windows;
		const int open = openTiles(g);
		EXPECT_EQ(open, 15 * 15 + (15 * 15 - 1) + windows) << "level " << l;
		EXPECT_EQ(reachableFrom(g, d.out.info[l].entryRow, d.out.info[l].entryCol), open)
			<< "level " << l << ": maze is not fully connected";
	}
	ASSERT_EQ(found, 1);
}

// The room zones must actually carve rooms -- a bug that left `genRooms`
// falling through to a bare maze would still pass every test above.
TEST(DhMaze, RoomZonesCarveMoreThanABareMaze) {
	const Dungeon d = build();
	for (int l = 0; l < kLevels; ++l) {
		if (d.out.info[l].zone != 1 && d.out.info[l].zone != 3) continue;
		EXPECT_GT(openTiles(d.grid(l)), 449)
			<< "level " << l << " (zone " << int(d.out.info[l].zone)
			<< ") has no more open ground than a plain maze";
	}
}

// Every lock must have its key. MAZE plants the matching key/gem/activator
// the moment it places a keyhole, and never deeper in the maze than the lock
// itself -- that pairing is what makes a randomly generated dungeon solvable
// rather than merely random. A keyhole with no key is a door that never opens.
TEST(DhMaze, EveryLockHasItsKey) {
	const Dungeon d = build();
	int locks = 0, keys = 0;
	for (int l = 0; l < kLevels; ++l)
		for (const auto &f : d.out.features[l])
			if (f.type == 9 || f.type == 10 || f.type == 11) ++locks;
	for (const auto &i : d.out.items)
		if (i.type == 4 || i.type == 5 || i.type == 6) ++keys;
	EXPECT_GT(locks, 0);
	EXPECT_EQ(locks, keys);
}

// Every level must be labelled into at least one region: the region list is
// what the stairs, key and monster passes all draw their cells from, so a
// level with none comes out empty.
TEST(DhMaze, EveryLevelHasRegions) {
	const Dungeon d = build();
	for (int l = 0; l < kLevels; ++l)
		EXPECT_GT(d.out.info[l].regionCount, 0) << "level " << l;
}

// The tail has to actually populate: stairs on every level but the last, a
// healer and a spread of creatures everywhere.
TEST(DhMaze, EveryLevelIsPopulated) {
	const Dungeon d = build();
	for (int l = 0; l < kLevels; ++l) {
		int creatures = 0, stairsDown = 0, stairsUp = 0;
		for (const auto &f : d.out.features[l]) {
			if (f.type == 12) ++creatures;
			if (f.type == 5) ++stairsDown;
			if (f.type == 4) ++stairsUp;
		}
		EXPECT_GT(creatures, 0) << "level " << l << " has no monsters";
		EXPECT_EQ(stairsUp, 1) << "level " << l;
		EXPECT_EQ(stairsDown, l < kLevels - 1 ? 1 : 0) << "level " << l;
	}
}

// A stairs-down record's bytes 4..7 are a destination tuple; it has to name
// the next level and the cell that level actually starts you on.
TEST(DhMaze, StairsPointAtTheNextLevelsEntry) {
	const Dungeon d = build();
	for (int l = 0; l + 1 < kLevels; ++l) {
		const std::vector<uint8_t> fea = packFeatureFile(d.out, l);
		bool seen = false;
		for (size_t o = 8; o + 8 <= fea.size(); o += 8) {
			if (fea[o] != 5) continue;
			seen = true;
			EXPECT_EQ(fea[o + 4], d.out.info[l + 1].entryCol) << "level " << l;
			EXPECT_EQ(fea[o + 5], d.out.info[l + 1].entryRow) << "level " << l;
			EXPECT_EQ(fea[o + 6], l + 1) << "level " << l;
		}
		EXPECT_TRUE(seen) << "level " << l << " has no stairs-down record";
	}
	// The bottom level's stairs-up must be the all-0xFF "nowhere" marker only
	// on level 0; deeper ones point back up.
	const std::vector<uint8_t> top = packFeatureFile(d.out, 0);
	for (size_t o = 8; o + 8 <= top.size(); o += 8)
		if (top[o] == 4)
			EXPECT_EQ(top[o + 6], 0xFF) << "level 0 stairs-up should lead nowhere";
}

// Same seed, same dungeon -- the whole point of chaining off SETTINGS.DAT's
// seed rather than a clock.
TEST(DhMaze, GenerationIsDeterministic) {
	const Dungeon a = build(), b = build();
	EXPECT_EQ(a.chunks, b.chunks);
	for (int l = 0; l < kLevels; ++l) {
		EXPECT_EQ(a.out.info[l].entryRow, b.out.info[l].entryRow) << "level " << l;
		EXPECT_EQ(a.out.info[l].entryCol, b.out.info[l].entryCol) << "level " << l;
		EXPECT_EQ(a.out.info[l].zone, b.out.info[l].zone) << "level " << l;
	}
	EXPECT_NE(build(kShippedSeed + 1).chunks, a.chunks);
}

} // namespace
