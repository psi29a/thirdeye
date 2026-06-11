/*
 * daesop_tests.cpp
 *
 * Regression tests for daesop's AESOP/16 -> AESOP/32 bitmap converter
 * (apps/daesop/convert.cpp). Built as a separate executable from `runtests`
 * because daesop is its own app (C-style globals) and we don't want it in the
 * thirdeye test link.
 *
 * Issue #18: the converter mis-decoded a bitmap whose RLE span starts at
 * x >= 256 (seen in the Spanish localization of EOB3, bitmap 189 "Reward",
 * where the translated text reflowed a span to x = 283). The span header's X is
 * a 16-bit value with bit 15 flagging the line's last span; the old code read
 * X as a single byte and tested the flag with "== 0x80", so a nonzero high byte
 * of X both misplaced the span and defeated the line-end test -> RLE desync.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

// daesop's pure (buffer-in, buffer-out) converter. Declared here rather than
// via convert.hpp to avoid pulling in daesop's RES headers; the signature /
// linkage must match convert.cpp exactly.
extern unsigned char *getNewBitmapForOldBitmap(unsigned char *buf,
                                               unsigned int len, int *newlen);

namespace {

// Build a minimal AESOP/16 OLD-format bitmap: global header (u32 size, u16
// subpicture count, u32 offset table) + one subpicture (width x 1) whose single
// row holds one literal-pixel span starting at `spanX`.
std::vector<uint8_t> makeOldBitmap(int width, int spanX,
                                   const std::vector<uint8_t> &pixels) {
	std::vector<uint8_t> b;
	auto u16 = [&](int v) { b.push_back(v & 0xff); b.push_back((v >> 8) & 0xff); };
	auto u32 = [&](uint32_t v) {
		for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 0xff);
	};

	u32(0);            // total size (patched below)
	u16(1);            // subpicture count
	u32(10);           // subpicture[0] offset

	// subpicture 0 @ offset 10
	u16(width);        // width
	u16(1);            // height (one row)
	b.push_back(0);    // row y = 0
	// span header: 16-bit X, bit 15 = last span of the line
	int xword = (spanX & 0x7fff) | 0x8000;
	b.push_back(xword & 0xff);
	b.push_back((xword >> 8) & 0xff);
	b.push_back(static_cast<uint8_t>(pixels.size())); // rle_width
	b.push_back(0);                                   // rle_bytes (unused)
	// one copy token: mode 0 (copy), amount = (token>>1)+1 literal bytes
	b.push_back(static_cast<uint8_t>((static_cast<int>(pixels.size()) - 1) << 1));
	for (uint8_t p : pixels) b.push_back(p);
	b.push_back(0xff); // end of subpicture

	uint32_t sz = static_cast<uint32_t>(b.size());
	b[0] = sz & 0xff; b[1] = (sz >> 8) & 0xff;
	b[2] = (sz >> 16) & 0xff; b[3] = (sz >> 24) & 0xff;
	return b;
}

// Decode row 0 of a converted "1.10" bitmap into a width-length scanline
// (0 = transparent). Mirrors the token grammar in graphics/bitmap.cpp.
std::vector<uint8_t> decode110Row0(const unsigned char *d, int &width) {
	uint32_t ptr = d[8] | (d[9] << 8) | (d[10] << 16) |
	               (static_cast<uint32_t>(d[11]) << 24);
	int bx = d[ptr + 2] | (d[ptr + 3] << 8);
	width = bx + 1;
	std::vector<uint8_t> row(width, 0);
	uint32_t pos = ptr + 24; // past the 24-byte subpicture header
	int x = 0;
	for (;;) {
		uint8_t m = d[pos++];
		if (m == 0) break;                    // end of line
		if (m == 1) { x += d[pos++]; continue; } // skip transparent
		int amount = m >> 1;
		if (m & 1) {                          // string: literal pixels
			for (int i = 0; i < amount; ++i, ++x) {
				if (x < width) row[x] = d[pos];
				++pos;
			}
		} else {                              // run: repeated pixel
			uint8_t v = d[pos++];
			for (int i = 0; i < amount; ++i, ++x)
				if (x < width) row[x] = v;
		}
	}
	return row;
}

} // namespace

// The reported case: a span starting at x = 283 (>= 256). Must convert, and the
// decoded pixels must land at the right x.
TEST (Daesop_Bitmap, ConvertsSpanStartingPastByte) {
	std::vector<uint8_t> pixels = {1, 2, 3, 4, 5};
	std::vector<uint8_t> old = makeOldBitmap(/*width*/ 300, /*spanX*/ 283, pixels);

	int newlen = 0;
	unsigned char *out = getNewBitmapForOldBitmap(old.data(),
	                                              static_cast<unsigned>(old.size()),
	                                              &newlen);
	ASSERT_NE(out, nullptr) << "conversion failed -- the issue #18 RLE desync";
	ASSERT_GT(newlen, 0);
	EXPECT_EQ(out[0], '1'); EXPECT_EQ(out[1], '.');
	EXPECT_EQ(out[2], '1'); EXPECT_EQ(out[3], '0');

	int width = 0;
	std::vector<uint8_t> row = decode110Row0(out, width);
	EXPECT_EQ(width, 300);
	for (size_t i = 0; i < pixels.size(); ++i)
		EXPECT_EQ(row[283 + i], pixels[i]) << "pixel at x=" << (283 + i);
	EXPECT_EQ(row[0], 0);   // before the span is transparent
	EXPECT_EQ(row[282], 0);
	free(out);
}

// Sanity: the common case (span starting at x < 256) still converts.
TEST (Daesop_Bitmap, ConvertsNormalSpan) {
	std::vector<uint8_t> pixels = {7, 7, 7};
	std::vector<uint8_t> old = makeOldBitmap(/*width*/ 100, /*spanX*/ 10, pixels);

	int newlen = 0;
	unsigned char *out = getNewBitmapForOldBitmap(old.data(),
	                                              static_cast<unsigned>(old.size()),
	                                              &newlen);
	ASSERT_NE(out, nullptr);
	int width = 0;
	std::vector<uint8_t> row = decode110Row0(out, width);
	EXPECT_EQ(width, 100);
	EXPECT_EQ(row[10], 7); EXPECT_EQ(row[11], 7); EXPECT_EQ(row[12], 7);
	EXPECT_EQ(row[9], 0);
	free(out);
}
