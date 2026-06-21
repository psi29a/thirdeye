#include "gtest/gtest.h"

#include "../thirdeye/chargen/charpics.hpp"
#include "../thirdeye/chargen/item_dat.hpp"
#include "../thirdeye/chargen/itemtype_dat.hpp"
#include "../thirdeye/graphics/bitmap.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
	ASSERT_EQ(434u, p.items.size());   // fatal: we index items[0..1] below
	ASSERT_EQ(123u, p.names.size());
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
// 1026 bytes = 64 × 16-byte records + u16 trailer (0x0004). No count header.
TEST(ItemTypeDat_Test, ParsesBundledItemTypeDat) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "ITEMTYPE.DAT";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	auto p = THIRDEYE::chargen::loadItemTypeDat(path);
	ASSERT_EQ(64u,     p.types.size());  // fatal: we index types[0] below
	EXPECT_EQ(0x0004,  p.trailer);
	// First record's distinctive bytes (from the file dump):
	//   40 00 08 00 08 00 00 39 01 01 08 00 01 0a 00 00
	EXPECT_EQ(0x0040, p.types[0].kind);
	EXPECT_EQ(0x0008, p.types[0].width);
	EXPECT_EQ(0x0008, p.types[0].height);
	EXPECT_EQ(0x3900, p.types[0].slotMask);
}

// --- Bitmap CHARPICS auto-detect regressions --------------------------

// An older-format buffer where u32@0 happens to equal the buffer size and
// unknown1==0 must NOT be auto-detected as CHARPICS. We disambiguate by
// requiring two monotonic-ascending u32 offsets at +10/+14.
TEST(CharPics_Test, OlderFormatNotMisdetectedAsCharPics) {
	// Simulate an older-format file: u16 fileSize, u16 unknown1=0, u16 numSubs=1,
	// rest random bytes (NOT a sorted offset table). Total size 60.
	std::vector<uint8_t> b(60, 0);
	const uint16_t sz = static_cast<uint16_t>(b.size());
	b[0] = sz & 0xFF; b[1] = (sz >> 8) & 0xFF;     // u16 fileSize -- happens to equal size
	// b[2..3] = 0 (unknown1 = 0). Combined with b[0..1], u32@0 == size.
	b[4] = 1; b[5] = 0;                             // numSubBitmaps = 1
	// b[6..7] = 0 (offset to first sub)
	// Bytes at +10..+17 are NOT plausible monotonic u32 offsets.
	b[10] = 0xAA; b[11] = 0xBB; b[12] = 0; b[13] = 0;
	b[14] = 0x10; b[15] = 0;   b[16] = 0; b[17] = 0;  // 0x10 < 0xBBAA -> non-monotonic
	GRAPHICS::Bitmap bmp(b);
	// Should fall through to OLDER format, numSubBitmaps = 1
	EXPECT_EQ(1u, bmp.getNumberOfBitmaps());
}

// --- CHARPICS via graphics::Bitmap ------------------------------------

// graphics::Bitmap now auto-detects the CHARPICS variant and exposes each
// portrait as a regular Bitmap shape. The pixel decoder is best-effort
// (literal-bytes-clamped-to-width) until the per-row RLE grammar is RE'd --
// the test pins dimensions + that the decode produces width*height bytes.
TEST(CharPics_Test, BitmapDecodesAllBundledPortraits) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "CHARPICS.BMP";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	std::ifstream f(path, std::ios::binary);
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
	GRAPHICS::Bitmap bmp(bytes);
	ASSERT_EQ(89u, bmp.getNumberOfBitmaps())
	    << "expected 89 portraits in bundled CHARPICS.BMP";
	for (uint16_t i = 0; i < bmp.getNumberOfBitmaps(); ++i) {
		EXPECT_EQ(32u, bmp.getWidth(i))  << "portrait " << i;
		EXPECT_EQ(32u, bmp.getHeight(i)) << "portrait " << i;
		auto px = bmp[i];
		EXPECT_EQ(32u * 32u, px.size()) << "portrait " << i << " pixel buffer";
	}
}

