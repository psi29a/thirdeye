#include "gtest/gtest.h"

#include "../thirdeye/savegame/items_tmp.hpp"
#include "../thirdeye/savegame/savegame_dir.hpp"
#include "../thirdeye/savegame/transfer.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

namespace {

using THIRDEYE::savegame::ItemsTmp;
using THIRDEYE::savegame::TransferState;

void writeU16(std::vector<uint8_t> &b, size_t off, uint16_t v) {
	if (off + 1 >= b.size()) b.resize(off + 2, 0);
	b[off] = v & 0xFF;
	b[off + 1] = (v >> 8) & 0xFF;
}

void writeU32(std::vector<uint8_t> &b, size_t off, uint32_t v) {
	if (off + 3 >= b.size()) b.resize(off + 4, 0);
	for (int i = 0; i < 4; ++i) b[off + i] = (v >> (8 * i)) & 0xFF;
}

void writeStr(std::vector<uint8_t> &b, size_t off, const char *s, size_t cap) {
	if (off + cap > b.size()) b.resize(off + cap, 0);
	for (size_t i = 0; i < cap; ++i) {
		char c = s[i];
		b[off + i] = static_cast<uint8_t>(c);
		if (c == 0) break;
	}
}

} // namespace

// --- ITEMS.TMP ---------------------------------------------------------

// An empty / too-short buffer yields a default ItemsTmp without crashing.
TEST(ItemsTmp_Test, ShortBufferYieldsEmpty) {
	std::vector<uint8_t> empty;
	auto p = THIRDEYE::savegame::parseItemsTmp(empty);
	EXPECT_TRUE(p.characters.empty());
	EXPECT_EQ(0, p.position.x);
	EXPECT_EQ(0, p.position.level);

	std::vector<uint8_t> headerOnly(100, 0);
	auto p2 = THIRDEYE::savegame::parseItemsTmp(headerOnly);
	EXPECT_TRUE(p2.characters.empty());
}

// Party position lives at file offsets 252..255 (X, Y, facing, level).
// "Quick Start Party" is level 3 @ (7,24) facing E (1).
TEST(ItemsTmp_Test, ReadsPartyPosition) {
	std::vector<uint8_t> b(677, 0); // enough to clear the header but no PC records
	b[252] = 7;
	b[253] = 24;
	b[254] = 1;
	b[255] = 3;
	auto p = THIRDEYE::savegame::parseItemsTmp(b);
	EXPECT_EQ(7,  p.position.x);
	EXPECT_EQ(24, p.position.y);
	EXPECT_EQ(1,  p.position.facing);
	EXPECT_EQ(3,  p.position.level);
	EXPECT_TRUE(p.characters.empty()); // no records fit
}

// A single PC record with Sir Mikeal's "Quick Start Party" stats survives the
// round-trip through the parser intact.
TEST(ItemsTmp_Test, ReadsOnePcRecord) {
	constexpr size_t kBase = 677;
	constexpr size_t kStride = 627;
	std::vector<uint8_t> b(kBase + kStride, 0);
	b[252] = 7; b[253] = 24; b[254] = 1; b[255] = 3;

	writeU16(b, kBase + 0,   32);          // object index
	writeU16(b, kBase + 2,   1369);        // class = PC
	writeStr(b, kBase + 155, "Sir Mikeal", 20);
	writeU16(b, kBase + 189, 97);          // HP cur
	writeU16(b, kBase + 191, 97);          // HP max
	b[kBase + 195] = 100;                  // food%
	writeU32(b, kBase + 197, 600000u);     // XP
	b[kBase + 209] = 18;                   // STR
	b[kBase + 210] = 94;                   // STR%
	b[kBase + 211] = 12;                   // INT
	b[kBase + 212] = 16;                   // WIS
	b[kBase + 213] = 16;                   // DEX
	b[kBase + 214] = 17;                   // CON
	b[kBase + 215] = 17;                   // CHA

	writeU16(b, kBase + 127, 994);         // body
	writeU16(b, kBase + 131, 993);         // right hand
	writeU16(b, kBase + 139, 992);         // left hand
	// All other equip slots empty (0xFFFF):
	for (int s : {1, 3, 4, 5, 7, 8, 9, 10, 11})
		writeU16(b, kBase + 127 + s * 2, 0xFFFFu);

	auto p = THIRDEYE::savegame::parseItemsTmp(b);
	ASSERT_EQ(1u, p.characters.size());
	const auto &c = p.characters[0];
	EXPECT_EQ(32,    c.objectIndex);
	EXPECT_EQ(1369,  c.classNumber);
	EXPECT_EQ("Sir Mikeal", c.name);
	EXPECT_EQ(97,    c.hpCurrent);
	EXPECT_EQ(97,    c.hpMax);
	EXPECT_EQ(100,   c.foodPct);
	EXPECT_EQ(600000, c.xp[0]);  // class 1 XP
	EXPECT_EQ(18,    c.str);
	EXPECT_EQ(94,    c.strPct);
	EXPECT_EQ(12,    c.intel);
	EXPECT_EQ(16,    c.wis);
	EXPECT_EQ(16,    c.dex);
	EXPECT_EQ(17,    c.con);
	EXPECT_EQ(17,    c.cha);
	EXPECT_EQ(994,   c.equip[0]);  // body
	EXPECT_EQ(-1,    c.equip[1]);  // bracers empty
	EXPECT_EQ(993,   c.equip[2]);  // right hand
	EXPECT_EQ(992,   c.equip[6]);  // left hand
	EXPECT_EQ(-1,    c.equip[11]); // helmet empty
}

