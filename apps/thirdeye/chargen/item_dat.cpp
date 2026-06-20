#include "item_dat.hpp"

#include <fstream>
#include <iterator>

namespace THIRDEYE::chargen {

namespace {

constexpr size_t kItemRecSize = 14;
constexpr size_t kNameLen     = 35;

uint16_t readU16(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
}

int16_t readI16(const std::vector<uint8_t> &d, size_t off) {
	return static_cast<int16_t>(readU16(d, off));
}

std::string readFixedString(const std::vector<uint8_t> &d, size_t off, size_t cap) {
	std::string s;
	s.reserve(cap);
	for (size_t i = 0; i < cap && off + i < d.size(); ++i) {
		uint8_t b = d[off + i];
		if (b == 0) break;
		s.push_back(static_cast<char>(b));
	}
	return s;
}

} // namespace

const std::string &ItemDat::nameOf(int idx) const {
	static const std::string empty;
	if (idx < 0 || static_cast<size_t>(idx) >= names.size()) return empty;
	return names[idx];
}

ItemDat parseItemDat(const std::vector<uint8_t> &data) {
	ItemDat out;
	if (data.size() < 2) return out;

	const size_t numItems = readU16(data, 0);
	size_t off = 2;
	for (size_t i = 0; i < numItems; ++i) {
		if (off + kItemRecSize > data.size()) break; // truncated
		Item it;
		it.unid   = data[off + 0];
		it.id     = data[off + 1];
		it.bits   = data[off + 2];
		it.pic    = data[off + 3];
		it.type   = data[off + 4];
		it.subpos = data[off + 5];
		it.pos    = readI16(data, off + 6);
		it.next   = readI16(data, off + 8);
		it.prev   = readI16(data, off + 10);
		it.level  = data[off + 12];
		it.value  = static_cast<int8_t>(data[off + 13]);
		out.items.push_back(it);
		off += kItemRecSize;
	}

	if (off + 2 > data.size()) return out; // no name table -> done
	const size_t numNames = readU16(data, off);
	off += 2;
	for (size_t i = 0; i < numNames; ++i) {
		if (off + kNameLen > data.size()) break;
		out.names.push_back(readFixedString(data, off, kNameLen));
		off += kNameLen;
	}
	return out;
}

ItemDat loadItemDat(const std::filesystem::path &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
	                          std::istreambuf_iterator<char>());
	return parseItemDat(data);
}

} // namespace THIRDEYE::chargen
