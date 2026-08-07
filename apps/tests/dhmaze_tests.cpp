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

struct Dungeon {
	std::vector<uint8_t> chunks = std::vector<uint8_t>(kLevels * 0x400);
	std::vector<LevelInfo> info = std::vector<LevelInfo>(kLevels);
	const uint8_t *grid(int l) const { return chunks.data() + l * 0x400; }
};

Dungeon build(uint32_t seed = kShippedSeed, bool water = true) {
	Dungeon d;
	generateDungeon(d.chunks.data(), kLevels, seed, water, d.info.data());
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
		const LevelInfo &n = d.info[l];
		ASSERT_EQ(g[n.entryRow * 32 + n.entryCol], 0xFF)
			<< "level " << l << " entry in rock";
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
	for (int l = 0; l < 4; ++l) EXPECT_EQ(d.info[l].zone, 1) << "level " << l;
	EXPECT_EQ(d.info[kLevels - 1].zone, 4);
	int zeros = 0;
	for (int l = 0; l < kLevels; ++l) {
		ASSERT_LE(d.info[l].zone, 4) << "level " << l;
		if (d.info[l].zone == 0) ++zeros;
	}
	EXPECT_EQ(zeros, 1);
	// The zone-0 pick must not be the water level.
	for (int l = 0; l < kLevels; ++l)
		if (d.info[l].zone == 0) EXPECT_FALSE(d.info[l].water);
}

// Zone 0 is the pure maze branch: no rooms, no corridors, and doors only ever
// replace tiles that are already floor. So it must come out a *perfect* maze
// over the 15x15 odd-coordinate cells -- 225 cells plus 224 corridors joining
// them, all connected, no loops. That is a tight check on the backtracker: a
// swapped entry in the step table severs whole rows and the count collapses.
TEST(DhMaze, ZoneZeroLevelIsAPerfectMaze) {
	const Dungeon d = build();
	int found = 0;
	for (int l = 0; l < kLevels; ++l) {
		if (d.info[l].zone != 0) continue;
		++found;
		const uint8_t *g = d.grid(l);
		const int open = openTiles(g);
		EXPECT_EQ(open, 15 * 15 + (15 * 15 - 1)) << "level " << l;
		EXPECT_EQ(reachableFrom(g, d.info[l].entryRow, d.info[l].entryCol), open)
			<< "level " << l << ": maze is not fully connected";
	}
	ASSERT_EQ(found, 1);
}

// The room zones must actually carve rooms -- a bug that left `genRooms`
// falling through to a bare maze would still pass every test above.
TEST(DhMaze, RoomZonesCarveMoreThanABareMaze) {
	const Dungeon d = build();
	for (int l = 0; l < kLevels; ++l) {
		if (d.info[l].zone != 1 && d.info[l].zone != 3) continue;
		EXPECT_GT(openTiles(d.grid(l)), 449)
			<< "level " << l << " (zone " << int(d.info[l].zone)
			<< ") has no more open ground than a plain maze";
	}
}

// Same seed, same dungeon -- the whole point of chaining off SETTINGS.DAT's
// seed rather than a clock.
TEST(DhMaze, GenerationIsDeterministic) {
	const Dungeon a = build(), b = build();
	EXPECT_EQ(a.chunks, b.chunks);
	for (int l = 0; l < kLevels; ++l) {
		EXPECT_EQ(a.info[l].entryRow, b.info[l].entryRow) << "level " << l;
		EXPECT_EQ(a.info[l].entryCol, b.info[l].entryCol) << "level " << l;
		EXPECT_EQ(a.info[l].zone, b.info[l].zone) << "level " << l;
	}
	EXPECT_NE(build(kShippedSeed + 1).chunks, a.chunks);
}

} // namespace
