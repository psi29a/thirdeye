#include "itemtype_dat.hpp"

#include <fstream>
#include <iterator>

namespace THIRDEYE::chargen {

namespace {

constexpr size_t kRecSize = 16;

uint16_t readU16(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

} // namespace

ItemTypeDat parseItemTypeDat(const std::vector<uint8_t> &data) {
	ItemTypeDat out;
	if (data.size() < 2) return out;
	// Layout: N × 16-byte records, then u16 trailer (0x0004). No count
	// header in this file -- the count is implicit from the file size.
	// (Originally I wrote a parser that read a u16 count at offset 0 and
	// then 16-byte records from offset 2. It "worked" on the bundled file
	// only because record[0].kind happens to be 0x40 = 64 = the record
	// count. That's a coincidence; the real layout has no header.)
	if (data.size() < kRecSize + 2) return out; // need at least one record + trailer
	const size_t count = (data.size() - 2) / kRecSize;
	size_t off = 0;
	for (size_t i = 0; i < count; ++i) {
		if (off + kRecSize > data.size()) break;
		ItemType t;
		t.kind     = readU16(data, off + 0);
		t.width    = readU16(data, off + 2);
		t.height   = readU16(data, off + 4);
		t.slotMask = readU16(data, off + 6);
		t.flag1    = data[off + 8];
		t.flag2    = data[off + 9];
		t.field6   = readU16(data, off + 10);
		t.field7   = readU16(data, off + 12);
		t.field8   = readU16(data, off + 14);
		out.types.push_back(t);
		off += kRecSize;
	}
	if (off + 2 <= data.size())
		out.trailer = readU16(data, off);
	return out;
}

ItemTypeDat loadItemTypeDat(const std::filesystem::path &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
	                          std::istreambuf_iterator<char>());
	return parseItemTypeDat(data);
}

} // namespace THIRDEYE::chargen