// --- CHARPICS.BMP ------------------------------------------------------

TEST(CharPics_Test, EmptyBuffer) {
	auto p = THIRDEYE::chargen::parseCharPics({});
	EXPECT_TRUE(p.portraits.empty());
}

// Hand-build a minimal CHARPICS-style file with 2 tiny portraits:
//   header (10 bytes) + offset table (2 entries, 8 bytes) + portrait sub-headers + RLE bytes.
TEST(CharPics_Test, ParsesHandBuilt) {
	std::vector<uint8_t> b;
	auto u32 = [&](uint32_t v) {
		for (int i = 0; i < 4; ++i) b.push_back((v >> (i * 8)) & 0xFF);
	};
	auto u16 = [&](uint16_t v) {
		b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF);
	};
	auto skip = [&](size_t n) { b.resize(b.size() + n, 0); };

	// Header (placeholder file size; we'll fix it later)
	size_t headerStart = b.size();
	u32(0); u16(0); u16(0); u16(0);

	// Reserve offset table (2 × u32)
	size_t tableStart = b.size();
	u32(0); u32(0);

	// Portrait 0: 4x4, RLE = 5 bytes of fake data
	size_t off0 = b.size();
	u16(3);          // boundsx = width-1
	u16(4);          // boundsy = height
	u16(0);          // reserved
	for (int i = 0; i < 5; ++i) b.push_back(static_cast<uint8_t>(0xA0 + i));

	// Portrait 1: 8x8, RLE = 3 bytes
	size_t off1 = b.size();
	u16(7); u16(8); u16(0);
	b.push_back(0xB1); b.push_back(0xB2); b.push_back(0xB3);

	// Patch offsets + file size
	auto patchU32 = [&](size_t off, uint32_t v) {
		for (int i = 0; i < 4; ++i) b[off + i] = (v >> (i * 8)) & 0xFF;
	};
	patchU32(tableStart + 0, static_cast<uint32_t>(off0));
	patchU32(tableStart + 4, static_cast<uint32_t>(off1));
	patchU32(headerStart, static_cast<uint32_t>(b.size()));

	auto p = THIRDEYE::chargen::parseCharPics(b);
	EXPECT_EQ(b.size(), p.declaredFileSize);
	ASSERT_EQ(2u, p.portraits.size());
	EXPECT_EQ(4,  p.portraits[0].width);
	EXPECT_EQ(4,  p.portraits[0].height);
	EXPECT_EQ(5u, p.portraits[0].rleData.size());
	EXPECT_EQ(0xA0, p.portraits[0].rleData[0]);
	EXPECT_EQ(0xA4, p.portraits[0].rleData[4]);
	EXPECT_EQ(8,  p.portraits[1].width);
	EXPECT_EQ(8,  p.portraits[1].height);
	EXPECT_EQ(3u, p.portraits[1].rleData.size());
}

// Real-data: bundled CHARGEN/CHARPICS.BMP has 89 32x32 portraits.
TEST(CharPics_Test, ParsesBundledCharPics) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "CHARPICS.BMP";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	auto p = THIRDEYE::chargen::loadCharPics(path);
	EXPECT_EQ(82005u, p.declaredFileSize);
	ASSERT_EQ(89u,    p.portraits.size());
	// All 89 are 32x32 in this file (a chargen invariant).
	for (size_t i = 0; i < p.portraits.size(); ++i) {
		EXPECT_EQ(32, p.portraits[i].width)
		    << "portrait " << i << " width";
		EXPECT_EQ(32, p.portraits[i].height)
		    << "portrait " << i << " height";
		EXPECT_GT(p.portraits[i].rleData.size(), 0u);
	}
	// 14 of the 89 are "empty"-shaped (~229 - 6 header = ~223-byte RLE).
	// Pin the distribution loosely.
	int small = 0;
	for (const auto &q : p.portraits)
		if (q.rleData.size() < 300) ++small;
	EXPECT_EQ(14, small) << "expected 14 'empty' portrait slots";
}
