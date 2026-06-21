#include "charpics.hpp"

#include <fstream>
#include <iterator>

namespace THIRDEYE::chargen {

namespace {

constexpr size_t kHeaderBytes = 10;        // u32 fileSize + u16 + u16 + u16
constexpr size_t kSubHeaderBytes = 6;      // u16 boundsx + u16 boundsy + u16 reserved
constexpr size_t kTableEntryBytes = 4;     // u32 LE file offset per portrait

uint16_t readU16(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

uint32_t readU32(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<uint32_t>(d[off]) |
	       (static_cast<uint32_t>(d[off + 1]) << 8) |
	       (static_cast<uint32_t>(d[off + 2]) << 16) |
	       (static_cast<uint32_t>(d[off + 3]) << 24);
}

} // namespace

CharPics parseCharPics(const std::vector<uint8_t> &data) {
	CharPics out;
	if (data.size() < kHeaderBytes + kTableEntryBytes) return out;

	out.declaredFileSize = readU32(data, 0);

	// Walk the offset table starting at +10. Stop on:
	//  - a non-monotonic entry (table is sorted ascending),
	//  - an entry pointing outside the file,
	//  - an entry that would overlap the table itself.
	std::vector<uint32_t> offsets;
	size_t pos = kHeaderBytes;
	uint32_t lastOff = 0;
	while (pos + kTableEntryBytes <= data.size()) {
		uint32_t v = readU32(data, pos);
		if (v <= lastOff) break;
		if (v < pos + kTableEntryBytes) break;
		if (v >= data.size()) break;
		offsets.push_back(v);
		lastOff = v;
		pos += kTableEntryBytes;
	}

	// Decode each portrait's sub-header + capture its raw RLE bytes.
	for (size_t i = 0; i < offsets.size(); ++i) {
		size_t off = offsets[i];
		size_t end = (i + 1 < offsets.size()) ? offsets[i + 1] : data.size();
		if (off + kSubHeaderBytes > end) break;
		CharPic p;
		p.width  = readU16(data, off + 0) + 1;  // boundsx is width-1
		p.height = readU16(data, off + 2);       // boundsy is height
		// u16 @4 = reserved (== 0 across the bundled file)
		p.rleData.assign(data.begin() + off + kSubHeaderBytes,
		                 data.begin() + end);
		out.portraits.push_back(std::move(p));
	}
	return out;
}

CharPics loadCharPics(const std::filesystem::path &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
	                          std::istreambuf_iterator<char>());
	return parseCharPics(data);
}

} // namespace THIRDEYE::chargen
