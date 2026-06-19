#include "savegame_dir.hpp"

#include <fstream>
#include <iterator>

namespace THIRDEYE::savegame {

namespace {

// Strip trailing `_` (unused-slot filler), spaces, and \0; an entry that
// reduces to nothing is "unused".
std::string trim(const std::string &s) {
	size_t end = s.size();
	while (end > 0) {
		char c = s[end - 1];
		if (c == '_' || c == ' ' || c == '\0') --end;
		else break;
	}
	return s.substr(0, end);
}

} // namespace

std::vector<SaveDirEntry> parseSaveDir(const std::vector<unsigned char> &data) {
	std::vector<SaveDirEntry> out;
	if (data.empty()) return out;

	// Drop the trailing \x1a (CP/M EOF) if present.
	size_t end = data.size();
	if (end > 0 && data[end - 1] == 0x1a) --end;

	// Walk \r\n-separated entries.
	std::string cur;
	for (size_t i = 0; i < end; ++i) {
		unsigned char c = data[i];
		if (c == '\r') continue;
		if (c == '\n') {
			SaveDirEntry e;
			e.name = trim(cur);
			e.used = !e.name.empty();
			out.push_back(std::move(e));
			cur.clear();
			continue;
		}
		cur.push_back(static_cast<char>(c));
	}
	// Tail entry without a trailing CRLF.
	if (!cur.empty()) {
		SaveDirEntry e;
		e.name = trim(cur);
		e.used = !e.name.empty();
		out.push_back(std::move(e));
	}
	return out;
}

std::vector<SaveDirEntry> loadSaveDir(const std::filesystem::path &path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return {};
	std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
	                                std::istreambuf_iterator<char>());
	return parseSaveDir(data);
}

} // namespace THIRDEYE::savegame
