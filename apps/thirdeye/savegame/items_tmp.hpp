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

		// Backpack ids @+96..+125 (14 slots × 2 bytes) -- see equip[] for the
		// worn block. (Backpack ids correspond to PC.W:inventory[0..13].)

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
// After the character-records block, ITEMS.TMP holds a flat array of "object
// slots" indexed by SOP object id. Each slot is:
//   +0 u16 id
//   +2 u16 class    (0xFFFF means an empty/dead slot)
//   +4 N bytes      (the instance's static block; N depends on class)
// For empty slots N is 4 (a minimum-size placeholder). For real items N is
// the class's total `instanceStaticSize` (sum across the parent chain).
//
// The size lookup is provided by the caller: it returns the static-block
// length for a given class number. Implementations in production use the live
// `ObjectSystem`; tests pass a small table.

struct ItemRecord {
	uint16_t id = 0;
	uint16_t cls = 0xFFFF;
	std::vector<uint8_t> staticBlock; // exactly `lookup(cls)` bytes
};

using ClassStaticSize = std::function<uint32_t(uint16_t cls)>;

// Walk the item-object stream starting at `streamOff`. Returns one entry per
// non-empty slot (cls != 0xFFFF), in file order. Empty slots are skipped (we
// don't need to recreate them). The caller is responsible for `streamOff`;
// for the Quick Start Party save it is 6947 (0x1b23) = 677 + 10 * 627.
std::vector<ItemRecord> parseItemStream(const std::vector<uint8_t> &data,
                                        size_t streamOff,
                                        const ClassStaticSize &lookup);

} // namespace THIRDEYE::savegame

#endif // THIRDEYE_SAVEGAME_ITEMS_TMP_HPP
