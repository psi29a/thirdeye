/*
 * RLE decoding technique from Andreas Larsson (Jackasser)
 */

#include "bitmap.hpp"

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

	auto rd32 = [&](size_t off) -> uint32_t {
		return vec[off] | (vec[off + 1] << 8) | (vec[off + 2] << 16) |
		       (static_cast<uint32_t>(vec[off + 3]) << 24);
	};

	if (mIsVFXShape) {
		mNumSubBitmaps = static_cast<uint16_t>(rd32(4));
		for (uint16_t i = 0; i < mNumSubBitmaps; i++) {
			size_t entry = 8 + static_cast<size_t>(i) * 8;
			if (entry + 4 <= vec.size())
				mBitmapOffets[i] = rd32(entry);
		}
		return;
	}

	// CHARGEN/CHARPICS.BMP-style portrait sheet: the first u32 LE is the file
	// size, then u16×3 reserved fields, then a u32 offset table.
	//
	// We confirm the format with THREE checks (the single `rd32(0)==size`
	// check would mis-fire on an older-format file that happens to have
	// `unknown1 == 0` and `fileSize_u16 == actual_size`):
	//   1. u32 fileSize matches actual file size.
	//   2. The would-be first directory entry at +10 is a plausible offset
	//      (past the 10-byte header, within the file).
	//   3. The would-be second entry at +14 is strictly greater than the
	//      first (the table is monotonic ascending).
	// Two monotonic plausible u32 offsets right after the header is a strong
	// CHARPICS signature; OLDER-format files have raw image data there, not
	// a sorted offset table.
	auto looksLikeCharPicsDir = [&]() {
		if (vec.size() <= 18) return false;
		if (rd32(0) != vec.size()) return false;
		uint32_t o0 = rd32(10);
		uint32_t o1 = rd32(14);
		return o0 > 10 && o0 < vec.size() && o1 > o0 && o1 < vec.size();
	};
	if (looksLikeCharPicsDir()) {
		mIsCharPics = true;
		mNumSubBitmaps = 0; // ctor doesn't default-init this; explicit reset
		// Walk monotonically-increasing u32 offsets starting at +10 until they
		// stop making sense (a sentinel of sorts; the table ends where the
		// first portrait's data begins).
		constexpr size_t kHdr = 10;
		uint32_t lastOff = 0;
		size_t pos = kHdr;
		// Each portrait starts with a 6-byte sub-header (boundsx, boundsy,
		// reserved); require that the offset leaves room for at least that
		// header so the later getWidth/getHeight + decode path can't run off
		// the end on a malformed/truncated file.
		constexpr size_t kSubHdr = 6;
		while (pos + 4 <= vec.size()) {
			uint32_t v = rd32(pos);
			if (v <= lastOff) break;
			if (v < pos + 4) break;
			if (static_cast<size_t>(v) + kSubHdr > vec.size()) break;
			mBitmapOffets[mNumSubBitmaps++] = v;
			lastOff = v;
			pos += 4;
		}
		return;
	}

	mNumSubBitmaps = *reinterpret_cast<const uint16_t*>(&vec[2 * 2]);
	for (uint16_t i = 0; i < mNumSubBitmaps; i++) {
		mBitmapOffets[i] = *reinterpret_cast<const uint16_t*>(&vec[6 + i * 4]);
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
	uint32_t off = mBitmapOffets[index];
	// "1.10" subpicture header: boundsy (height-1) at +0, boundsx (width-1) at
	// +2. CHARPICS sub-header: boundsx (width-1) at +0, boundsy (height) at
	// +2. Older format: width at +0. (All little-endian u16.)
	if (mIsVFXShape)
		return *reinterpret_cast<const uint16_t*>(&mBitmapData[off + 2]) + 1;
	if (mIsCharPics)
		return *reinterpret_cast<const uint16_t*>(&mBitmapData[off]) + 1;
	return *reinterpret_cast<const uint16_t*>(&mBitmapData[off]);
}

uint16_t GRAPHICS::Bitmap::getHeight(uint16_t index) {
	if (!inRange(index, mNumSubBitmaps))
		throw std::out_of_range("Bitmap::getHeight: shape index " +
		                        std::to_string(index) + " >= count " +
		                        std::to_string(mNumSubBitmaps));
	uint32_t off = mBitmapOffets[index];
	if (mIsVFXShape)
		return *reinterpret_cast<const uint16_t*>(&mBitmapData[off]) + 1;
	if (mIsCharPics)
		return *reinterpret_cast<const uint16_t*>(&mBitmapData[off + 2]);
	return *reinterpret_cast<const uint16_t*>(&mBitmapData[off + 2]);
}

