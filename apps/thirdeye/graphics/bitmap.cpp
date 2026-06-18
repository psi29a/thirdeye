/*
 * RLE decoding technique from Andreas Larsson (Jackasser)
 */

#include "bitmap.hpp"

#include "components/misc/endian.hpp"

#include <mdspan>
#include <stdexcept>
#include <string>

GRAPHICS::Bitmap::Bitmap(const std::vector<uint8_t> &vec) {
	mBitmapData = vec;
	nextBitmapPos = 0;

	// AESOP/16 "1.10" VFX shape table (every EYE.RES bitmap): a 4-byte version
	// string "1.10", a u32 shape count, then a directory of <count> 8-byte
	// entries {u32 offset, u32 color}; each entry's offset points at that
	// sub-shape's 24-byte header. (The older format, used by the intro's GFF
	// frames, has no version string -- it's handled by the branch below.)
	mIsVFXShape = vec.size() >= 8 && vec[0] == '1' && vec[1] == '.' &&
	              vec[2] == '1' && vec[3] == '0';

	if (mIsVFXShape) {
		mNumSubBitmaps = static_cast<uint16_t>(Misc::loadLE<uint32_t>(&vec[4]));
		for (uint16_t i = 0; i < mNumSubBitmaps; i++) {
			size_t entry = 8 + static_cast<size_t>(i) * 8;
			if (entry + 4 <= vec.size())
				mBitmapOffets[i] = Misc::loadLE<uint32_t>(&vec[entry]);
		}
		return;
	}

	mNumSubBitmaps = Misc::loadLE<uint16_t>(&vec[4]);
	for (uint16_t i = 0; i < mNumSubBitmaps; i++) {
		mBitmapOffets[i] = Misc::loadLE<uint16_t>(&vec[6 + i * 4]);
	}
}

GRAPHICS::Bitmap::~Bitmap() {

}

bool GRAPHICS::Bitmap::isMoreBitmap(){
	return ((bool) nextBitmapPos);
}

uint32_t GRAPHICS::Bitmap::getNextBitmapPos(){
	return (nextBitmapPos);
}

uint16_t GRAPHICS::Bitmap::getNumberOfBitmaps() {
	return (mNumSubBitmaps);
}

// Bounds-check the index against mNumSubBitmaps. mBitmapOffets is a std::map,
// so an out-of-range `[index]` would default-construct an entry with value 0
// (the start of the bitmap's header bytes). On a "1.10" VFX shape that header
// reads as enormous bogus dimensions (boundsx/boundsy = "1." / "10" magic
// bytes -> ~12000 each), so a downstream decode allocates ~155 MB of garbage
// and burns 15+ seconds in the inner loop. The kernel hits this naturally:
// `set_pointer` calls `set_mouse_pointer(186, kernel.report(2000), ...)` and
// the default report(2000) returns 2000 -- an index Icons (~100 shapes) can't
// satisfy. Throwing here turns a multi-second silent hang into an instant,
// catchable error at the runtime-hook boundary.
static bool inRange(uint16_t index, uint16_t num) {
	return index < num;
}

uint16_t GRAPHICS::Bitmap::getWidth(uint16_t index) {
	if (!inRange(index, mNumSubBitmaps))
		throw std::out_of_range("Bitmap::getWidth: shape index " +
		                        std::to_string(index) + " >= count " +
		                        std::to_string(mNumSubBitmaps));
	uint32_t off = mBitmapOffets.at(index);
	// "1.10" subpicture header: boundsy (height-1) at +0, boundsx (width-1) at
	// +2. Older format: width at +0. (Both little-endian u16.)
	if (mIsVFXShape)
		return Misc::loadLE<uint16_t>(&mBitmapData[off + 2]) + 1;
	return Misc::loadLE<uint16_t>(&mBitmapData[off]);
}

uint16_t GRAPHICS::Bitmap::getHeight(uint16_t index) {
	if (!inRange(index, mNumSubBitmaps))
		throw std::out_of_range("Bitmap::getHeight: shape index " +
		                        std::to_string(index) + " >= count " +
		                        std::to_string(mNumSubBitmaps));
	uint32_t off = mBitmapOffets.at(index);
	if (mIsVFXShape)
		return Misc::loadLE<uint16_t>(&mBitmapData[off]) + 1;
	return Misc::loadLE<uint16_t>(&mBitmapData[off + 2]);
}

