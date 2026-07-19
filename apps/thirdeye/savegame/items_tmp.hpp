#ifndef THIRDEYE_SAVEGAME_ITEMS_TMP_HPP
#define THIRDEYE_SAVEGAME_ITEMS_TMP_HPP

// EOB3 ITEMS.TMP — the main save (party position + character records + live
// item objects). Layout RE notes in docs/eob3_savegame_format.md §2.
//
// Two parsers in this header:
//   parseItemsTmp   — game-state header + character records (§2.1 / §2.2).
//   parseItemStream — live item-object stream (§2.3), variable-stride. Needs
//                     a class-size lookup callback since the per-record stride
//                     is the SOP class's total `instanceStaticSize` + 4 bytes
//                     of (id, class) header.
//
// Equipment slots in the EOB3 save layout are a packed 12-word block at
// PC+127 (body, bracers, rhand, lring, rring, boots, lhand, pouchA, pouchB,
// pouchC, necklace, helmet) -- NOT the same indices as the live PC class's
// W:inventory[26]. See docs/equipment_slots.md for the runtime mapping
// (W:inventory[14..25] = same 12 slots, in the same order).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace THIRDEYE::savegame {

struct ItemsTmp {
	struct Position {
		uint8_t x = 0;       // file +252
		uint8_t y = 0;       // file +253
		uint8_t facing = 0;  // file +254  (0=N, 1=E, 2=S, 3=W)
		uint8_t level = 0;   // file +255  (1..14)
	};

	struct Character {
		// PC record offsets are PC-class static offset + 18 (the file record
		// has an 18-byte header before the SOP static block starts). Cross-
		// referenced against EYE.RES res 1369 "PC"'s exported-variable table
		// (see docs/eob3_savegame_format.md §2.2 + .claude/skills/sop-debug).
		int16_t  objectIndex = -1; // +0   live SOP object id (32..)
		int16_t  classNumber = -1; // +2   1369 = "PC"

		// Spell-menu level state @+95..+98: B:sp_lvl[2] (PC+77) and
		// B:mn_lvl[2] (PC+79), one byte per caster type (0=mage, 1=cleric).
		// DOS saves carry 1s; without them the camp spell menu opens at
		// "Level 0: 0 of 0 Available".
		uint8_t  spLvl[2] = {1, 1};
		uint8_t  mnLvl[2] = {1, 1};

		// Backpack ids @+99..+126 (14 slots × 2 bytes, verbatim statics+18:
		// PC.W:inventory[0..13] @81) -- see equip[] for the worn block.
		// -1 = empty. Must be restored: a zero-filled slot reads as "item
		// object 0" and the inventory click handler happily picks it up.
		static constexpr int kBackpackSlots = 14;
		int16_t  backpack[kBackpackSlots] = {-1,-1,-1,-1,-1,-1,-1,
		                                     -1,-1,-1,-1,-1,-1,-1};

		// Equipment slots (item-object IDs; -1 = empty / 0xFFFF).
		// File offset +127. Indices: 0 body, 1 bracers, 2 rhand, 3 lring,
		// 4 rring, 5 boots, 6 lhand, 7 pouchA, 8 pouchB, 9 pouchC, 10 necklace,
		// 11 helmet. Same order as W:inventory[14..25] in the live PC.
		static constexpr int kEquipSlots = 12;
		int16_t  equip[kEquipSlots] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};

		int16_t  arrowsType = -1;  // +151 (PC.W:quiver  @133)
		int16_t  arrowsQty  = 0;   // +153 (PC.W:arrows  @135)
		std::string name;          // +155 NUL-terminated up to 20 (PC.B:name @137,20)

		uint8_t  race = 0;         // +175 (PC.B:race      @157)
		uint8_t  classes = 0;      // +176 (PC.B:classes   @158, multi-class bitfield)
		int16_t  portrait = 0;     // +177 (PC.W:portrait  @159)
		uint8_t  PCstat = 0;       // +179 (PC.B:PCstat    @161, status bits)
		uint8_t  alignment = 0;    // +180 (PC.B:alignment @162)
		uint8_t  levels[3] = {};   // +181 (PC.B:levels      @163,3, per-class level)
		uint8_t  lostLevels[3]={}; // +184 (PC.B:lost_levels @166,3, drained levels)
		int16_t  lostHp = 0;       // +187 (PC.W:lost_hp   @169)

		int16_t  hpCurrent = 0;    // +189 (PC.W:hpts      @171)
		int16_t  hpMax     = 0;    // +191 (PC.W:hmax      @173)
		int16_t  hbon = 0;         // +193 (PC.W:hbon      @175, HP per-level bonus)
		int16_t  foodPct   = 0;    // +195 (PC.W:food      @177, 0..100)

