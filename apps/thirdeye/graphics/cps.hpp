#ifndef GRAPHICS_CPS_HPP
#define GRAPHICS_CPS_HPP

// Westwood Studios CPS image loader -- a 320x200 indexed full-screen image,
// stored either raw or LCW/Format80-compressed. Used for chargen backdrops
// (CHARGEN.CPS / CHARGENB.CPS) and elsewhere across the Westwood catalogue.
//
// File layout:
//   u16 fileSize          (entire file size, in bytes)
//   u16 compression       (0 = raw, 4 = LCW/Format80; we handle both)
//   u32 uncompressedSize  (typically 64000 = 320 * 200)
//   u16 paletteSize       (0 if no embedded palette; otherwise 768 = 256*3 6-bit)
//   u8  palette[paletteSize] (optional embedded VGA palette, 256 colours of 0..63)
//   u8  data[]             (compressed or raw indexed pixels)

#include <cstdint>
#include <filesystem>
#include <vector>

namespace GRAPHICS {

struct Cps {
	int width  = 320;
	int height = 200;
	std::vector<uint8_t> pixels;   // width * height bytes, palette-indexed
	std::vector<uint8_t> palette;  // empty if file had paletteSize == 0
};

// LCW (Westwood Format80) decompressor. Returns the decompressed stream
// truncated to `destSize`. Tolerates trailing garbage / missing terminator.
std::vector<uint8_t> decompressLCW(const uint8_t *src, size_t srcSize,
                                   size_t destSize);

// Load a .CPS file. Returns an empty Cps on failure (missing file / unknown
// compression / truncated stream).
Cps loadCps(const std::filesystem::path &path);

} // namespace GRAPHICS

#endif // GRAPHICS_CPS_HPP