// Stops on the first record that doesn't fit -- no out-of-bounds read on a
// truncated save with a partial record. The parser's "fits" check is on the
// record's footprint (last touched byte = CHA @+215), so we sit the buffer
// end *between* record N's footprint and record N+1's footprint.
TEST(ItemsTmp_Test, TruncatedRecordStopsParse) {
	constexpr size_t kBase = 677;
	constexpr size_t kStride = 627;
	constexpr size_t kFootprint = 216; // matches kRecordFootprint in items_tmp.cpp
	// One full record + a partial second (less than footprint).
	std::vector<uint8_t> b(kBase + kStride + kFootprint - 1, 0);
	writeU16(b, kBase + 0, 32);
	writeU16(b, kBase + 2, 1369);
	auto p = THIRDEYE::savegame::parseItemsTmp(b);
	EXPECT_EQ(1u, p.characters.size());
}

// Opt-in integration test against the real Quick Start Party save. Skipped
// unless THIRDEYE_TEST_DATA_DIR points at an EOB3 install (which has
// `SAVEGAME/ITEMS.TMP` in it) -- we don't ship game assets, so this only runs
// for the developer who has the data sibling-mounted.
//
// Set the env var to the dir that holds EYE.RES, e.g.:
//   THIRDEYE_TEST_DATA_DIR=$HOME/Workspace/private/eob3/data ctest ...
//
// Expected: level 3 @ (7,24) facing E (1), 6 records, PCs Sir Mikeal +
// Stonebeard with their published "Quick Start Party" stats.
TEST(ItemsTmp_Test, ParsesQuickStartParty) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 data dir";
	auto path = std::filesystem::path(dataDir) / "SAVEGAME" / "ITEMS.TMP";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	auto p = THIRDEYE::savegame::loadItemsTmp(path);
	EXPECT_EQ(7,  p.position.x);
	EXPECT_EQ(24, p.position.y);
	EXPECT_EQ(1,  p.position.facing);  // east
	EXPECT_EQ(3,  p.position.level);
	// 4 party + 2 joined NPCs + 4 reserve slots = 10 PC-class records.
	ASSERT_EQ(10u, p.characters.size());
	// Item stream begins right after them, at 677 + 10*627 = 6947.
	EXPECT_EQ(6947u, p.itemStreamOff);

	const auto &mikeal     = p.characters[0];
	const auto &stonebeard = p.characters[1];

	// Sir Mikeal: HP 97, STR 18/94, XP 600,000, single-class fighter.
	EXPECT_EQ("Sir Mikeal", mikeal.name);
	EXPECT_EQ(1369, mikeal.classNumber);
	EXPECT_EQ(97,   mikeal.hpCurrent);
	EXPECT_EQ(97,   mikeal.hpMax);
	EXPECT_EQ(18,   mikeal.str);
	EXPECT_EQ(94,   mikeal.strPct);
	EXPECT_EQ(17,   mikeal.con);
	EXPECT_EQ(600000, mikeal.xp[0]);
	EXPECT_EQ(-1,     mikeal.xp[1]);     // single-class -> class 2 unused
	EXPECT_EQ(10,     mikeal.levels[0]);
	EXPECT_EQ(58,     mikeal.portrait);
	EXPECT_EQ(994,    mikeal.equip[0]);  // body
	EXPECT_EQ(993,    mikeal.equip[2]);  // right hand
	EXPECT_EQ(992,    mikeal.equip[6]);  // left hand

	// Stonebeard (dwarf, multi-class fighter/cleric levels 10/10).
	EXPECT_EQ("Stonebeard", stonebeard.name);
	EXPECT_EQ(18, stonebeard.str);
	EXPECT_EQ(76, stonebeard.strPct);
	EXPECT_EQ(19, stonebeard.con);
	EXPECT_EQ(8,  stonebeard.cha);
	EXPECT_EQ(6,  stonebeard.race);      // dwarf
	EXPECT_EQ(7,  stonebeard.classes);   // multi-class bitfield
	EXPECT_EQ(10, stonebeard.levels[0]);
	EXPECT_EQ(10, stonebeard.levels[1]);
	EXPECT_EQ(24, stonebeard.portrait);

	// Tail: spell arrays always populated (200 bytes each); magicEffects is
	// 0 across the board on a freshly-loaded save.
	EXPECT_EQ(200u, mikeal.spellCnt.size());
	EXPECT_EQ(200u, mikeal.spellStat.size());
	EXPECT_EQ(0,    mikeal.magicEffects);
	EXPECT_EQ(0,    stonebeard.magicEffects);
	// Sparkle is 0xFF (default sentinel) for all 6 named records in this save.
	EXPECT_EQ(0xFF, mikeal.sparkle);
}

