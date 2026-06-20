#ifndef THIRDEYE_SAVEGAME_SAVEGAME_DIR_HPP
#define THIRDEYE_SAVEGAME_SAVEGAME_DIR_HPP

// SAVEGAME.DIR — the save-slot menu list.
//
// 328 bytes of ASCII. Each slot is a fixed-length name region separated by
// `\r\n`, with unused slots filled by underscores (`_`). The file ends with
// `\x1a` (CP/M EOF). Per docs/eob3_savegame_format.md §1: the bundled
// "Quick Start Party" save has one used slot ("Quick Start Party") and the
// rest blank, ~21 slots total.
//
// This parser returns one entry per slot, with `name` set to the trimmed
// display name (empty for an underscore-only entry, so callers can render
// "[empty]" or similar).

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace THIRDEYE::savegame {

struct SaveDirEntry {
	std::string name;  // trimmed; "" for an unused slot
	bool used = false; // true iff name is non-empty after trimming
};

// Parse a buffer holding the contents of SAVEGAME.DIR. Strips the trailing
// `\x1a` if present.
std::vector<SaveDirEntry> parseSaveDir(const std::vector<unsigned char> &data);

// Read SAVEGAME.DIR from disk. Returns an empty vector if the file can't be
// opened.
std::vector<SaveDirEntry> loadSaveDir(const std::filesystem::path &path);

} // namespace THIRDEYE::savegame

#endif // THIRDEYE_SAVEGAME_SAVEGAME_DIR_HPP
