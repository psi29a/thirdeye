// Regression test for the scanline (non-VFX) bitmap decoder's coverage mask.
//
// The older AESOP bitmap format is SPARSE: each row names its y, then a series
// of runs at explicit x positions. Pixels no run covers are never written --
// that omission is the transparency. A run may legitimately paint palette
// index 0, and the original renders those as solid black.
//
// The bug this locks in: the draw path used to colorkey index 0, so every
// painted-black pixel became a hole. Dungeon Hack's camp panel is 25% painted
// black over its full area, so a quarter of it showed the dungeon behind it
// (and "Stone Frame" is worse -- 15546 such pixels). EOB3 was unaffected only
// because all 312 of its bitmaps are VFX shapes, which were masked already.
//
// No game data needed: the fixture is a hand-built two-row bitmap.

#include "gtest/gtest.h"

#include "../thirdeye/graphics/bitmap.hpp"

#include <cstdint>
#include <vector>

namespace {

// Older-format container: u16 pad, u16 pad, u16 count, then u16 offsets at
// 6 + i*4. Each sub-bitmap is u16 width, u16 height, then the scanline stream.
// A scanline is: u8 y, then runs of {u16 x | 0x8000 on the last, u8 rle_width,
// u8 pad} followed by tokens -- even marker = copy `marker>>1 + 1` literals,
// odd marker = fill `marker>>1 + 1` with the next byte. y == 0xFF ends it.
std::vector<uint8_t> makeScanlineBitmap() {
	const uint16_t w = 8, h = 2;
	std::vector<uint8_t> v{ 0, 0, 0, 0, 1, 0 };     // pads + count = 1
	v.push_back(10); v.push_back(0);                 // offsets[0] = 10
	v.push_back(0);  v.push_back(0);                 // pad to offset 10
	// sub-bitmap header
	v.push_back(w & 0xFF); v.push_back(w >> 8);
	v.push_back(h & 0xFF); v.push_back(h >> 8);

	// Row 0: at x=2, fill 4 pixels with value 0 -- PAINTED BLACK, the whole
	// point of the test. x=0,1 and 6,7 stay uncovered.
	v.push_back(0);                                  // y = 0
	v.push_back(2); v.push_back(0x80);               // x = 2, last run in row
	v.push_back(4); v.push_back(0);                  // rle_width = 4
	v.push_back((3 << 1) | 1); v.push_back(0);       // fill 4 x value 0

	// Row 1: at x=0, copy 3 literals 7,8,9. x=3..7 stay uncovered.
	v.push_back(1);                                  // y = 1
	v.push_back(0); v.push_back(0x80);               // x = 0, last run in row
	v.push_back(3); v.push_back(0);                  // rle_width = 3
	v.push_back((2 << 1) | 0);                       // copy 3 literals
	v.push_back(7); v.push_back(8); v.push_back(9);

	v.push_back(0xFF);                               // end of scanlines
	return v;
}

TEST(BitmapMask, PaintedBlackIsOpaqueAndGapsAreTransparent) {
	auto data = makeScanlineBitmap();
	GRAPHICS::Bitmap bm(data);
	ASSERT_FALSE(bm.isVFXShape());
	ASSERT_EQ(bm.getNumberOfBitmaps(), 1);
	ASSERT_EQ(bm.getWidth(0), 8);
	ASSERT_EQ(bm.getHeight(0), 2);

	std::vector<uint8_t> mask;
	const std::vector<uint8_t> px = bm.decodeScanlineMasked(0, mask);
	ASSERT_EQ(px.size(), 16u);
	ASSERT_EQ(mask.size(), 16u);

	// Row 0: only x=2..5 were painted, and they were painted BLACK. Value and
	// coverage must disagree -- that is exactly what colorkeying index 0 lost.
	for (int x = 0; x < 8; ++x) {
		const bool painted = (x >= 2 && x <= 5);
		EXPECT_EQ(mask[x] != 0, painted) << "row 0 x=" << x;
		EXPECT_EQ(px[x], 0) << "row 0 x=" << x;   // every value here is 0
	}

	// Row 1: x=0..2 painted with real values, x=3..7 untouched.
	EXPECT_EQ(px[8], 7);
	EXPECT_EQ(px[9], 8);
	EXPECT_EQ(px[10], 9);
	for (int x = 0; x < 8; ++x)
		EXPECT_EQ(mask[8 + x] != 0, x <= 2) << "row 1 x=" << x;
}

// operator[] must keep returning exactly the pixels it always did -- the mask
// is additive. Callers that don't want coverage information are unaffected.
TEST(BitmapMask, OperatorIndexStillReturnsThePixels) {
	auto data = makeScanlineBitmap();
	GRAPHICS::Bitmap bm(data);
	std::vector<uint8_t> mask;
	EXPECT_EQ(bm[0], bm.decodeScanlineMasked(0, mask));
}

} // namespace