// Decode one shape of a CHARGEN/CHARPICS.BMP-style portrait sheet into an
// 8-bit (palette-indexed) width*height buffer, 0 = transparent.
//
// Per-shape layout (verified against all 89 portraits in the bundled file):
//   +0..1   u16 boundsx       (= width - 1)
//   +2..3   u16 boundsy       (= height)
//   +4..5   u16 reserved      (= 0)
//   +6      0x80 0x1f                  --> row-0 marker (implicit row index)
//   +8..    row-0 byte stream
//   ...     <row+1>0x00 0x80 0x1f      --> row N marker (4 bytes; row idx 1..H-1)
//           row-N byte stream
//   end     0xff (often) at EOF        --> shape terminator
//
// Per-row byte stream (verified for all 89 portraits): each row starts with
// a u8 N = pixel-byte count, followed by N literal palette-index bytes. The
// `1 + N` total length lines up exactly with the bytes between consecutive
// row markers for every row of every portrait.
//
// What's NOT yet RE'd: the pixels' horizontal *positioning*. Visual inspection
// of the rendered silhouettes (face-shape contour: narrow at top, widening
// through forehead/face, full-width at chin/shoulders) makes centered
// alignment the obvious read, but the data itself contains no explicit X-
// offset and we haven't pinned whether the CHGEN drawer truly centers or
// uses some byte we're treating as a literal pixel as a position field. The
// "empty" portraits encode every row as exactly `02 3d 00` (N=2, two literal
// "background" pixels) which gives us no positioning signal either.
//
// We center the row's N pixels in the W-wide canvas, which produces correct
// portrait silhouettes for the cases we've verified. If a future RE pass
// finds an explicit offset byte, this is the only line to change.
std::vector<uint8_t> GRAPHICS::Bitmap::decodeCharPicsShape(uint16_t index) {
	uint32_t base = mBitmapOffets[index];
	uint16_t width  = getWidth(index);
	uint16_t height = getHeight(index);
	std::vector<uint8_t> bitmap(static_cast<size_t>(width) * height, 0);

	const uint32_t end = static_cast<uint32_t>(mBitmapData.size());
	uint32_t pos = base + 6; // skip the 6-byte per-shape sub-header
	// Implicit row-0 marker: 0x80 0x1f. Skip it if present.
	if (pos + 2 <= end && mBitmapData[pos] == 0x80 && mBitmapData[pos + 1] == 0x1f)
		pos += 2;

	for (uint16_t y = 0; y < height; ++y) {
		// Find this row's end = the start of the next row's 4-byte marker
		// `<y+1>0x00 0x80 0x1f`. Last row goes to end of shape (or EOF).
		uint32_t rowEnd = end;
		uint32_t shapeEnd = (index + 1 < mNumSubBitmaps)
		                        ? mBitmapOffets[index + 1] : end;
		if (rowEnd > shapeEnd) rowEnd = shapeEnd;
		uint8_t nextRow = static_cast<uint8_t>(y + 1);
		for (uint32_t i = pos; i + 3 < rowEnd; ++i) {
			if (mBitmapData[i] == nextRow && mBitmapData[i + 1] == 0x00 &&
			    mBitmapData[i + 2] == 0x80 && mBitmapData[i + 3] == 0x1f) {
				rowEnd = i;
				break;
			}
		}
		if (pos < rowEnd) {
			uint8_t n = mBitmapData[pos];
			uint32_t avail = static_cast<uint32_t>(rowEnd - (pos + 1));
			uint32_t count = n < avail ? n : avail;     // defensive clamp
			if (count > width) count = width;
			// Center the row's N pixels in the W-wide canvas.
			uint32_t start = (width > count) ? (width - count) / 2 : 0;
			for (uint32_t i = 0; i < count; ++i) {
				size_t x = static_cast<size_t>(start + i);
				if (x < width)
					bitmap[static_cast<size_t>(y) * width + x] =
					    mBitmapData[pos + 1 + i];
			}
		}
		// Advance past the row's data + the 4-byte marker.
		pos = rowEnd + 4;
		if (pos > end) break;
	}
	nextBitmapPos = 0;
	return bitmap;
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
	uint32_t base = mBitmapOffets[index];
	uint16_t width = getWidth(index);
	uint16_t height = getHeight(index);
	std::vector<uint8_t> bitmap(static_cast<size_t>(width) * height, 0);

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
						bitmap[static_cast<size_t>(y) * width + x] = px;
				}
			} else {                    // run: one pixel repeated
				if (pos >= end) break;
				uint8_t px = mBitmapData[pos++];
				for (uint32_t i = 0; i < amount; i++, x++)
					if (x < width)
						bitmap[static_cast<size_t>(y) * width + x] = px;
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
	if (mIsCharPics)
		return decodeCharPicsShape(index);

	uint32_t pos = mBitmapOffets[index] + 4;	// skip over width and height
	std::vector<uint8_t> bitmap(static_cast<size_t>(getWidth(index)) * getHeight(index));
	memset(&bitmap[0], 0, static_cast<size_t>(getWidth(index)) * getHeight(index));

	while (true) {
		int32_t y = mBitmapData[pos];
		if (y == 0xff)
			break;

		if ((y < 0) || (y >= getHeight(index))) {
			throw std::runtime_error(
					"Bitmap RLE decode out of sync: y-coord " + std::to_string(y) +
					" outside height " + std::to_string(getHeight(index)));
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
					memcpy(&bitmap[0] + x + y * getWidth(index),
							&mBitmapData[0] + pos, amount);
					pos += amount;
				} else if (mode == 1)	// Fill
						{
					int value = mBitmapData[pos];
					pos++;
					memset(&bitmap[0] + x + y * getWidth(index), value, amount);
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
