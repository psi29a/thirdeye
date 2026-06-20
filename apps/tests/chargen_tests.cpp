#include "gtest/gtest.h"

#include "../thirdeye/chargen/item_dat.hpp"
#include "../thirdeye/chargen/itemtype_dat.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

using THIRDEYE::chargen::ItemDat;
using THIRDEYE::chargen::ItemTypeDat;

void writeU16(std::vector<uint8_t> &b, size_t off, uint16_t v) {
	if (off + 1 >= b.size()) b.resize(off + 2, 0);
	b[off]     = v & 0xFF;
	b[off + 1] = (v >> 8) & 0xFF;
}

} // namespace

// --- ITEM.DAT ---------------------------------------------------------

TEST(ItemDat_Test, EmptyBuffer) {
	auto p = THIRDEYE::chargen::parseItemDat({});
	EXPECT_TRUE(p.items.empty());
	EXPECT_TRUE(p.names.empty());
}

// Truncated header just gives an empty result, no crash.
TEST(ItemDat_Test, SingleByteHeaderBails) {
	std::vector<uint8_t> b{0x05};
	auto p = THIRDEYE::chargen::parseItemDat(b);
	EXPECT_TRUE(p.items.empty());
}

// Hand-built: 2 items + 1 name. Verify all 11 fields per item + the name.
TEST(ItemDat_Test, ParsesItemsAndNames) {
	std::vector<uint8_t> b;
	// header: 2 items
	writeU16(b, 0, 2);
	// item 0
	std::vector<uint8_t> item0 = {
	    1, 2, 0x80, 31, 22, 7,    // unid id bits pic type subpos
	    0x10, 0x00,                // pos = 16
	    0xFF, 0xFF,                // next = -1
	    0xFE, 0xFF,                // prev = -2
	    3,                          // level
	    0x05                        // value = 5
	};
	b.insert(b.end(), item0.begin(), item0.end());
	// item 1
	std::vector<uint8_t> item1 = {
	    3, 4, 0x40, 32, 23, 8,
	    0x20, 0x00,
	    0x01, 0x00,
	    0x02, 0x00,
	    1,
	    0xFF  // signed -1
	};
	b.insert(b.end(), item1.begin(), item1.end());
	// names: 1 name "Long Sword" padded to 35
	writeU16(b, b.size(), 1);
	std::string name = "Long Sword";
	size_t nameOff = b.size();
	b.resize(nameOff + 35, 0);
	for (size_t i = 0; i < name.size(); ++i)
		b[nameOff + i] = static_cast<uint8_t>(name[i]);

	auto p = THIRDEYE::chargen::parseItemDat(b);
	ASSERT_EQ(2u, p.items.size());

	EXPECT_EQ(1,     p.items[0].unid);
	EXPECT_EQ(2,     p.items[0].id);
	EXPECT_EQ(0x80,  p.items[0].bits);   // glow/magic
	EXPECT_EQ(31,    p.items[0].pic);
	EXPECT_EQ(22,    p.items[0].type);   // leather armor in EOB1 types
	EXPECT_EQ(7,     p.items[0].subpos);
	EXPECT_EQ(16,    p.items[0].pos);
	EXPECT_EQ(-1,    p.items[0].next);
	EXPECT_EQ(-2,    p.items[0].prev);
	EXPECT_EQ(3,     p.items[0].level);
	EXPECT_EQ(5,     p.items[0].value);

	EXPECT_EQ(0x40,  p.items[1].bits);   // identified
	EXPECT_EQ(-1,    p.items[1].value);  // signed!

	ASSERT_EQ(1u, p.names.size());
	EXPECT_EQ("Long Sword", p.names[0]);
	EXPECT_EQ("Long Sword", p.nameOf(0));
	EXPECT_EQ("",            p.nameOf(99));
	EXPECT_EQ("",            p.nameOf(-1));
}