// Item chain pointers live in the STATIC BLOCK (last 4 bytes = next_u16,
// prev_u16 LE), not in the trailer. Pin this against the 14-arrow stack in
// the bundled save: each arrow's next-link points at the next arrow's id,
// the head has prev=0xFFFF, the tail has next=0xFFFF.
TEST(ItemStream_Test, StaticBlockChainPointersInBundledSave) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 data dir";
	auto path = std::filesystem::path(dataDir) / "SAVEGAME" / "ITEMS.TMP";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	std::ifstream f(path.string(), std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
	// Use a real static-size lookup: cls 1335 (arrow) has a 13-byte block per
	// the trailer dump. Hardcode just what we need to walk this fixed chain.
	auto p = THIRDEYE::savegame::loadItemsTmp(path);
	auto stream = THIRDEYE::savegame::parseItemStream(
	    bytes, p.itemStreamOff, [](uint16_t cls) -> uint32_t {
	        // Complete class -> static-block-size table for every class
	        // observed in the bundled Quick Start save (extracted from the
	        // THIRDEYE_DUMP_TRAILERS=1 run). Three distinct sizes: 12, 13, 15.
	        static const uint32_t k13[] = {
	            1323,1324,1325,1326,1327,1328,1329,1330,1332,1333,1334,1335,
	            1336,1338,1339,1340,1341,1342,1343,1349,1350,1351,1353,1355,
	            1682,1696,1709,1714,1723,1726,1751,1754,1761,1764,1770,1786,
	            1791,1797,1818,1839,1879,1891};
	        static const uint32_t k12[] = {
	            1344,1345,1346,1347,1352,1357,1358,1359,1360,1362,1363,1366,
	            1367,1368,1375,1397,1400,1403,1408,1411,1414,1417,1420,1425,
	            1434,1451,1456,1459,1462,1465,1468,1471,1474,1477,1480,1483,
	            1486,1492,1495,1498,1501,1504,1524,1527,1530,1533,1536,1539,
	            1542,1545,1548,1551,1554,1557,1560,1566,1569,1572,1575,1578};
	        static const uint32_t k15[] = {1376,1377,1602};
	        for (uint32_t c : k13) if (c == cls) return 13;
	        for (uint32_t c : k12) if (c == cls) return 12;
	        for (uint32_t c : k15) if (c == cls) return 15;
	        return 0;
	    });
	// Locate the 14-arrow stack head (id 612, cls 1335) -- the parser may have
	// stopped earlier on an unknown class. Verify if present.
	auto it = std::find_if(stream.begin(), stream.end(),
	                       [](const auto &r){ return r.id == 612; });
	if (it == stream.end())
		GTEST_SKIP() << "parser stopped before id 612 (need more class sizes)";
	const auto &r = *it;
	ASSERT_EQ(13u, r.staticBlock.size());
	// Block layout for cls 1335 (arrow, block size 13):
	//   bytes 10..11 = next u16 LE  (0xFFFF = chain tail)
	//   byte  12     = prev u8      (0xFF = chain head; only the low byte of
	//                                the prev item id is stored, since chained
	//                                items live in the same id locality)
	uint16_t next = r.staticBlock[10] | (r.staticBlock[11] << 8);
	uint8_t  prev = r.staticBlock[12];
	EXPECT_EQ(613u, next) << "id 612 should chain to id 613 (next arrow)";
	EXPECT_EQ(0x63, prev) << "id 612 prev byte = low(611) = 0x63";
	// Tail (id 625): next = 0xFFFF
	auto tailIt = std::find_if(stream.begin(), stream.end(),
	                           [](const auto &r){ return r.id == 625; });
	if (tailIt != stream.end()) {
		ASSERT_GE(tailIt->staticBlock.size(), 12u)
		    << "id 625 static block too short for next-pointer read";
		uint16_t tailNext = tailIt->staticBlock[10] | (tailIt->staticBlock[11] << 8);
		EXPECT_EQ(0xFFFFu, tailNext) << "id 625 is chain tail";
	}
	// Head (id 611, Delmair's quiver): prev = 0xFF (no previous)
	auto headIt = std::find_if(stream.begin(), stream.end(),
	                           [](const auto &r){ return r.id == 611; });
	if (headIt != stream.end()) {
		ASSERT_GE(headIt->staticBlock.size(), 13u)
		    << "id 611 static block too short for prev-byte read";
		EXPECT_EQ(0xFF, headIt->staticBlock[12])
		    << "id 611 is chain head (prev=0xFF)";
	}
}

