#ifndef THIRDEYE_SAVEGAME_TRANSFER_HPP
#define THIRDEYE_SAVEGAME_TRANSFER_HPP

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace THIRDEYE::savegame {

// State for the char-gen party transfer (the `xfer` SOP object's
// "convert created party"/"transfer" handlers).
//
// open_transfer_file buffers CHARGEN\CREATE.SAV (the party CHGEN.EXE wrote);
// player_attrib/item_attrib read fields back out of it.
//
// CREATE.SAV layout (reverse-engineered from the default party Bob/Carol/Ted/
// Alice): a short header, then four fixed 345-byte PC records starting at 0x16.
// player_attrib(pc, attr, size) reads `size` little-endian bytes at file offset
// kPcBase + pc*kPcStride + attr (attr is the byte offset within the record's
// attribute area, which begins just past the 11-byte name -- so e.g. attr 2 is
// the first ability score). The bytecode just copies these bytes into the PC
// object's statics, so returning the right bytes reconstructs the real party.
//
// See docs/equipment_slots.md for the body-part slot map and EOB1 type -> EOB3
// class table this struct uses for typed item placement.
struct TransferState {
	std::vector<uint8_t> data; // CREATE.SAV contents (empty => not open)

	// PC-record offsets. The record starts at 0x16 and `attr` is a direct byte
	// offset into it biased by +2 (attr 2 = the first byte = name[0]): the name-
	// copy loop reads attr 2..12 -> name[0..10] ("Bob\0..."), confirming the bias.
	// So kPcBase = 0x16 - 2 = 20.
	static constexpr int kPcBase = 0x16 - 2;
	static constexpr int kPcStride = 345;

	// --- inventory / starting gear (CREATE.SAV item array) ---
	// A PC's inventory is a row of 26 word slots at record offset 219 holding item
	// ids (0 = empty). The party's items live in an EOB1-format item array near the
	// end of CREATE.SAV: 14-byte records (unid,id,bits,pic,type,subpos,pos,next,
	// prev,level,value), the char-gen's ids running from 434 (= ITEM.DAT's item
	// count) at file 0x894. item_attrib(pc, slot, attr) resolves slot->id->record:
	// attr 1 = type (+4, matched against the xfer's table123 type->object map),
	// attr 0 = bits/flags (+2, -> itmflags), attr 2 = value (+13, signed -> bonus).
	static constexpr int kInvRecOff = 219;       // PC inventory slots (words)
	static constexpr int kInvSlots = 26;
	static constexpr int kItemArrayBase = 0x894; // party item records in CREATE.SAV
	static constexpr int kItemIdBase = 434;      // first party item id (ITEM.DAT count)
	static constexpr int kItemRecSize = 14;

	// EOB3 W:inventory[26]: 0..13 backpack, 14..25 worn equipment. See
	// docs/equipment_slots.md for the body-part assignments (14=body, 15=bracers,
	// 16=right hand, 17/18=rings, 19=boots, 20=left hand, 21..23=pouches,
	// 24=necklace, 25=helmet). CREATE.SAV's chargen row has no consistent
	// body-part order (Bob's slot 0=robe, Carol's slot 0=holy symbol), so any
	// sensible placement has to classify by item *type* and pick a body slot for
	// that category. pcItemAtSlot[pc][eob3] is the CREATE.SAV slot whose item
	// goes into EOB3 W:inventory[eob3], or -1 for empty. Filled by
	// rebuildPlacement() the moment CREATE.SAV is loaded.
	int8_t pcItemAtSlot[4][kInvSlots] = {};

	// EOB1 type -> EOB3 class. xfer SOP's table123 (LONG pairs at code offset
	// 123) maps each EOB1 type to obj_id; the EOB3 class is 1280 + obj_id. Pairs
	// with obj_id 0 are special-cased (no object created). See
	// docs/equipment_slots.md §4 for the verified table.
	static int table123Lookup(int type);

	enum class SlotCat : uint8_t {
		NONE, WEAPON, WEAPON_2H, RANGED, AMMO, BODY, HELMET,
		SHIELD, BOOTS, RING, BRACERS, NECKLACE, CARRIED
	};

	static SlotCat categoryForClass(int cls);

	// EOB2 save sniff (for the CHARCOPY stand-in): EOB2 saves carry a 20-byte
	// save-name header with a 00 01 save-valid flag at 0x14-0x15; EOB1 puts that
	// flag at 0x00-0x01 instead (records start at 0x02, 243-byte stride -- a
	// layout the xfer SOP's offsets can't read, which is why the original
	// CHARCOPY refuses EOB1 saves too).
	static bool looksLikeEob2Save(const uint8_t *hdr, size_t len) {
		return len >= 0x16 && hdr[0x14] == 0 && hdr[0x15] == 1;
	}

	// Read `size` (1/2/4) little-endian bytes for player `pc`, attribute `attr`.
	int32_t playerAttrib(int pc, int attr, int size) const;

	int readItemTypeAtCsSlot(int pc, int csSlot) const;

	// Walk each PC's 26 CREATE.SAV slots in order. For each non-empty item:
	// resolve class via table123, classify into a body-part category, pick the
	// first free EOB3 slot from that category's preference list, else spill to
	// the first free backpack slot 0..13. Drop on overflow.
	void rebuildPlacement();

	int32_t itemAttrib(int pc, int slot, int attr) const;
};

} // namespace THIRDEYE::savegame

#endif // THIRDEYE_SAVEGAME_TRANSFER_HPP
