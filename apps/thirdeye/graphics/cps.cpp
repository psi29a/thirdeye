#include "cps.hpp"

#include <fstream>
#include <iterator>

namespace GRAPHICS {

// Westwood LCW (a.k.a. Format80). Four command families, dispatched on the
// top two bits of the first byte:
//   0xxxxxxx + 1 byte    : short relative copy from output (3..10 bytes,
//                          12-bit back-offset 1..4095). Length = ((c>>4)&7)+3,
//                          offset = ((c & 0x0F) << 8) | next.
//   10xxxxxx             : copy literal bytes (0..63). 0x80 = end of stream.
//   11xxxxxx (cmd<0xFE)  : medium copy from absolute output (3..62 bytes).
//                          Length = (c & 0x3F) + 3, position = next u16 LE.
//   11111110 (0xFE)      : large fill. count u16 LE, then 1-byte colour.
//   11111111 (0xFF)      : large copy from absolute output.
//                          count u16 LE, position u16 LE.
// Self-overlapping copies (a run that reads from the just-written tail) are
// required by the format -- we copy byte-by-byte through `dest` to honour them.
std::vector<uint8_t> decompressLCW(const uint8_t *src, size_t srcSize,
                                   size_t destSize) {
	std::vector<uint8_t> dest;
	dest.reserve(destSize);
	const uint8_t *end = src + srcSize;

	auto pushAt = [&](size_t pos) {
		if (pos < dest.size()) dest.push_back(dest[pos]);
		else dest.push_back(0); // defensive: malformed offset
	};

	while (src < end && dest.size() < destSize) {
		uint8_t c = *src++;
		if ((c & 0x80) == 0) {
			// short relative copy
			if (src >= end) break;
			uint8_t b1 = *src++;
			int count = ((c & 0x70) >> 4) + 3;
			int offset = ((c & 0x0F) << 8) | b1;
			for (int i = 0; i < count && dest.size() < destSize; ++i)
				pushAt(dest.size() - offset);
		} else if ((c & 0x40) == 0) {
			// literal copy or end-of-stream
			if (c == 0x80) break;
			int count = c & 0x3F;
			for (int i = 0; i < count && src < end && dest.size() < destSize; ++i)
				dest.push_back(*src++);
		} else if (c == 0xFE) {
			// large fill: u16 count, u8 colour
			if (src + 3 > end) break;
			uint16_t count = src[0] | (src[1] << 8);
			uint8_t colour = src[2];
			src += 3;
			for (uint16_t i = 0; i < count && dest.size() < destSize; ++i)
				dest.push_back(colour);
		} else if (c == 0xFF) {
			// large copy from absolute output: u16 count, u16 position
			if (src + 4 > end) break;
			uint16_t count = src[0] | (src[1] << 8);
			uint16_t pos   = src[2] | (src[3] << 8);
			src += 4;
			for (uint16_t i = 0; i < count && dest.size() < destSize; ++i)
				pushAt(pos + i);
		} else {
			// medium copy from absolute output: u16 position
			if (src + 2 > end) break;
			int count = (c & 0x3F) + 3;
			uint16_t pos = src[0] | (src[1] << 8);
			src += 2;
			for (int i = 0; i < count && dest.size() < destSize; ++i)
				pushAt(pos + i);
		}
	}
	return dest;
}

static std::vector<uint8_t> readFile(const std::filesystem::path &p) {
	std::ifstream f(p, std::ios::binary);
	if (!f) return {};
	return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
	                            std::istreambuf_iterator<char>());
}

Cps loadCps(const std::filesystem::path &path) {
	auto data = readFile(path);
	if (data.size() < 10) return {};

	uint16_t /*fileSize*/   _fileSize    = data[0] | (data[1] << 8);
	(void) _fileSize;
	uint16_t compression  = data[2] | (data[3] << 8);
	uint32_t uncompSize   = data[4] | (data[5] << 8) |
	                        (static_cast<uint32_t>(data[6]) << 16) |
	                        (static_cast<uint32_t>(data[7]) << 24);
	uint16_t paletteSize  = data[8] | (data[9] << 8);

	size_t off = 10;
	Cps out;
	if (paletteSize > 0) {
		// Header claimed a palette block but the file's too short to hold it
		// -- treat as malformed (don't return a partial Cps with no pixels).
		if (off + paletteSize > data.size()) return {};
		out.palette.assign(data.begin() + off,
		                   data.begin() + off + paletteSize);
		off += paletteSize;
	}

	if (compression == 0) {
		// Raw indexed pixels.
		if (off + uncompSize > data.size()) return {};
		out.pixels.assign(data.begin() + off, data.begin() + off + uncompSize);
	} else if (compression == 4) {
		out.pixels = decompressLCW(data.data() + off, data.size() - off,
		                            uncompSize);
	} else {
		// Unknown compression -- bail.
		return {};
	}

	// Dimensions are implicit at 320x200 for the chargen CPS files. Refuse a
	// short pixel buffer so callers don't have to defend against partial
	// decodes -- the chargen path memcpy-slices any non-empty result and a
	// short buffer would over-read.
	if (out.pixels.size() != static_cast<size_t>(uncompSize)) return {};
	out.width  = 320;
	out.height = 200;
	return out;
}

} // namespace GRAPHICS