// Trailer byte 3 (magical bonus) decode. The literal trailer values used
// here are the actual byte patterns observed in the bundled Quick Start
// Party save (Father Jon's ring of protection +3, Sir Mikeal's +1 plate/
// short sword/shield, etc.) -- but the test itself is synthetic and runs
// without bundled data, so it stays useful in CI environments without
// THIRDEYE_TEST_DATA_DIR. See `../../eob3_research/SAVEGAME/README.md`
// for the cross-reference table that motivated these literals.
TEST(ItemStream_Test, MagicalBonusDecodedFromTrailerLiterals) {
	THIRDEYE::savegame::ItemRecord r;
	r.trailer = 0x80000007u;       // byte 3 = 0x80 = -128 signed (synthetic boundary)
	EXPECT_EQ(static_cast<int8_t>(-128), r.magicalBonus());
	r.trailer = 0x010000FFu;       // observed "+1" pattern (Sir Mikeal's plate)
	EXPECT_EQ(1, r.magicalBonus());
	r.trailer = 0xFD0000FFu;       // observed "-3" pattern (cursed cls-1350 ring)
	EXPECT_EQ(-3, r.magicalBonus());
}

// Empty-slot trailers in the bundled save are uniformly 0x0000FFFF (bytes
// FF FF 00 00). Pins that part of the RE so future writers can emit the
// correct sentinel on initial-tempfile creation.
TEST(ItemStream_Test, EmptySlotTrailerIsFFFFinBundledSave) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 data dir";
	auto path = std::filesystem::path(dataDir) / "SAVEGAME" / "ITEMS.TMP";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	std::ifstream f(path.string(), std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
	auto p = THIRDEYE::savegame::loadItemsTmp(path);

	// Walk consecutive empty slots from streamOff (cls == 0xFFFF), read each
	// trailer raw. The first non-empty record terminates the run.
	size_t o = p.itemStreamOff;
	int emptyCount = 0;
	while (o + 8 <= bytes.size()) {
		uint16_t cls = bytes[o + 2] | (bytes[o + 3] << 8);
		if (cls != 0xFFFF) break;
		uint32_t t = bytes[o + 4] | (bytes[o + 5] << 8) |
		             (bytes[o + 6] << 16) | (bytes[o + 7] << 24);
		EXPECT_EQ(0x0000FFFFu, t)
		    << "empty slot @0x" << std::hex << o << std::dec
		    << " has unexpected trailer";
		++emptyCount;
		o += 8;
	}
	EXPECT_GT(emptyCount, 0) << "expected some empty slots before first item";
}

// All ten character slots populate when the buffer is large enough.
// EOB3 has 4 party + 2 NPC + 4 reserve = 10 PC-class record slots.
TEST(ItemsTmp_Test, ReadsAllTenSlots) {
	constexpr size_t kBase = 677;
	constexpr size_t kStride = 627;
	std::vector<uint8_t> b(kBase + 10 * kStride, 0);
	for (int i = 0; i < 10; ++i)
		writeU16(b, kBase + i * kStride + 0, static_cast<uint16_t>(32 + i));
	auto p = THIRDEYE::savegame::parseItemsTmp(b);
	ASSERT_EQ(10u, p.characters.size());
	for (int i = 0; i < 10; ++i)
		EXPECT_EQ(32 + i, p.characters[i].objectIndex);
	// And the item-stream offset sits right after the last record.
	EXPECT_EQ(kBase + 10 * kStride, p.itemStreamOff);
}

// --- CREATE.SAV / TransferState ----------------------------------------

// EOB1 type -> EOB3 class map (table123): a few spot checks of the verified
// entries in docs/equipment_slots.md §4.
TEST(Transfer_Test, Table123MapsKnownTypes) {
	using TS = TransferState;
	EXPECT_EQ(1323, TS::table123Lookup(0));   // axe
	EXPECT_EQ(1324, TS::table123Lookup(1));   // long sword
	EXPECT_EQ(1340, TS::table123Lookup(22));  // leather armor
	EXPECT_EQ(1346, TS::table123Lookup(30));  // holy symbol
	EXPECT_EQ(1353, TS::table123Lookup(45));  // two-handed sword
	// Unknown / special-cased (obj=0 in xfer's table) -> 0
	EXPECT_EQ(0,    TS::table123Lookup(34));
	EXPECT_EQ(0,    TS::table123Lookup(999));
}