		// Per-class XP: classes[i] is the level table, xp[i] the experience.
		// xp[1] / xp[2] = 0xFFFFFFFF (-1) for single-class PCs.
		int32_t  xp[3] = {};       // +197 (PC.L:experience @179,3)

		uint8_t  str = 0;          // +209 (PC.B:str      @191)
		uint8_t  strPct = 0;       // +210 (PC.B:exc_str  @192, "18/94")
		uint8_t  intel = 0;        // +211 (PC.B:int      @193)
		uint8_t  wis = 0;          // +212 (PC.B:wis      @194)
		uint8_t  dex = 0;          // +213 (PC.B:dex      @195)
		uint8_t  con = 0;          // +214 (PC.B:con      @196)
		uint8_t  cha = 0;          // +215 (PC.B:cha      @197)

		// --- +216..+626 tail: 411 bytes of mostly-spell state ---
		// Named fields we've isolated within the tail:
		uint8_t  sparkle = 0;      // +216 (PC.B:sparkle      @198)
		int32_t  magicEffects = 0; // +217 (PC.L:magiceffects @199, active-effect bitfield)
		uint8_t  tiger = 0;        // +221 (PC.B:tiger        @203, tiger-transform state)
		uint8_t  lostStr = 0;      // +222 (PC.B:lost_str     @204, drained STR)
		// +223..+226 (4 bytes): unknown, varies per PC (Mikeal "02 ae 1d ed",
		// Stonebeard "00 0e 00 90"). Possibly a hash/seed or AC components.
		uint32_t unknownGap = 0;

		// Spell state: B:spell_cnt[200] @PC+209 -> file +227..+426
		// and B:spell_stat[200] @PC+409 -> file +427..+626. We don't decode
		// the slot layout (per-class × per-spell-level × per-slot is a lot of
		// state to RE), but we capture them as raw bytes so write_initial_tempfiles
		// + resume_level can round-trip them losslessly.
		std::vector<uint8_t> spellCnt;   // 200 bytes
		std::vector<uint8_t> spellStat;  // 200 bytes
	};

	Position position{};
	// Kernel option bytes from the same object-0 record (B:bar_graphs@247,
	// B:sounds@248 -- see kernel.dasm exported variables). Default 1, matching
	// the shipped QSP scaffold, for truncated/absent saves.
	uint8_t barGraphs = 1;
	uint8_t soundsOn = 1;
	// Up to 10 character records: 4 party + 2 joined NPCs + 4 reserve slots.
	// Empty slots have classNumber=0 (and an empty name); the caller should
	// gate on classNumber == 1369 to act on live PC records only.
	std::vector<Character> characters;
	// Byte offset where the live item-object stream (§2.3) begins; pass to
	// parseItemStream. = kPcRecordBase + characters.size() * kPcStride.
	size_t itemStreamOff = 0;
};

// Parse a buffer holding the contents of ITEMS.TMP. Stops at the first
// character record that runs past the end of the buffer (so a truncated file
// yields a partial parse rather than a crash).
ItemsTmp parseItemsTmp(const std::vector<uint8_t> &data);

// Read ITEMS.TMP from disk. Returns an empty ItemsTmp (characters.empty()) if
// the file can't be opened.
ItemsTmp loadItemsTmp(const std::filesystem::path &path);

// --- §2.3 item-object stream ---
//
// ITEMS.TMP is one native `save_range` CDESC stream end-to-end (see
// RTOBJECT.C/RTOBJECT.H): byte 0 = 0x1A magic, then one record per object
// slot 0..999, each
//   +0 u16 slot
//   +2 u32 name     (SOP class number; 0xFFFFFFFF = empty/dead slot)
//   +6 u16 size     (static-block byte count; 0 for empty slots)
//   +8 `size` bytes (the instance's statics, whole parent chain, verbatim)
// The §2.1 kernel record (slot 0), §2.2 PC records (slots 32..41) and the
// §2.3 item pool are all just records in this one stream; the §2.2 field
// offsets in parseItemsTmp already bake in the 8-byte header.
//
// HISTORICAL NOTE (fixed 2026-07-19): this parser used to assume a 4-byte
// {id, cls} header + class-size lookup + "4-byte trailer", which read every
// static block 4 bytes early and dropped the real last 4 bytes (the tail of
// the entity/arms fields). All the "trailer semantics" RE (magical bonus in
// byte 3, itmflags arriving as 0xFFFF, "owner lives at B:lvl@4") were
// artifacts of that shift. True frame: items.W:place@0 = holder object id,
// or -1 with B:x/B:y/B:lvl set for items lying on a dungeon floor.

