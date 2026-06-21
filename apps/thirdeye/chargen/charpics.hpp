#ifndef THIRDEYE_CHARGEN_CHARPICS_HPP
#define THIRDEYE_CHARGEN_CHARPICS_HPP

// CHARGEN/CHARPICS.BMP -- the chargen portrait sheet.
//
// Despite the .BMP extension this is NOT a Windows BMP. RE'd against the
// bundled 82005-byte file (89 portraits):
//
//   u32 fileSize          // = 82005 (matches actual file size)
//   u16 ?                 // = 90  (could be width, but not used by portraits)
//   u16 ?                 // = 366 (the file offset where the offset table ends)
//   u16 ?                 // = 0
//   u32 offsets[N]        // table -- file offsets of each portrait's data
//                         // N is implicit = (offsets[0] - 10) / 4 ... but
//                         // actually we walk u32 LE values until they stop
//                         // looking like plausible monotonic offsets.
//
// Per-portrait header (at each offset):
//   u16 boundsx           // width-1  (always 0x001f = 31 -> 32 in the bundled file)
//   u16 boundsy           // height   (always 0x0020 = 32)
//   u16 reserved          // = 0
//   uint8_t data[]        // RLE-compressed pixel stream, format similar to but
//                         // not identical to the AESOP/16 "1.10" VFX shape
//                         // tokens (apps/thirdeye/graphics/bitmap.cpp).
//                         // Decoding is left to Phase 6b -- this reader
//                         // captures the raw bytes so callers can decode
//                         // later or pass to a future shared decoder.
//
// All 89 portraits are 32x32. The smallest data block (229 B for 14 of the 89)
// is almost certainly an "empty" / black portrait slot; the rest are 900-1100 B.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace THIRDEYE::chargen {

struct CharPic {
	uint16_t width  = 0;             // decoded from per-portrait sub-header
	uint16_t height = 0;
	std::vector<uint8_t> rleData;    // bytes after the 6-byte sub-header
};

struct CharPics {
	std::vector<CharPic> portraits;
	uint32_t declaredFileSize = 0;   // u32 @0; matches the on-disk size
};

// Parse a buffer holding CHARPICS.BMP. Stops cleanly at the first malformed
// entry; empty on totally bogus input.
CharPics parseCharPics(const std::vector<uint8_t> &data);

CharPics loadCharPics(const std::filesystem::path &path);

} // namespace THIRDEYE::chargen

#endif // THIRDEYE_CHARGEN_CHARPICS_HPP