// Slot categorization: weapons -> WEAPON, armor -> BODY, ring -> RING, etc.
// This is what drives rebuildPlacement; the test pins the category boundaries.
TEST(Transfer_Test, CategoryForKnownClasses) {
	using TS = TransferState;
	using Cat = TS::SlotCat;
	EXPECT_EQ(Cat::WEAPON,    TS::categoryForClass(1324)); // long sword
	EXPECT_EQ(Cat::WEAPON_2H, TS::categoryForClass(1353)); // 2H sword
	EXPECT_EQ(Cat::RANGED,    TS::categoryForClass(1327)); // bow
	EXPECT_EQ(Cat::AMMO,      TS::categoryForClass(1335)); // arrow
	EXPECT_EQ(Cat::BODY,      TS::categoryForClass(1340)); // leather armor
	EXPECT_EQ(Cat::BODY,      TS::categoryForClass(1349)); // robe
	EXPECT_EQ(Cat::HELMET,    TS::categoryForClass(1339)); // helm
	EXPECT_EQ(Cat::SHIELD,    TS::categoryForClass(1343)); // shield
	EXPECT_EQ(Cat::BOOTS,     TS::categoryForClass(1348)); // leather boots
	EXPECT_EQ(Cat::RING,      TS::categoryForClass(1350)); // ring of protection
	EXPECT_EQ(Cat::BRACERS,   TS::categoryForClass(1351)); // bracers
	EXPECT_EQ(Cat::NECKLACE,  TS::categoryForClass(1352)); // necklace
	// Carried fallback: spellbook, thieves' tools, holy symbol, rations, cloak.
	EXPECT_EQ(Cat::CARRIED,   TS::categoryForClass(1345));
	EXPECT_EQ(Cat::CARRIED,   TS::categoryForClass(1355)); // cloak (no slot yet)
}

// Empty / unopened transfer file: item_attrib(1) = -1 (no type), 0 elsewhere.
TEST(Transfer_Test, EmptyDataReturnsSafeDefaults) {
	TransferState ts;
	EXPECT_EQ(-1, ts.itemAttrib(0, 0, 1));   // type
	EXPECT_EQ(0,  ts.itemAttrib(0, 0, 0));   // bits
	EXPECT_EQ(0,  ts.itemAttrib(0, 0, 2));   // bonus
	EXPECT_EQ(0,  ts.playerAttrib(0, 2, 1)); // name byte
}

// --- ITEMS.TMP §2.3 item-object stream --------------------------------

// Empty buffer / empty stream offset out of range -> empty result.
TEST(ItemStream_Test, EmptyBuffer) {
	auto r = THIRDEYE::savegame::parseItemStream({}, 0,
	    [](uint16_t){ return 0u; });
	EXPECT_TRUE(r.empty());
}

// 8-byte empty-slot placeholder (cls = 0xFFFF) is skipped, no entry emitted.
TEST(ItemStream_Test, EmptySlotsSkipped) {
	std::vector<uint8_t> b = {
		// id=42, cls=0xFFFF, 4 placeholder bytes
		0x2a, 0x00,  0xff, 0xff,  0xff, 0xff, 0x00, 0x00,
		// id=43, cls=0xFFFF, 4 placeholder bytes
		0x2b, 0x00,  0xff, 0xff,  0xff, 0xff, 0x00, 0x00,
	};
	auto r = THIRDEYE::savegame::parseItemStream(b, 0,
	    [](uint16_t){ return 16u; }); // would be wrong if used; placeholders use 4
	EXPECT_TRUE(r.empty());
}

// One real item with a known class+static-size yields one record carrying the
// exact static-block bytes. Record layout: 4 header + N statics + 4 trailer.
TEST(ItemStream_Test, OneItem) {
	// Class 1326 (dagger) with a 16-byte static block.
	std::vector<uint8_t> b = {
		0x64, 0x00,  0x2e, 0x05,    // id=100, cls=1326
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
		0xaa, 0xbb, 0xcc, 0xdd,     // 4-byte trailer (skipped by parser)
	};
	auto r = THIRDEYE::savegame::parseItemStream(b, 0,
	    [](uint16_t cls) { return cls == 1326 ? 16u : 0u; });
	ASSERT_EQ(1u, r.size());
	EXPECT_EQ(100u,  r[0].id);
	EXPECT_EQ(1326u, r[0].cls);
	ASSERT_EQ(16u,   r[0].staticBlock.size());
	EXPECT_EQ(0x01,  r[0].staticBlock[0]);
	EXPECT_EQ(0x10,  r[0].staticBlock[15]);
}

// Empties interleaved with real items: parser walks both, returns only reals.
TEST(ItemStream_Test, MixedEmptyAndRealItems) {
	// empty(42, 8 bytes) + dagger(100, 4 + 8 statics + 4 trailer = 16 bytes)
	// + empty(43) + short_sword(101, 4 + 10 + 4 = 18 bytes)
	std::vector<uint8_t> b = {
		0x2a, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, // empty id=42
		0x64, 0x00, 0x2e, 0x05,                         // id=100 cls=1326 dagger
		0,0,0,0, 0,0,0,0,                               // 8 bytes static block
		0,0,0,0,                                        // 4 bytes trailer
		0x2b, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, // empty id=43
		0x65, 0x00, 0x2d, 0x05,                         // id=101 cls=1325 short sword
		0,0,0,0, 0,0,0,0, 0,0,                          // 10 bytes static block
		0,0,0,0,                                        // 4 bytes trailer
	};
	auto r = THIRDEYE::savegame::parseItemStream(b, 0, [](uint16_t cls) {
		if (cls == 1326) return 8u;
		if (cls == 1325) return 10u;
		return 0u;
	});
	ASSERT_EQ(2u, r.size());
	EXPECT_EQ(100u,  r[0].id); EXPECT_EQ(1326u, r[0].cls);
	EXPECT_EQ(101u,  r[1].id); EXPECT_EQ(1325u, r[1].cls);
}