// Real-file integration test: the bundled CHARGEN/ITEM.DAT.
// Per docs/create_sav_and_item_format.md §1: 434 items, 123 names, total 10385 B.
TEST(ItemDat_Test, ParsesBundledItemDat) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "ITEM.DAT";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	auto p = THIRDEYE::chargen::loadItemDat(path);
	EXPECT_EQ(434u, p.items.size());
	EXPECT_EQ(123u, p.names.size());
	// First couple of items have known shape (from the file dump):
	// item[0]: unid=0 id=1 (placeholder/sentinel)
	// item[1]: unid=2 id=1 bits=0 pic=0x1f type=0x16 (leather armor)
	EXPECT_EQ(0,    p.items[0].unid);
	EXPECT_EQ(1,    p.items[0].id);
	EXPECT_EQ(2,    p.items[1].unid);
	EXPECT_EQ(0x1f, p.items[1].pic);
	EXPECT_EQ(22,   p.items[1].type);  // EOB1 type 22 -> EOB3 class 1340 (leather armor)
	// Names should include something recognizable; just sanity-check a few non-empty.
	int nonEmpty = 0;
	for (const auto &n : p.names) if (!n.empty()) ++nonEmpty;
	EXPECT_GT(nonEmpty, 50);
}

// --- ITEMTYPE.DAT -----------------------------------------------------

TEST(ItemTypeDat_Test, EmptyBuffer) {
	auto p = THIRDEYE::chargen::parseItemTypeDat({});
	EXPECT_TRUE(p.types.empty());
}

TEST(ItemTypeDat_Test, ParsesHandBuiltRecords) {
	// 2 × 16-byte records then 2-byte trailer. No count header.
	std::vector<uint8_t> b;
	std::vector<uint8_t> rec0 = {
	    0x40, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x39,
	    0x01, 0x01, 0x08, 0x00, 0x01, 0x00, 0x0a, 0x00
	};
	std::vector<uint8_t> rec1 = {
	    0x01, 0x00, 0x10, 0x00, 0x20, 0x00, 0x3f, 0x00,
	    0x00, 0x01, 0x03, 0x00, 0x02, 0x00, 0x0c, 0x00
	};
	b.insert(b.end(), rec0.begin(), rec0.end());
	b.insert(b.end(), rec1.begin(), rec1.end());
	writeU16(b, b.size(), 0x0004);

	auto p = THIRDEYE::chargen::parseItemTypeDat(b);
	ASSERT_EQ(2u, p.types.size());
	EXPECT_EQ(0x0040, p.types[0].kind);
	EXPECT_EQ(0x0008, p.types[0].width);
	EXPECT_EQ(0x0008, p.types[0].height);
	EXPECT_EQ(0x3900, p.types[0].slotMask);
	EXPECT_EQ(0x000a, p.types[0].field8);
	EXPECT_EQ(0x0001, p.types[1].kind);
	EXPECT_EQ(0x0010, p.types[1].width);
	EXPECT_EQ(0x0020, p.types[1].height);
	EXPECT_EQ(0x003f, p.types[1].slotMask);
	EXPECT_EQ(0x0004, p.trailer);
}

// Real-file integration test: the bundled CHARGEN/ITEMTYPE.DAT.
// 1026 bytes = u16 count (64) + 64 × 16-byte records + u16 trailer (0x0004).
TEST(ItemTypeDat_Test, ParsesBundledItemTypeDat) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "ITEMTYPE.DAT";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	auto p = THIRDEYE::chargen::loadItemTypeDat(path);
	EXPECT_EQ(64u,     p.types.size());
	EXPECT_EQ(0x0004,  p.trailer);
	// First record's distinctive bytes (from the file dump):
	//   40 00 08 00 08 00 00 39 01 01 08 00 01 0a 00 00
	EXPECT_EQ(0x0040, p.types[0].kind);
	EXPECT_EQ(0x0008, p.types[0].width);
	EXPECT_EQ(0x0008, p.types[0].height);
	EXPECT_EQ(0x3900, p.types[0].slotMask);
}
