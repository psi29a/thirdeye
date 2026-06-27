#include "gtest/gtest.h"

#include "../thirdeye/chargen/item_dat.hpp"
#include "../thirdeye/chargen/itemtype_dat.hpp"
#include "../thirdeye/graphics/bitmap.hpp"
#include "../thirdeye/graphics/cps.hpp"

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

// Bundled CHARGEN/CHARPICS.BMP: 90 32x32 portraits, encoded with the same
// scanline RLE format every other AESOP/16 non-VFX bitmap uses (verified
// byte-for-byte against the file -- see the format note in
// graphics/bitmap.cpp's Bitmap ctor). The "yellow-pixels" portrait bug
// regressed when the previous decoder assumed a custom 6-byte sub-header +
// literal-byte rows; this test pins both shape geometry and a concrete
// pixel from portrait 1 (Bob's beard) so a regression of the decoder lights
// up here, not just on screen.
TEST(CharPics_Test, BitmapDecodesAllBundledPortraits) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "CHARPICS.BMP";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	std::ifstream f(path, std::ios::binary);
	ASSERT_TRUE(f.is_open()) << "failed to open fixture: " << path;
	std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
	GRAPHICS::Bitmap bmp(bytes);
	ASSERT_EQ(90u, bmp.getNumberOfBitmaps())
	    << "expected 90 portraits in bundled CHARPICS.BMP";
	for (uint16_t i = 0; i < bmp.getNumberOfBitmaps(); ++i) {
		EXPECT_EQ(32u, bmp.getWidth(i))  << "portrait " << i;
		EXPECT_EQ(32u, bmp.getHeight(i)) << "portrait " << i;
		auto px = bmp[i];
		EXPECT_EQ(32u * 32u, px.size()) << "portrait " << i << " pixel buffer";
	}

	// Portrait 1, row 0 (the bundled file's first non-blank portrait, an
	// old wizard): the RLE decodes to 14 pixels of color 0xac (x=0..13),
	// then 11 mixed literal-copy pixels (0f 97 0f a4 0f 97 a4 8f c6 c7 c7,
	// x=14..24), then 6 more of 0xac (x=25..30). Per-row rle_width is 31,
	// so x=31 stays transparent (0) -- CHARPICS portraits fill only the
	// left 31 of the 32-wide canvas, like a 1-px right padding column.
	auto px1 = bmp[1];
	ASSERT_EQ(32u * 32u, px1.size());
	EXPECT_EQ(0xac, px1[0]);    // row 0, col 0  (fill run)
	EXPECT_EQ(0x0f, px1[14]);   // row 0, col 14 (start of literal-copy)
	EXPECT_EQ(0xc7, px1[24]);   // row 0, col 24 (end of literal-copy)
	EXPECT_EQ(0xac, px1[30]);   // row 0, col 30 (trailing fill run)
	EXPECT_EQ(0x00, px1[31]);   // row 0, col 31 (transparent right edge)
}

// --- LCW / Format80 (Westwood) ---------------------------------------
//
// The four LCW command families, exercised with a hand-built stream so a
// regression in any one of them lights up here (catching a bug at unit-test
// time, not on a corrupted backdrop).
TEST(Cps_Test, LcwHandBuiltCoversAllOpcodes) {
	// Short-copy encoding: bits 6..4 = (count - 3), bits 3..0 = offset hi.
	// 0x82 'A' 'B'   -- literal copy 2 bytes ("AB")
	// 0x30 0x01      -- short rel copy: count=((0x30>>4)&7)+3=3+3=6,
	//                   offset=((0x30&0x0F)<<8)|0x01 = 0x001 = 1
	//                   (self-overlapping: writes 'B' six times -> "BBBBBB")
	// 0xC3 0x00 0x00 -- medium copy from absolute output:
	//                   count=(0xC3&0x3F)+3=6, pos=0 (read 6 from start of
	//                   dest = "ABBBBB")
	// 0xFE 0x04 0x00 0x5A         -- large fill: count=4, colour=0x5A
	// 0xFF 0x03 0x00 0x00 0x00    -- large abs copy: count=3, pos=0 (-> "ABB")
	// 0x80                        -- end of stream
	std::vector<uint8_t> s = {
		0x82, 'A', 'B',
		0x30, 0x01,
		0xC3, 0x00, 0x00,
		0xFE, 0x04, 0x00, 0x5A,
		0xFF, 0x03, 0x00, 0x00, 0x00,
		0x80,
	};
	auto out = GRAPHICS::decompressLCW(s.data(), s.size(), 1024);
	std::vector<uint8_t> want = { 'A', 'B' };
	for (int i = 0; i < 6; ++i) want.push_back('B');                      // BBBBBB
	for (int i = 0; i < 6; ++i) want.push_back(want[i]);                  // ABBBBB
	for (int i = 0; i < 4; ++i) want.push_back(0x5A);                     // ZZZZ
	for (int i = 0; i < 3; ++i) want.push_back(want[i]);                  // ABB
	EXPECT_EQ(want, out);
}

// Real-data: bundled CHARGEN/CHARGEN.CPS decompresses to a 64000-byte
// 320x200 indexed image with no embedded palette. Pin both.
TEST(Cps_Test, LoadsBundledChargenBackdrop) {
	const char *dataDir = std::getenv("THIRDEYE_TEST_DATA_DIR");
	if (!dataDir) GTEST_SKIP() << "set THIRDEYE_TEST_DATA_DIR=/path/to/eob3 dir";
	auto path = std::filesystem::path(dataDir) / "CHARGEN" / "CHARGEN.CPS";
	if (!std::filesystem::exists(path)) GTEST_SKIP() << path << " not found";

	auto cps = GRAPHICS::loadCps(path);
	EXPECT_EQ(320, cps.width);
	EXPECT_EQ(200, cps.height);
	EXPECT_EQ(64000u, cps.pixels.size());
	EXPECT_TRUE(cps.palette.empty());
	// Spot-check that the upper-left corner is a stone tone (not all zero):
	// the title bar / frame border in the bundled backdrop ends up near
	// palette index 0x44-ish at (0, 0). Tightening to a specific palette
	// index would couple us to the file's exact palette mapping; the loose
	// "not zero" check just guards against a decoder that fills everything
	// with the sentinel byte from a malformed back-reference.
	EXPECT_NE(0u, cps.pixels[0]);
}