// A truncated last record (missing static block AND/OR trailer) bails cleanly.
TEST(ItemStream_Test, TruncatedRecordStopsParse) {
	std::vector<uint8_t> b = {
		0x64, 0x00, 0x2e, 0x05,  // id=100 cls=1326
		0,0,0,0, 0,0,0,0,        // 8 bytes statics
		0,0,0,0,                 // 4 bytes trailer
		0x65, 0x00, 0x2d, 0x05,  // id=101 cls=1325
		0,0,0,                   // only 3 of 10+4 bytes
	};
	auto r = THIRDEYE::savegame::parseItemStream(b, 0, [](uint16_t cls) {
		if (cls == 1326) return 8u;
		if (cls == 1325) return 10u;
		return 0u;
	});
	ASSERT_EQ(1u, r.size());
	EXPECT_EQ(100u, r[0].id);
}

// --- SAVEGAME.DIR ------------------------------------------------------

// Empty buffer yields no entries (no crash).
TEST(SaveDir_Test, EmptyBuffer) {
	auto e = THIRDEYE::savegame::parseSaveDir({});
	EXPECT_TRUE(e.empty());
}

// A single \r\n-separated entry. EOF marker (\x1a) is stripped.
TEST(SaveDir_Test, OneUsedSlotPlusUnusedSlots) {
	// "Quick Start Party" + 3 underscore-filled slots + \x1a EOF.
	std::vector<unsigned char> data;
	auto append = [&](const std::string &s) {
		for (char c : s) data.push_back(static_cast<unsigned char>(c));
		data.push_back('\r'); data.push_back('\n');
	};
	append("Quick Start Party");
	append("_________________");
	append("_________________");
	append("_________________");
	data.push_back(0x1a);
	auto e = THIRDEYE::savegame::parseSaveDir(data);
	ASSERT_EQ(4u, e.size());
	EXPECT_EQ("Quick Start Party", e[0].name);
	EXPECT_TRUE(e[0].used);
	for (int i = 1; i < 4; ++i) {
		EXPECT_EQ("", e[i].name);
		EXPECT_FALSE(e[i].used);
	}
}

// A trailing entry without CRLF is still captured.
TEST(SaveDir_Test, TailingEntryWithoutNewline) {
	std::vector<unsigned char> data = {'F','o','o','\r','\n','B','a','r'};
	auto e = THIRDEYE::savegame::parseSaveDir(data);
	ASSERT_EQ(2u, e.size());
	EXPECT_EQ("Foo", e[0].name);
	EXPECT_EQ("Bar", e[1].name);
}

// Trailing underscores get trimmed (they're the unused-slot filler).
TEST(SaveDir_Test, TrimsTrailingUnderscores) {
	std::vector<unsigned char> data;
	for (char c : std::string("Sir Mikeal________________________")) data.push_back(c);
	auto e = THIRDEYE::savegame::parseSaveDir(data);
	ASSERT_EQ(1u, e.size());
	EXPECT_EQ("Sir Mikeal", e[0].name);
	EXPECT_TRUE(e[0].used);
}

// Opt-in: real Quick Start Party SAVEGAME.DIR has one used slot.
TEST(SaveDir_Test, ParsesQuickStartPartyDir) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 data dir";
	auto path = std::filesystem::path(dataDir) / "SAVEGAME" / "SAVEGAME.DIR";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";
	auto e = THIRDEYE::savegame::loadSaveDir(path);
	ASSERT_FALSE(e.empty());
	int used = 0;
	for (const auto &s : e) if (s.used) ++used;
	EXPECT_EQ(1, used) << "Quick Start Party save should have exactly one used slot";
	// And that one slot is the bundled save.
	for (const auto &s : e)
		if (s.used) { EXPECT_EQ("Quick Start Party", s.name); break; }
}

// rebuildPlacement on empty data leaves every slot empty.
TEST(Transfer_Test, RebuildPlacementOnEmptyDataLeavesSlotsEmpty) {
	TransferState ts;
	ts.rebuildPlacement();
	for (int pc = 0; pc < 4; ++pc)
		for (int s = 0; s < TransferState::kInvSlots; ++s)
			EXPECT_EQ(-1, ts.pcItemAtSlot[pc][s])
			    << "pc " << pc << " slot " << s;
}

