#ifndef THIRDEYE_CHARGEN_ITEMTYPE_DAT_HPP
#define THIRDEYE_CHARGEN_ITEMTYPE_DAT_HPP

// CHARGEN/ITEMTYPE.DAT — per-item-type metadata shipped beside CHGEN.EXE.
//
// Layout (verified against the bundled 1026-byte file):
//
//   ItemType records[N]   // 16 bytes each; N is implicit = (filesize - 2) / 16
//   u16 trailer           // = 0x0004 (purpose unknown)
//
// Total for bundled: 64 × 16 + 2 = 1026 ✓.  There is NO count header --
// the first u16 (record[0].kind) happens to be 0x40 = 64 = the record count
// in the bundled file, but that's coincidence, not a header.
//
// Per-record semantics are partly inferred (CHGEN.EXE xrefs are the source
// of truth -- TODO follow them from ../eob3_research/CHGEN/strings_xrefs.txt).
// Fields named `field*` below have a known position but uncertain meaning.

#include <cstdint>
#include <filesystem>
#include <vector>

namespace THIRDEYE::chargen {

struct ItemType {
	uint16_t kind;       // +0   item-type class code (often == record index OR a
	                     //      bit-flag pattern; the high bit on byte 1 seems
	                     //      to flag "carried / inventory-only" types)
	uint16_t width;      // +2   inventory icon width  (usually 8)
	uint16_t height;     // +4   inventory icon height (usually 8)
	uint16_t slotMask;   // +6   bitmask of allowed equipment slots (0x3900,
	                     //      0x003f, …) — to be cross-validated
	uint8_t  flag1;      // +8   1 / 0 — handedness?
	uint8_t  flag2;      // +9   mostly 1
	uint16_t field6;     // +10  usually 0x0008 (weight in 1/10 lb? icon?)
	uint16_t field7;     // +12  usually 0x0001
	uint16_t field8;     // +14  varies (0x000a, 0x000c, 0x0008, …) — value? cost?
};

struct ItemTypeDat {
	std::vector<ItemType> types;
	uint16_t trailer = 0;   // 0x0004 in the bundled file
};

// Parse a buffer holding ITEMTYPE.DAT contents.
ItemTypeDat parseItemTypeDat(const std::vector<uint8_t> &data);

// Read ITEMTYPE.DAT from disk.
ItemTypeDat loadItemTypeDat(const std::filesystem::path &path);

} // namespace THIRDEYE::chargen

#endif // THIRDEYE_CHARGEN_ITEMTYPE_DAT_HPP