struct ItemRecord {
	uint16_t id = 0;
	uint16_t cls = 0xFFFF;
	std::vector<uint8_t> staticBlock; // CDESC `size` bytes, verbatim
};

// Walk the CDESC stream starting at `streamOff`. Returns one entry per
// non-empty slot (name != 0xFFFFFFFF), in file order. Empty slots are skipped
// (we don't need to recreate them) but ARE reported through `coveredSlots`
// when provided: every slot id the stream explicitly mentions -- live or
// empty -- is appended. The caller uses that to distinguish "this slot is
// empty because the player consumed the item" (explicit empty record; do NOT
// gap-fill it from ITEMS_00.BIN) from "this save predates full-array
// serialization" (slot absent from the stream entirely; gap-fill is the
// right recovery). The caller is responsible for `streamOff`; for the Quick
// Start Party save it is 6947 (0x1b23) = 677 + 10 * 627.
std::vector<ItemRecord> parseItemStream(const std::vector<uint8_t> &data,
                                        size_t streamOff,
                                        std::vector<uint16_t> *coveredSlots = nullptr);

// --- Slot-backup restoration -----------------------------------------
//
// Both helpers below copy SAVEGAME/<NAME>_<NN>.BIN -> SAVEGAME/<NAME>.TMP
// using `copy_file(overwrite_existing)`. Two safety invariants the SOP's
// Restore-Game picker depends on:
//   1. **Fail fast on missing source.** If <NAME>_<NN>.BIN doesn't exist
//      (empty slot / bogus index), do NOT touch the destination TMP. The
//      user's currently-loaded state must survive a botched restore. (An
//      earlier pre-delete-then-copy version destroyed live state when the
//      picker fired against an empty slot -- see the bug report that
//      added this helper.)
//   2. **Don't create empty TMPs.** `copy_file` is the only write op;
//      either it succeeds (replacing the target atomically-ish) or the
//      target is left as-is.
//
// `restoreItems` returns true on success, false on missing-source.
// `restoreLevels` returns the number of LVL??.TMP files actually copied
// (0..14); a slot whose ITEMS_NN.BIN is absent short-circuits to 0
// without touching any LVL??.TMP.
bool restoreItems(const std::filesystem::path &saveDir, int slotIdx);
int  restoreLevels(const std::filesystem::path &saveDir, int slotIdx);

// --- Native ITEMS_00.BIN CDESC parsing (area-class instances) ---
//
// The "real" ITEMS save format is the native AESOP `save_range`/`restore_range`
// CDESC stream: byte 0 = 0x1A (binary magic), then 1000 records (one per object
// slot in [FIRST_ITEM..LAST_ITEM] = 0..999), each `{u16 slot, u32 class,
// u16 size, size_bytes data}`. CDESC slots with class=0xFFFFFFFF are empty.
//
// Of particular interest: slots 1..14 hold one **area-class instance per
// dungeon level** (e.g. slot 1 = class 2409 "mauslvl1", slot 3 = class 2415
// "graveyrd", etc.). These are the singletons that dungeon's SOP "init level"
// handler looks up via `SOLE` to drive `SEND "enter level"` -- which calls
// `set_palette` for walls + PAL_M1 + PAL_M2 *from the bytecode*, not from C++.
//
// Without these instances live in the object table, "init level" finds no area
// object and the creature palette load never fires. So at boot/load time we
// pre-create them by reading ITEMS_00.BIN's CDESC records for slots 0..14 and
// instantiating each (with its saved static data restored verbatim).
//
// Returns the count of objects created. saveDir is the SAVEGAME/ directory.
// The `create` callback receives (objIndex, classNumber, staticData) -- the
// caller wires it to ObjectSystem::createProgram + classStaticPtr writes.
//
// The slot range is [firstSlot, lastSlot] inclusive. Defaults to (1, 14) --
// just the area singletons. Widen to (1, 999) to also pre-create the world
// item pool (potions, scrolls, weapons, quest items — the slots referenced by
// niche.W:contents, monster.W:carried, chest contents, etc.). Callback should
// skip slots that are already live; the raw record still gets scanned but no
// object is replaced.
using CdescCreate = std::function<void(int slot, uint16_t cls,
                                       const std::vector<uint8_t> &data)>;
int loadAreaInstances(const std::filesystem::path &saveDir,
                      const CdescCreate &create,
                      int firstSlot = 1, int lastSlot = 14);

} // namespace THIRDEYE::savegame

#endif // THIRDEYE_SAVEGAME_ITEMS_TMP_HPP