// A single PC carrying [long sword, leather armor, ring of protection, dagger]
// in CREATE.SAV slots 0..3 should land them in their EOB3 body-part slots:
// 16 (rhand), 14 (body), 17 (ring1), 20 (lhand). See docs/equipment_slots.md.
TEST(Transfer_Test, RebuildPlacementCategorizesItems) {
	TransferState ts;
	// Synthesize a CREATE.SAV with one PC. The TransferState reads:
	//   inventory slot k        -> file offset (kPcBase + 2 + 0 * kPcStride
	//                                          + kInvRecOff + k * 2)
	//   item record for id N    -> file offset (kItemArrayBase + (N - 434) * 14)
	//                              with type at +4.
	constexpr size_t kPcBase = TransferState::kPcBase;
	constexpr size_t kInvRecOff = TransferState::kInvRecOff;
	constexpr size_t kItemArrayBase = TransferState::kItemArrayBase;
	constexpr int    kItemIdBase = TransferState::kItemIdBase;
	constexpr size_t kItemRecSize = TransferState::kItemRecSize;

	// Worst-case end address: the item record for the last item we add.
	const int numItems = 4;
	const size_t maxItemRecEnd =
	    kItemArrayBase + (kItemIdBase + numItems - 1 - kItemIdBase + 1) *
	                          kItemRecSize;
	ts.data.assign(maxItemRecEnd, 0);

	// Item types we'll place in CREATE.SAV slots 0..3:
	//   id 434 -> type 1   (long sword)
	//   id 435 -> type 22  (leather armor)
	//   id 436 -> type 42  (ring of protection)
	//   id 437 -> type 5   (dagger)
	const int types[numItems] = {1, 22, 42, 5};
	for (int k = 0; k < numItems; ++k) {
		int id = kItemIdBase + k;
		// Slot k in PC 0's inventory holds the item id.
		size_t slotOff = kPcBase + 2 + 0 * TransferState::kPcStride
		               + kInvRecOff + k * 2;
		ts.data[slotOff]     = static_cast<uint8_t>(id & 0xFF);
		ts.data[slotOff + 1] = static_cast<uint8_t>((id >> 8) & 0xFF);
		// Item record: type lives at +4 in a 14-byte record.
		size_t rec = kItemArrayBase + (id - kItemIdBase) * kItemRecSize;
		ts.data[rec + 4] = static_cast<uint8_t>(types[k]);
	}

	ts.rebuildPlacement();

	// Slot 16 = right hand, holds the long sword (CREATE.SAV slot 0).
	EXPECT_EQ(0, ts.pcItemAtSlot[0][16]);
	// Slot 14 = body, holds the leather armor (CREATE.SAV slot 1).
	EXPECT_EQ(1, ts.pcItemAtSlot[0][14]);
	// Slot 17 = first ring, holds the ring of protection (CREATE.SAV slot 2).
	EXPECT_EQ(2, ts.pcItemAtSlot[0][17]);
	// Slot 20 = left hand, holds the dagger (CREATE.SAV slot 3).
	EXPECT_EQ(3, ts.pcItemAtSlot[0][20]);
	// Other slots stay empty.
	EXPECT_EQ(-1, ts.pcItemAtSlot[0][0]);
	EXPECT_EQ(-1, ts.pcItemAtSlot[0][15]); // bracers (not provided)
	EXPECT_EQ(-1, ts.pcItemAtSlot[0][25]); // helmet
}

// --- Save-slot restoration safety -------------------------------------
//
// Pins the picker's two safety invariants (see items_tmp.hpp docs above
// restoreItems / restoreLevels): fail-fast on missing source so the user's
// currently-loaded save state survives a botched click. These regressed
// in production when the picker fired against an empty slot -- live
// ITEMS.TMP got pre-deleted and then the copy_file failed, leaving the
// user with no save at all.

namespace {
// One-shot scratch dir under the gtest temp area. Caller does the writes.
// Uses a process-local counter (not PID) so the implementation stays
// portable across Linux/macOS/Windows -- no <unistd.h>/<process.h> split.
std::filesystem::path makeScratchDir(const char *tag) {
	static std::atomic<unsigned long long> kCounter{0};
	auto dir = std::filesystem::temp_directory_path() /
	           ("thirdeye_restore_test_" + std::string(tag) + "_" +
	            std::to_string(kCounter.fetch_add(1)));
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::filesystem::create_directories(dir);
	return dir;
}

void writeBytes(const std::filesystem::path &p,
                std::initializer_list<uint8_t> bytes) {
	std::ofstream f(p, std::ios::binary);
	for (auto b : bytes) f.put(static_cast<char>(b));
}

std::vector<uint8_t> readBytes(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary);
	return {std::istreambuf_iterator<char>(f),
	        std::istreambuf_iterator<char>()};
}
} // namespace

// restoreItems on a missing BIN must NOT touch the live ITEMS.TMP. This is
// the bug that ate the user's Quick Start Party when they clicked an empty
// row in the picker.
TEST(Restore_Test, ItemsMissingSourcePreservesTmp) {
	auto dir = makeScratchDir("items_missing");
	// Seed a live TMP that simulates the currently-loaded save.
	writeBytes(dir / "ITEMS.TMP", {0xCA, 0xFE, 0xBA, 0xBE});
	// No ITEMS_06.BIN exists -> restore must fail-fast and preserve TMP.
	EXPECT_FALSE(THIRDEYE::savegame::restoreItems(dir, 6));
	auto after = readBytes(dir / "ITEMS.TMP");
	ASSERT_EQ(4u, after.size());
	EXPECT_EQ(0xCA, after[0]);
	EXPECT_EQ(0xFE, after[1]);
	EXPECT_EQ(0xBA, after[2]);
	EXPECT_EQ(0xBE, after[3]);
}

