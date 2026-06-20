#ifndef THIRDEYE_CHARGEN_ITEM_DAT_HPP
#define THIRDEYE_CHARGEN_ITEM_DAT_HPP

// CHARGEN/ITEM.DAT — base item template DB shipped beside CHGEN.EXE.
// Format fully RE'd in docs/create_sav_and_item_format.md §1; this is the
// C++ reader.
//
//   u16 numItems = 434
//   Item items[434]           (14 bytes each, EOB1 item layout)
//   u16 numNames = 123
//   char names[123][35]       (fixed 35-byte strings, NUL-padded)
//
// Phase 6a of the chargen-replacement effort (see docs/roadmap.md). This
// reader is a pure data layer -- no SDL, no ObjectSystem; it can be linked
// into a stand-alone EOB1/2/3 item inspector tool too.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace THIRDEYE::chargen {

// One 14-byte item record from ITEM.DAT (also the in-file format used in
// CREATE.SAV's party item array and in EOB1's `EOBDATA.SAV`).
struct Item {
	uint8_t  unid   = 0;   // +0 name-string index when unidentified
	uint8_t  id     = 0;   // +1 name-string index when identified
	uint8_t  bits   = 0;   // +2 flags: 0x80 glow/magic, 0x40 identified,
	                       //          0x20 cursed, 0x08 life-drain
	uint8_t  pic    = 0;   // +3 inventory icon number
	uint8_t  type   = 0;   // +4 item type (table123 key -- see transfer.cpp)
	uint8_t  subpos = 0;   // +5 placement (floor/wall 0..7, inv slot 0..26)
	int16_t  pos    = 0;   // +6 position = x + y*32; <=0 = consumed
	int16_t  next   = 0;   // +8 next item index in linked list
	int16_t  prev   = 0;   // +10 prev item index
	uint8_t  level  = 0;   // +12 dungeon level
	int8_t   value  = 0;   // +13 value / kind code; -1 = consumed
};

struct ItemDat {
	std::vector<Item>        items;  // up to ~434 entries (file declares count)
	std::vector<std::string> names;  // up to ~123 names; trimmed of NUL padding

	// Look up an item's display name. `unid` (or `id`) is the index used in
	// the item's name fields. Out-of-range returns "".
	const std::string &nameOf(int idx) const;
};

// Parse a buffer holding ITEM.DAT contents. Returns an empty ItemDat if the
// header is malformed or the file is truncated; partial parses populate as
// much as fits.
ItemDat parseItemDat(const std::vector<uint8_t> &data);

// Read ITEM.DAT from disk; returns empty on I/O failure.
ItemDat loadItemDat(const std::filesystem::path &path);

} // namespace THIRDEYE::chargen

#endif // THIRDEYE_CHARGEN_ITEM_DAT_HPP