// Decode one sub-shape of an AESOP/16 "1.10" VFX shape table into an 8-bit
// (palette-indexed) width*height buffer, 0 = transparent. The shape data
// follows the 24-byte subpicture header; it is a per-line token stream (ref:
// daesop convert.cpp, the new-bitmap writers):
//   0          end of line (advance to the next row)
//   1          skip: next byte = count of transparent pixels
//   even >= 2  run:    amount = marker>>1, next byte = pixel repeated `amount`x
//   odd  >= 3  string: amount = marker>>1, then `amount` literal pixels
// There is exactly one end-of-line token per row, height rows in all.
std::vector<uint8_t> GRAPHICS::Bitmap::decodeVFXShape(uint16_t index) {
	uint32_t base = mBitmapOffets.at(index);
	uint16_t width = getWidth(index);
	uint16_t height = getHeight(index);
	std::vector<uint8_t> bitmap(static_cast<size_t>(width) * height, 0);

	// 2D view over the row-major palette-indexed buffer; lets us write fb[y, x]
	// instead of y*width+x and keeps the row stride implicit.
	std::mdspan<uint8_t, std::dextents<size_t, 2>> fb(bitmap.data(), height, width);

	uint32_t pos = base + 24; // skip the 24-byte subpicture header
	const uint32_t end = static_cast<uint32_t>(mBitmapData.size());

	for (uint16_t y = 0; y < height && pos < end; y++) {
		uint32_t x = 0;
		while (pos < end) {
			uint8_t marker = mBitmapData[pos++];
			if (marker == 0)            // end of this line
				break;
			if (marker == 1) {          // skip transparent pixels
				if (pos >= end) break;
				x += mBitmapData[pos++];
				continue;
			}
			uint32_t amount = marker >> 1;
			if (marker & 1) {           // string: literal pixels
				for (uint32_t i = 0; i < amount && pos < end; i++, x++) {
					uint8_t px = mBitmapData[pos++];
					if (x < width)
						fb[y, x] = px;
				}
			} else {                    // run: one pixel repeated
				if (pos >= end) break;
				uint8_t px = mBitmapData[pos++];
				for (uint32_t i = 0; i < amount; i++, x++)
					if (x < width)
						fb[y, x] = px;
			}
		}
	}
	nextBitmapPos = 0;
	return bitmap;
}

std::vector<uint8_t> GRAPHICS::Bitmap::operator[](uint16_t index) {
	if (!inRange(index, mNumSubBitmaps))
		throw std::out_of_range("Bitmap::operator[]: shape index " +
		                        std::to_string(index) + " >= count " +
		                        std::to_string(mNumSubBitmaps));
	if (mIsVFXShape)
		return decodeVFXShape(index);

	uint32_t pos = mBitmapOffets.at(index) + 4;	// skip over width and height
	uint16_t width = getWidth(index);
	uint16_t height = getHeight(index);
	std::vector<uint8_t> bitmap(static_cast<size_t>(width) * height, 0);
	std::mdspan<uint8_t, std::dextents<size_t, 2>> fb(bitmap.data(), height, width);

	while (true) {
		int32_t y = mBitmapData[pos];
		if (y == 0xff)
			break;

		if ((y < 0) || (y >= height)) {
			throw std::runtime_error(
					"Bitmap RLE decode out of sync: y-coord " + std::to_string(y) +
					" outside height " + std::to_string(height));
		}
		pos++;

		while (true) {
			int32_t x = mBitmapData[pos + 0]
					| ((mBitmapData[pos + 1] & 0x7f) << 8);
			int32_t islast = mBitmapData[pos + 1] & 0x80;
			int32_t rle_width = mBitmapData[pos + 2];
			//int rle_bytes = vec[pos+3];
			pos += 4;

			while (rle_width > 0) {
				int32_t mode = mBitmapData[pos] & 1;
				int32_t amount = (mBitmapData[pos] >> 1) + 1;
				pos++;

				if (mode == 0) {		// Copy
					memcpy(&fb[y, x], &mBitmapData[pos], amount);
					pos += amount;
				} else if (mode == 1)	// Fill
						{
					int value = mBitmapData[pos];
					pos++;
					memset(&fb[y, x], value, amount);
				}
				x += amount;
				rle_width -= amount;
			}

			if (rle_width != 0) {
				throw std::runtime_error(
						"Bitmap RLE decode out of sync (rle_width = " +
						std::to_string(rle_width) + ")");
			}

			if (islast == 0x80)
				break;
		}
	}


	if (pos+1 == mBitmapData.size()){
		//std::cout << "We're at the end!" << std::endl;
		nextBitmapPos = 0;
	}
	else {
		//std::cout << "Pos: " << pos << " size of file: " << mBitmapData.size() << std::endl;
		nextBitmapPos = pos+1;
	}


	return (bitmap);
}