// restoreItems with a real source replaces ITEMS.TMP atomically.
TEST(Restore_Test, ItemsHappyPath) {
	auto dir = makeScratchDir("items_happy");
	writeBytes(dir / "ITEMS.TMP",   {0xFF, 0xFF}); // stale
	writeBytes(dir / "ITEMS_03.BIN", {0x01, 0x02, 0x03});
	EXPECT_TRUE(THIRDEYE::savegame::restoreItems(dir, 3));
	auto after = readBytes(dir / "ITEMS.TMP");
	ASSERT_EQ(3u, after.size());
	EXPECT_EQ(0x01, after[0]);
	EXPECT_EQ(0x02, after[1]);
	EXPECT_EQ(0x03, after[2]);
}

// restoreItems with a negative index (the SOP's 1-based "slot 0" maps to
// idx -1) returns false without touching anything.
TEST(Restore_Test, ItemsNegativeIndexNoOp) {
	auto dir = makeScratchDir("items_neg");
	writeBytes(dir / "ITEMS.TMP", {0xAA, 0xBB});
	EXPECT_FALSE(THIRDEYE::savegame::restoreItems(dir, -1));
	auto after = readBytes(dir / "ITEMS.TMP");
	ASSERT_EQ(2u, after.size());
	EXPECT_EQ(0xAA, after[0]);
	EXPECT_EQ(0xBB, after[1]); // entire payload must survive a no-op
}

// restoreLevels gates on ITEMS_NN.BIN existing -- if the picker fires
// against an empty slot, NO LVL??.TMP gets wiped. Live state survives.
TEST(Restore_Test, LevelsNoItemsBinPreservesAllTmp) {
	auto dir = makeScratchDir("levels_noitems");
	// Live level state from the user's previous session.
	writeBytes(dir / "LVL01.TMP", {0x11});
	writeBytes(dir / "LVL07.TMP", {0x77});
	// Slot 6 has no ITEMS_06.BIN (= empty slot).
	EXPECT_EQ(0, THIRDEYE::savegame::restoreLevels(dir, 6));
	// Both TMPs preserved byte-for-byte.
	auto a = readBytes(dir / "LVL01.TMP");
	auto b = readBytes(dir / "LVL07.TMP");
	ASSERT_EQ(1u, a.size()); EXPECT_EQ(0x11, a[0]);
	ASSERT_EQ(1u, b.size()); EXPECT_EQ(0x77, b[0]);
}

// restoreLevels short-circuits on negative slot just like restoreItems --
// the runtime gate (the `restore_level_objects` handler) requires BOTH
// halves to land before firing resume_level, so this is the symmetric
// guard for the items-side fail-fast pinned in ItemsNegativeIndexNoOp.
TEST(Restore_Test, LevelsNegativeIndexNoOp) {
	auto dir = makeScratchDir("levels_neg");
	writeBytes(dir / "LVL01.TMP", {0x11, 0x22, 0x33});
	EXPECT_EQ(0, THIRDEYE::savegame::restoreLevels(dir, -1));
	auto after = readBytes(dir / "LVL01.TMP");
	ASSERT_EQ(3u, after.size());
	EXPECT_EQ(0x11, after[0]);
	EXPECT_EQ(0x22, after[1]);
	EXPECT_EQ(0x33, after[2]);
}

// restoreLevels copies just the LVL??_NN.BIN files that exist; missing
// per-level BINs are skipped without touching their TMP either.
TEST(Restore_Test, LevelsHappyPathCopiesPresentBackups) {
	auto dir = makeScratchDir("levels_happy");
	// Slot 3: ITEMS gate + two of the 14 levels backed up.
	writeBytes(dir / "ITEMS_03.BIN",    {0x00});
	writeBytes(dir / "LVL01_03.BIN",    {0xA1});
	writeBytes(dir / "LVL14_03.BIN",    {0xA2});
	// Live state for one level that's NOT in the backup (must survive).
	writeBytes(dir / "LVL05.TMP",       {0x55});
	EXPECT_EQ(2, THIRDEYE::savegame::restoreLevels(dir, 3));
	// Restored backups overwrote / created the TMP:
	auto a = readBytes(dir / "LVL01.TMP");
	auto b = readBytes(dir / "LVL14.TMP");
	ASSERT_EQ(1u, a.size()); EXPECT_EQ(0xA1, a[0]);
	ASSERT_EQ(1u, b.size()); EXPECT_EQ(0xA2, b[0]);
	// Level 5 had no backup in slot 3 -- its TMP must be untouched.
	auto c = readBytes(dir / "LVL05.TMP");
	ASSERT_EQ(1u, c.size()); EXPECT_EQ(0x55, c[0]);
}
