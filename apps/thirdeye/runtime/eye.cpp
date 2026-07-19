#include "internal.hpp"

#include "../automap.hpp"
#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../savegame/items_tmp.hpp"
#include "../savegame/lvl_tmp.hpp"
#include "../savegame/savegame_dir.hpp"
#include "../savegame/transfer.hpp"
#include "../vm/objects.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <ostream> // for rt() << ... << std::endl (internal.hpp only fwd-decls)
#include <string>
#include <vector>

namespace THIRDEYE::runtime::eye {

// Savegame-picker slot-name buffer. The SOP picker passes whatever
// savegame_title returned to sprint/print, which calls back through the
// VM's readString -> ObjectSystem::staticsPtr. We expose this buffer at
// a sentinel obj id via ObjectSystem::setDynamicStaticsHook, so the SOP
// reads "Quick Start Party" (and friends) without us inventing a fake
// SOP object class.
namespace {
constexpr int kSaveSlots = 32;       // larger than the picker's 12-slot view
constexpr int kSlotNameLen = 32;     // padded name slot
constexpr uint16_t kSaveBufObj = 0x3FFE; // unused obj id; staticsPtr routes here
uint8_t gSlotNameBuf[kSaveSlots * kSlotNameLen] = {};

void ensureSlotNameHook(VM::ObjectSystem &objects) {
	// Install per-ObjectSystem instead of process-global: a second VM in the
	// same process (tests, future split-engine scenarios) would otherwise
	// share the "already installed" flag while having no hook of its own,
	// leaving savegame_title returns pointing into a dead-object path.
	// setDynamicStaticsHook is idempotent for the same target so re-installing
	// per boot is cheap and correct.
	objects.setDynamicStaticsHook([](int obj, uint32_t off,
	                                 uint32_t size) -> uint8_t* {
		if (obj != kSaveBufObj) return nullptr;
		// Compute against the buffer length as a single u64 to avoid the
		// `off + size` u32-wrap that could let an attacker-controlled offset
		// slip past the bounds check and return an OOB pointer.
		constexpr uint64_t kBufLen = sizeof(gSlotNameBuf);
		if (static_cast<uint64_t>(off) + size > kBufLen) return nullptr;
		return gSlotNameBuf + off;
	});
}
// Serialize the live party + item objects into the fixed ITEMS layout that
// parseItemsTmp/parseItemStream read back. This is the round-trippable writer
// (unlike saveRange's CDESC dump, which does NOT round-trip and is why the
// save/suspend item halves were quarantined). Kernel-state bytes 0..676 are
// scaffolded from the pristine ITEMS_00.BIN; the party position is either kept
// from that scaffold or overwritten from the live kernel.
//
//   preInstantiateWorld: bootstrap the world item pool (slots 15..999) from
//     ITEMS_00.BIN into live objects BEFORE serializing. On only for the
//     initial (chargen) party -- mid-game the world is already live, and
//     re-creating it would resurrect items the player already consumed.
//   writeLivePosition: overwrite file bytes 252..257 (party x/y/fdir/level +
//     bar_graphs/sounds option bytes) with the live kernel statics @243..248.
//     Off for the initial party (the scaffold's graveyard cell is correct);
//     on for real mid-game saves.
//
// Returns true iff ITEMS bytes were written to dstPath. `outPcs`/`outItems`
// receive the record counts for the caller's trace line.
static bool writeItemsFixed(Context &ctx, const std::filesystem::path &dstPath,
                            bool preInstantiateWorld, bool writeLivePosition,
                            int &outPcs, int &outItems, int &outWorld) {
	auto saveDir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
	outPcs = outItems = outWorld = 0;

	// Never overwrite a save with an empty party: suspend_game fires before
	// launching sub-programs, and if that ever happens with no live PCs (e.g.
	// a new-game path before the party exists) we'd strand a 0-PC ITEMS.TMP
	// that boots straight to the death screen. No party -> leave the file be.
	{
		int live = 0;
		for (int pc : ctx.objects.objectsOfClass(1369)) { (void) pc; ++live; }
		if (live == 0) return false;
	}

	// File-format offsets (verified against items_tmp.cpp's reader).
	constexpr size_t kPcRecordBase = 677;
	constexpr size_t kPcStride     = 627;
	constexpr size_t kMaxRecords   = 10;
	constexpr size_t kItemStreamOff = kPcRecordBase + kMaxRecords * kPcStride;

	// PC class statics offsets (mirror items_tmp.cpp).
	constexpr uint16_t kPcClass = 1369;
	constexpr uint16_t kItemsBase = 1371; // every item subclasses 1371
	constexpr uint32_t kPcSpLvlOff  = 77;
	constexpr uint32_t kPcMnLvlOff  = 79;
	constexpr uint32_t kPcInvOff    = 81;
	constexpr uint32_t kPcQuiverOff = 133, kPcArrowsOff = 135;
	constexpr uint32_t kPcNameOff = 137, kPcNameLen = 20;
	constexpr uint32_t kPcRaceOff = 157, kPcClassesOff = 158;
	constexpr uint32_t kPcPortraitOff = 159, kPcPCstatOff = 161;
	constexpr uint32_t kPcAlignOff = 162, kPcLevelsOff = 163;
	constexpr uint32_t kPcLostLvlsOff = 166, kPcLostHpOff = 169;
	constexpr uint32_t kPcHpCurOff = 171, kPcHpMaxOff = 173, kPcHbonOff = 175;
	constexpr uint32_t kPcFoodOff = 177, kPcXpOff = 179;
	constexpr uint32_t kPcStrOff = 191, kPcExcStrOff = 192, kPcIntOff = 193;
	constexpr uint32_t kPcWisOff = 194, kPcDexOff = 195, kPcConOff = 196;
	constexpr uint32_t kPcChaOff = 197, kPcSparkleOff = 198;
	constexpr uint32_t kPcMagicEffOff = 199, kPcTigerOff = 203, kPcLostStrOff = 204;
	constexpr uint32_t kPcUnknownGapOff = 205; // 4 bytes, semantics unknown
	constexpr uint32_t kPcSpellCntOff = 209, kPcSpellStatOff = 409;
	constexpr uint32_t kPcSpellLen = 200;

	// File-record offsets within a 627-byte PC record.
	constexpr size_t kFObjIndex = 0, kFClass = 2, kFConst619 = 6;
	constexpr size_t kFSpLvl = 95, kFMnLvl = 97;
	constexpr size_t kFBackpack = 99, kFEquip = 127;
	constexpr size_t kFArrowsType = 151, kFArrowsQty = 153;
	constexpr size_t kFName = 155, kFRace = 175, kFClasses = 176;
	constexpr size_t kFPortrait = 177, kFPCstat = 179, kFAlign = 180;
	constexpr size_t kFLevels = 181, kFLostLvls = 184, kFLostHp = 187;
	constexpr size_t kFHpCur = 189, kFHpMax = 191, kFHbon = 193, kFFood = 195;
	constexpr size_t kFXp = 197, kFStr = 209, kFStrPct = 210;
	constexpr size_t kFInt = 211, kFWis = 212, kFDex = 213, kFCon = 214;
	constexpr size_t kFCha = 215, kFSparkle = 216, kFMagicEff = 217;
	constexpr size_t kFTiger = 221, kFLostStr = 222, kFUnknownGap = 223;
	constexpr size_t kFSpellCnt = 227, kFSpellStat = 427;

	auto putU16 = [](uint8_t *p, uint16_t v) {
		p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
	};
	auto putI16 = [&](uint8_t *p, int16_t v) {
		putU16(p, static_cast<uint16_t>(v));
	};
	auto putU32 = [](uint8_t *p, uint32_t v) {
		p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
		p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
	};
	auto readPcU8 = [&](int pc, uint32_t off) -> uint8_t {
		uint8_t *p = ctx.objects.classStaticPtr(pc, kPcClass, off, 1);
		return p ? *p : 0;
	};
	auto readPcU16 = [&](int pc, uint32_t off) -> uint16_t {
		uint8_t *p = ctx.objects.classStaticPtr(pc, kPcClass, off, 2);
		return p ? static_cast<uint16_t>(p[0] | (p[1] << 8)) : 0;
	};
	auto readPcU32 = [&](int pc, uint32_t off) -> uint32_t {
		uint8_t *p = ctx.objects.classStaticPtr(pc, kPcClass, off, 4);
		return p ? static_cast<uint32_t>(p[0]) | (p[1] << 8u)
		           | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24) : 0;
	};

	// Step 1 -- seed scaffold from ITEMS_00.BIN (kernel-state bytes 0..676).
	std::vector<uint8_t> buf;
	bool scaffoldOk = false;
	{
		std::ifstream src(saveDir / "ITEMS_00.BIN", std::ios::binary);
		if (src) {
			buf.assign(std::istreambuf_iterator<char>(src),
			           std::istreambuf_iterator<char>());
			scaffoldOk = !buf.empty();
		}
	}
	if (!scaffoldOk) buf.assign(kItemStreamOff, 0);
	if (buf.size() < kItemStreamOff) buf.resize(kItemStreamOff, 0);

	// Step 2 -- party position. Either keep the scaffold's cell (fresh party)
	// or copy the live kernel's @243..248 into file @252..257 (mid-game save).
	if (writeLivePosition) {
		constexpr uint16_t kKernelClass = 1382;
		constexpr uint32_t kPartyX = 243; // x,y,fdir,level,bar_graphs,sounds
		constexpr size_t kPosOff = 252;
		int kernel = ctx.objects.firstObjectOfClass(kKernelClass);
		if (kernel >= 0)
			for (uint32_t k = 0; k < 6; ++k)
				if (uint8_t *p = ctx.objects.classStaticPtr(kernel, kKernelClass,
				                                            kPartyX + k, 1))
					buf[kPosOff + k] = *p;
	}

	// Step 3 -- write live PC records. The loader reads records sequentially
	// and maps file slot -> party position (slots 0..5 register in the kernel's
	// player[] array; 6..9 are reserve roster). So the FILE-SLOT ORDER must be
	// the party order -- NOT PC.W:num, which is only set for party members and
	// leaves roster PCs at a stale 0 (that collision put a roster PC in slot 0,
	// displacing the party leader). Build the order from the kernel's player[]
	// array, then append any live PC objects not in the party.
	std::fill(buf.begin() + kPcRecordBase, buf.begin() + kItemStreamOff, 0u);
	std::array<int, kMaxRecords> slotPc;
	slotPc.fill(-1);
	{
		// 1. Party members take their kernel player[] index -- that is exactly
		//    the party position the loader will re-register them into.
		constexpr uint16_t kKernelClass = 1382;
		constexpr uint32_t kPlayerOff = 229, kPlayerSlots = 6;
		int kernel = ctx.objects.firstObjectOfClass(kKernelClass);
		if (kernel >= 0)
			for (uint32_t i = 0; i < kPlayerSlots && i < kMaxRecords; ++i)
				if (uint8_t *p = ctx.objects.classStaticPtr(
				        kernel, kKernelClass, kPlayerOff + i * 2, 2)) {
					int16_t oi = static_cast<int16_t>(p[0] | (p[1] << 8));
					if (oi > 0) slotPc[i] = oi;
				}
		// 2. Remaining live PCs (reserve roster; or, on the fresh-chargen path
		//    where player[] isn't populated yet, the whole party) go to their
		//    PC.W:num slot when free, else the next free slot. This keeps the
		//    proven chargen layout (PC_num 0..5) and stops roster PCs whose
		//    PC_num is a stale 0 from colliding into an occupied party slot.
		auto place = [&](int pc, uint32_t want) {
			if (want < kMaxRecords && slotPc[want] == -1) { slotPc[want] = pc; return; }
			for (size_t s = 0; s < kMaxRecords; ++s)
				if (slotPc[s] == -1) { slotPc[s] = pc; return; }
		};
		for (int pc : ctx.objects.objectsOfClass(kPcClass)) {
			bool placed = false;
			for (int s : slotPc) if (s == pc) { placed = true; break; }
			if (!placed) place(pc, readPcU8(pc, /*PC_num*/ 0));
		}
	}
	for (size_t slot = 0; slot < kMaxRecords; ++slot) {
		int pc = slotPc[slot];
		if (pc < 0) continue;
		uint8_t *rec = &buf[kPcRecordBase + slot * kPcStride];
		putU16(rec + kFObjIndex, static_cast<uint16_t>(pc));
		putU16(rec + kFClass,    kPcClass);
		putU16(rec + kFConst619, 619);
		for (int s = 0; s < 12; ++s)
			putU16(rec + kFEquip + s * 2,
			       readPcU16(pc, kPcInvOff + (14 + s) * 2));
		for (int k = 0; k < 2; ++k) {
			rec[kFSpLvl + k] = readPcU8(pc, kPcSpLvlOff + k);
			rec[kFMnLvl + k] = readPcU8(pc, kPcMnLvlOff + k);
		}
		for (int s = 0; s < 14; ++s)
			putU16(rec + kFBackpack + s * 2,
			       readPcU16(pc, kPcInvOff + s * 2));
		putI16(rec + kFArrowsType, static_cast<int16_t>(readPcU16(pc, kPcQuiverOff)));
		putI16(rec + kFArrowsQty,  static_cast<int16_t>(readPcU16(pc, kPcArrowsOff)));
		if (uint8_t *np = ctx.objects.classStaticPtr(pc, kPcClass, kPcNameOff,
		                                              kPcNameLen))
			std::memcpy(rec + kFName, np, kPcNameLen);
		rec[kFRace]    = readPcU8(pc, kPcRaceOff);
		rec[kFClasses] = readPcU8(pc, kPcClassesOff);
		putU16(rec + kFPortrait, readPcU16(pc, kPcPortraitOff));
		rec[kFPCstat]  = readPcU8(pc, kPcPCstatOff);
		rec[kFAlign]   = readPcU8(pc, kPcAlignOff);
		for (int k = 0; k < 3; ++k) {
			rec[kFLevels + k]   = readPcU8(pc, kPcLevelsOff + k);
			rec[kFLostLvls + k] = readPcU8(pc, kPcLostLvlsOff + k);
			putU32(rec + kFXp + k * 4, readPcU32(pc, kPcXpOff + k * 4));
		}
		putI16(rec + kFLostHp, static_cast<int16_t>(readPcU16(pc, kPcLostHpOff)));
		putI16(rec + kFHpCur,  static_cast<int16_t>(readPcU16(pc, kPcHpCurOff)));
		putI16(rec + kFHpMax,  static_cast<int16_t>(readPcU16(pc, kPcHpMaxOff)));
		putI16(rec + kFHbon,   static_cast<int16_t>(readPcU16(pc, kPcHbonOff)));
		putI16(rec + kFFood,   static_cast<int16_t>(readPcU16(pc, kPcFoodOff)));
		rec[kFStr]    = readPcU8(pc, kPcStrOff);
		rec[kFStrPct] = readPcU8(pc, kPcExcStrOff);
		rec[kFInt]    = readPcU8(pc, kPcIntOff);
		rec[kFWis]    = readPcU8(pc, kPcWisOff);
		rec[kFDex]    = readPcU8(pc, kPcDexOff);
		rec[kFCon]    = readPcU8(pc, kPcConOff);
		rec[kFCha]    = readPcU8(pc, kPcChaOff);
		rec[kFSparkle] = readPcU8(pc, kPcSparkleOff);
		putU32(rec + kFMagicEff, readPcU32(pc, kPcMagicEffOff));
		rec[kFTiger]   = readPcU8(pc, kPcTigerOff);
		rec[kFLostStr] = readPcU8(pc, kPcLostStrOff);
		// +223..+226: the 4 unknown bytes (PC 205..208). parseItemsTmp and
		// resume_level round-trip them; zeroing here silently wiped whatever
		// they hold on every save (CodeRabbit).
		putU32(rec + kFUnknownGap, readPcU32(pc, kPcUnknownGapOff));
		if (uint8_t *sp = ctx.objects.classStaticPtr(pc, kPcClass,
		                                              kPcSpellCntOff, kPcSpellLen))
			std::memcpy(rec + kFSpellCnt, sp, kPcSpellLen);
		if (uint8_t *sp = ctx.objects.classStaticPtr(pc, kPcClass,
		                                              kPcSpellStatOff, kPcSpellLen))
			std::memcpy(rec + kFSpellStat, sp, kPcSpellLen);
		++outPcs;
	}

	// Step 3.5 -- initial party only: pre-instantiate the world item pool from
	// ITEMS_00.BIN so niche/monster/chest references resolve (and get baked
	// into the item stream below). NEVER during a mid-game save: the live-slot
	// guard skips occupied slots, but a slot freed by a consumed item would be
	// re-created here, resurrecting loot the player already used.
	if (preInstantiateWorld) {
		outWorld = THIRDEYE::savegame::loadAreaInstances(
		    saveDir,
		    [&](int slot, uint16_t cls, const std::vector<uint8_t> &data) {
			    if (slot == 15) return; // entities singleton
			    if (!ctx.objects.isSubclassOf(cls, kItemsBase)) return;
			    if (ctx.objects.classOf(slot) != 0xFFFF) return; // keep live
			    try {
				    ctx.objects.createProgram(slot, cls);
				    if (!data.empty())
					    if (uint8_t *sp = ctx.objects.staticsPtr(
					            slot, 0, static_cast<uint32_t>(data.size())))
						    std::memcpy(sp, data.data(), data.size());
			    } catch (const std::exception &) {}
		    },
		    /*firstSlot=*/15, /*lastSlot=*/999);
	}

	// Step 4 -- truncate at the stream offset and append the item-object
	// array as native CDESC records ({u16 slot, u32 name, u16 size} + statics,
	// RTOBJECT.H save_range): a full record for every live item, and an
	// EXPLICIT empty-slot record ({id, name=0xFFFFFFFF, size=0} -- save_range's
	// own encoding for a dead objlist entry) for every world-pool slot
	// (15..999) without one. The explicit empties are what lets the loader
	// distinguish "consumed item" from "old save that never serialized this
	// slot": without them the gap-fill resurrected every used-up potion/scroll
	// from ITEMS_00.BIN on each restore (CodeRabbit). Statics go verbatim --
	// B:bonus/itmflags live inside them at their true offsets, no side-channel.
	buf.resize(kItemStreamOff);
	constexpr int kWorldFirst = 15, kWorldLast = 999;
	for (int i = 0; i < VM::ObjectSystem::kNumEntities; ++i) {
		uint16_t cls = ctx.objects.classOf(i);
		bool liveItem = cls != 0xFFFF &&
		                ctx.objects.isSubclassOf(cls, kItemsBase);
		if (!liveItem) {
			if (i >= kWorldFirst && i <= kWorldLast) {
				size_t recOff = buf.size();
				buf.resize(recOff + 8, 0);
				putU16(&buf[recOff],     static_cast<uint16_t>(i));
				putU16(&buf[recOff + 2], 0xFFFF);
				putU16(&buf[recOff + 4], 0xFFFF); // name high half: u32 -1
				// size u16 @+6 stays 0
			}
			continue;
		}
		uint32_t blockSize = ctx.objects.instanceStaticSize(cls);
		if (blockSize > 0xFFFF) {
			// CDESC size is u16; a silent wrap here would desync every
			// following record on the next load (CodeRabbit). No real EOB3
			// class comes near this, so treat it as data corruption: emit an
			// explicit empty record for the slot instead.
			std::cerr << "[save] item slot " << i << " class " << cls
			          << " statics " << blockSize
			          << " B exceed CDESC u16 -- writing empty record\n";
			size_t recOff = buf.size();
			buf.resize(recOff + 8, 0);
			putU16(&buf[recOff],     static_cast<uint16_t>(i));
			putU16(&buf[recOff + 2], 0xFFFF);
			putU16(&buf[recOff + 4], 0xFFFF);
			continue;
		}
		size_t recOff = buf.size();
		buf.resize(recOff + 8 + blockSize, 0);
		putU16(&buf[recOff],     static_cast<uint16_t>(i));
		putU16(&buf[recOff + 2], cls); // name low half; high half stays 0
		putU16(&buf[recOff + 6], static_cast<uint16_t>(blockSize));
		if (blockSize > 0)
			if (uint8_t *sp = ctx.objects.staticsPtr(i, 0, blockSize))
				std::memcpy(&buf[recOff + 8], sp, blockSize);
		++outItems;
	}

	// Step 5 -- write atomically: stage to a same-directory temp file, then
	// rename over the destination. A crash/full disk mid-write leaves the
	// old save intact instead of a truncated file (CodeRabbit).
	auto tmpPath = dstPath;
	tmpPath += ".tmpwrite";
	{
		std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		out.write(reinterpret_cast<const char *>(buf.data()),
		          static_cast<std::streamsize>(buf.size()));
		// close() flushes the tail of the buffer and sets failbit on error;
		// checking before it would miss a failed close-time flush (CodeRabbit).
		out.close();
		if (out.fail()) {
			std::error_code ec;
			std::filesystem::remove(tmpPath, ec);
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(tmpPath, dstPath, ec);
	if (ec) {
		std::error_code ec2;
		std::filesystem::remove(tmpPath, ec2);
		return false;
	}
	return true;
}

// Multi-file save transaction: the ITEMS and LVL halves of a save are only
// meaningful as a pair, so each output is first written to a staged
// "<final>.stage" path, then every stage is renamed over its final in one
// commit pass once ALL writes succeeded. On any write failure the stages are
// discarded and the previously committed ITEMS/LVL pair stays intact
// (CodeRabbit: no mixed-generation slots). Individual writers remain
// internally atomic (temp+rename onto the stage), so a crash mid-anything
// leaves only ignorable *.stage litter, never a truncated live file.
struct StagedCommit {
	std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
	std::filesystem::path stage(const std::filesystem::path &final) {
		auto s = final;
		s += ".stage";
		files.emplace_back(s, final);
		return s;
	}
	bool commit() {
		bool all = true;
		for (const auto &[s, f] : files) {
			std::error_code ec;
			std::filesystem::rename(s, f, ec);
			if (ec) all = false; // same-dir rename; failure here is exotic
		}
		return all;
	}
	void discard() {
		for (const auto &[s, f] : files) {
			std::error_code ec;
			std::filesystem::remove(s, ec);
		}
	}
};

} // namespace

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// --- save-picker probe (Continue the Quest → Restore Game) -----------
	// The SOP's save-picker iterates slots 0..N, calls savegame_title(slot)
	// to fetch the slot's name, then string_compare(name, "_______") to
	// check if the slot is empty. With both stubbed to 0, every slot looks
	// empty and the picker prints "No game is saved at that position".
	//
	// Wire savegame_title to return the slot index for slots that have a
	// save in SAVEGAME.DIR (any non-zero distinguishes "used"), 0 otherwise;
	// and string_compare returns non-zero when arg0 != "______" pattern
	// (= the slot has a real name). This is intentionally minimal -- enough
	// to advance the SOP past "is this slot empty" and reveal what it calls
	// next (= the actual save-restoration runtime function we still need
	// to wire). Real string display is a future step.
	if (fn == "savegame_title" && args.size() >= 1) {
		int slot = static_cast<int>(args[0]);
		// Read the slot name from SAVEGAME.DIR; write into our runtime-owned
		// buffer; ALWAYS return the buffer addr so the SOP's string_compare
		// against the "__________________________" empty-slot marker returns
		// the right thing for both cases:
		//   used slot   -> buffer = "Quick Start Party" -> compare != 0 -> render name + click loads
		//   empty slot  -> buffer = "__________________________" -> compare == 0 -> render dashes + click is ignored
		// Returning 0 for empty slots (which we used to do) made the SOP's
		// string_compare see "" vs "____" = NON-zero = "slot is used" = it
		// would happily fire restore_items on every empty slot the user
		// clicked, corrupting ITEMS.TMP. The original game's savegame_title
		// also writes the underscore marker into the buffer for empty slots.
		std::string name;
		try {
			auto entries = THIRDEYE::savegame::loadSaveDir(
			    ctx.res.resourcePath().parent_path() / "SAVEGAME" / "SAVEGAME.DIR");
			if (slot >= 0 && static_cast<size_t>(slot) < entries.size() &&
			    entries[slot].used)
				name = entries[slot].name;
		} catch (const std::exception &) {}
		ensureSlotNameHook(ctx.objects);
		if (slot < 0 || slot >= kSaveSlots) {
			result = 0;
			return true;
		}
		// 26 underscores -- matches the marker the SOP's string_compare passes
		// (we observed "__________________________" in the picker trace).
		constexpr size_t kEmptyMarkLen = 26;
		uint8_t *p = gSlotNameBuf + slot * kSlotNameLen;
		// Only (re)fill from disk while the slot buffer is empty. Once
		// populated -- by us or by the SOP (the save UI copy_string's the
		// typed slot name into this buffer before write_save_directory) --
		// the buffer is the live truth and a disk re-read would clobber it.
		if (p[0] == 0) {
			std::memset(p, 0, kSlotNameLen);
			if (name.empty()) {
				std::memset(p, '_',
				            std::min<size_t>(kEmptyMarkLen, kSlotNameLen - 1));
			} else {
				size_t n = std::min(name.size(),
				                    static_cast<size_t>(kSlotNameLen - 1));
				std::memcpy(p, name.data(), n);
			}
		}
		result = VM::makeAddr(VM::AddrSpace::Static,
		                      static_cast<uint32_t>(slot * kSlotNameLen),
		                      kSaveBufObj);
		rt() << "  [savegame_title slot=" << slot
		     << (name.empty() ? " -> empty (\"_____\")" : " -> \"" + name + "\"")
		     << "]" << std::endl;
		return true;
	}
	// restore_items(slot) -- copy SAVEGAME/ITEMS_(slot):02d.BIN -> ITEMS.TMP.
	// The SOP's save picker calls this after the user clicks a used slot;
	// resume_level later reads ITEMS.TMP and seeds the party from it.
	// Slot number maps to the file suffix VERBATIM (EYE.C set_save_slotnum):
	// picker slot 1 ("Quick Start Party" = SAVEGAME.DIR line 1) = ITEMS_01.BIN.
	// Slot 0 = the new-game INITIAL state ("Read initial (slot 0) items" in
	// EYE.C; save_game abends on it) -- a real DOS new-game overwrites
	// ITEMS_00.BIN with the rolled party, so it is NOT a pristine QSP copy.
	if (fn == "restore_items" && args.size() >= 1) {
		int slot = static_cast<int>(args[0]);
		int idx  = slot;
		auto dir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		bool ok = THIRDEYE::savegame::restoreItems(dir, idx);
		// Automap sidecar: copy slot's MAPS_nn.BIN -> MAPS.TMP, then load into
		// the module. Gated on the ITEMS restore succeeding AND the copy AND
		// the parse -- otherwise a stale MAPS.TMP from another slot could
		// smear its exploration onto this one (CodeRabbit). Any failure ->
		// reset (fresh fog; strictly less information, never wrong info).
		char nn[16];
		std::snprintf(nn, sizeof(nn), "%02d", idx);
		auto src = dir / ("MAPS_" + std::string(nn) + ".BIN");
		auto live = dir / "MAPS.TMP";
		std::error_code ec;
		bool mapRestored = false;
		if (ok && std::filesystem::exists(src, ec)) {
			bool copied = std::filesystem::copy_file(src, live,
			    std::filesystem::copy_options::overwrite_existing, ec);
			mapRestored = copied && THIRDEYE::automap::loadFrom(live);
		}
		if (!mapRestored)
			THIRDEYE::automap::reset();
		rt() << "  [restore_items: slot " << idx
		     << (ok ? " -> ITEMS.TMP OK" : " not found; ITEMS.TMP preserved")
		     << "]" << std::endl;
		result = ok ? 1 : 0;
		return true;
	}
	// restore_level_objects(slot, ...) -- copy SAVEGAME/LVL??_(slot-1):02d.BIN
	// -> LVL??.TMP for every level whose backup exists. (The 2nd arg looks
	// like a redraw cookie / progress counter -- ignored; we restore all
	// levels in one shot so subsequent change_level reads the right state.)
	if (fn == "restore_level_objects" && args.size() >= 1) {
		int slot = static_cast<int>(args[0]);
		int idx  = slot; // verbatim slot -> file suffix (see restore_items)
		auto dir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		// Re-confirm the ITEMS.TMP refresh BEFORE counting level copies.
		// The SOP pairs restore_items + restore_level_objects, but if the
		// first call's copy_file failed (disk full, race, ...) ITEMS.TMP is
		// stale. Self-dispatching resume_level on `copied > 0` alone could
		// then patch live PC state from the wrong slot. restoreItems with
		// overwrite_existing is idempotent for the happy path (re-copying
		// the same content) and acts as a confirmation gate for the
		// failure path. CodeRabbit'd.
		bool itemsOk = THIRDEYE::savegame::restoreItems(dir, idx);
		int copied = itemsOk
		                 ? THIRDEYE::savegame::restoreLevels(dir, idx) : 0;
		rt() << "  [restore_level_objects: copied " << copied
		     << " LVL??.TMP from slot " << idx
		     << (!itemsOk ? " (ITEMS.TMP refresh failed; LVL??.TMP preserved)"
		         : copied == 0 ? " (slot empty or no backups; LVL??.TMP preserved)"
		                       : "")
		     << "]" << std::endl;
		// resume_level only fires when BOTH halves landed: a valid ITEMS.TMP
		// for this slot AND at least one LVL??.TMP. This guarantees the live
		// PC state we patch matches the file state we just wrote.
		bool restored = itemsOk && copied > 0;
		if (restored) {
			VM::Value rl;
			tryHandle(ctx, "resume_level", {VM::Value{0}}, rl);
		}
		result = restored ? 1 : 0;
		return true;
	}
	if (fn == "string_compare" && args.size() >= 2) {
		// stricmp semantics (RTCODE.C): 0 = equal ignoring case, nonzero =
		// different. Both args are tagged addresses (Code string OR
		// Static/Extern buffer); readString handles either and returns ""
		// for an unresolvable address.
		std::string a = ctx.vm.readString(args[0]);
		std::string b = ctx.vm.readString(args[1]);
		auto lower = [](std::string s) {
			for (char &c : s)
				c = static_cast<char>(
				    std::tolower(static_cast<unsigned char>(c)));
			return s;
		};
		int cmp = lower(a).compare(lower(b));
		result = cmp;
		rt() << "  [string_compare \"" << a << "\" vs \"" << b
		     << "\" -> " << cmp << "]" << std::endl;
		return true;
	}

	// --- in-game save (EYE.C save cluster) -------------------------------
	// read_save_directory: load SAVEGAME.DIR into the slot-name buffer (the
	// original's savegame_dir[] array). savegame_title then serves pointers
	// into it; the save UI edits it in place (copy_string) and
	// write_save_directory persists it.
	if (fn == "read_save_directory") {
		ensureSlotNameHook(ctx.objects);
		std::memset(gSlotNameBuf, 0, sizeof(gSlotNameBuf));
		try {
			auto entries = THIRDEYE::savegame::loadSaveDir(
			    ctx.res.resourcePath().parent_path() / "SAVEGAME" /
			    "SAVEGAME.DIR");
			for (size_t s = 0; s < entries.size() &&
			                   s < static_cast<size_t>(kSaveSlots); ++s) {
				uint8_t *p = gSlotNameBuf + s * kSlotNameLen;
				if (entries[s].used) {
					size_t n = std::min(entries[s].name.size(),
					                    static_cast<size_t>(kSlotNameLen - 1));
					std::memcpy(p, entries[s].name.data(), n);
				} else {
					std::memset(p, '_', 26);
				}
			}
		} catch (const std::exception &) {}
		result = 0;
		return true;
	}
	// write_save_directory: serialize the slot-name buffer back to
	// SAVEGAME.DIR (12 CRLF lines + trailing 0x1A, matching the shipped file).
	if (fn == "write_save_directory") {
		auto path = ctx.res.resourcePath().parent_path() / "SAVEGAME" /
		            "SAVEGAME.DIR";
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		constexpr int kDirSlots = 12; // NUM_SAVEGAMES
		if (out) {
			for (int s = 0; s < kDirSlots; ++s) {
				const uint8_t *p = gSlotNameBuf + s * kSlotNameLen;
				if (p[0] != 0) {
					// strnlen: the SOP's copy_string writes these slots in
					// place and can fill all kSlotNameLen bytes w/o a NUL.
					out.write(reinterpret_cast<const char *>(p),
					          static_cast<std::streamsize>(strnlen(
					              reinterpret_cast<const char *>(p),
					              kSlotNameLen)));
				} else {
					for (int i = 0; i < 26; ++i) out.put('_');
				}
				out.write("\r\n", 2);
			}
			out.put('\x1a');
		}
		rt() << "  [write_save_directory -> " << path.string()
		     << (out ? " OK" : " FAILED") << "]" << std::endl;
		result = 0;
		return true;
	}
	// save_game(slotnum, lvlnum): live items -> ITEMS_nn.BIN, live level
	// objects -> LVL(lvlnum)_nn.BIN, and the other levels' LVLxx.TMP copied
	// to LVLxx_nn.BIN. nn = slotnum VERBATIM (EYE.C set_save_slotnum); slot 0
	// is the new-game initial state and is rejected (EYE.C abends on it).
	// Returns 1 on success, 0 on failure (matches EYE.C).
	if (fn == "save_game" && args.size() >= 2) {
		int slot = static_cast<int>(args[0]);
		int lvl = static_cast<int>(args[1]);
		int idx = slot;
		result = 0;
		if (idx < 1 || lvl < 1 || lvl > 14)
			return true;
		char nn[16], ll[16];
		std::snprintf(nn, sizeof(nn), "%02d", idx);
		std::snprintf(ll, sizeof(ll), "%02d", lvl);
		auto dir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		// ITEMS half: serialize live party + items into the fixed layout the
		// loader reads (writeItemsFixed round-trips; the old saveRange CDESC
		// dump did not, and once wrote an unreadable ITEMS_01.BIN -- see git
		// history). writeLivePosition captures the party's current cell/facing;
		// preInstantiateWorld is OFF (mid-game the world pool is already live,
		// and re-seeding it from ITEMS_00.BIN would resurrect consumed items).
		// All slot outputs go through a StagedCommit: nothing under the final
		// ITEMS_nn/LVL??_nn names is replaced until every write landed, so a
		// mid-save failure can't pair this generation's items with a previous
		// generation's levels (CodeRabbit).
		StagedCommit txn;
		int spcs = 0, sitems = 0, sworld = 0;
		bool ok = writeItemsFixed(
		    ctx, txn.stage(dir / ("ITEMS_" + std::string(nn) + ".BIN")),
		    /*preInstantiateWorld=*/false, /*writeLivePosition=*/true,
		    spcs, sitems, sworld);
		ok = ok && THIRDEYE::savegame::saveRange(
		         ctx.objects,
		         txn.stage(dir / ("LVL" + std::string(ll) + "_" + nn + ".BIN")),
		         1000, 1999);
		int copied = 0;
		for (int l = 1; ok && l <= 14; ++l) {
			if (l == lvl) continue;
			char cc[3];
			std::snprintf(cc, sizeof(cc), "%02d", l);
			auto tmp = dir / ("LVL" + std::string(cc) + ".TMP");
			auto bin = dir / ("LVL" + std::string(cc) + "_" + nn + ".BIN");
			std::error_code ec;
			// A missing TMP is tolerated (fresh boot paths may not have
			// written every level yet); a save with the current level +
			// items intact is still restorable. A failed copy of an
			// EXISTING TMP aborts the transaction instead -- committing it
			// would silently leave that level's backup a generation stale.
			if (!std::filesystem::exists(tmp, ec))
				continue;
			if (std::filesystem::copy_file(
			        tmp, txn.stage(bin),
			        std::filesystem::copy_options::overwrite_existing, ec) &&
			    !ec)
				++copied;
			else
				ok = false;
		}
		if (ok)
			ok = txn.commit();
		else
			txn.discard();
		// Automap sidecar: write MAPS.TMP + clone to MAPS_nn.BIN. Separate
		// file, no bearing on the game save. Loss here is cosmetic (worst case
		// the map re-fogs on load), so failures don't gate ok -- but they are
		// logged so a full-disk doesn't silently strand old maps.
		bool mapsOk = THIRDEYE::automap::saveTo(dir / "MAPS.TMP");
		mapsOk = THIRDEYE::automap::saveTo(
		             dir / ("MAPS_" + std::string(nn) + ".BIN")) && mapsOk;
		if (!mapsOk)
			std::cerr << "[save_game: automap MAPS write failed (map may "
			             "re-fog on restore); game save unaffected]\n";
		rt() << "  [save_game slot " << idx << " lvl " << lvl
		     << (ok ? " OK" : " FAILED") << " (" << spcs << " PCs, " << sitems
		     << " items; +" << copied << " LVL??.TMP copies, maps "
		     << (mapsOk ? "OK" : "FAILED") << ")]" << std::endl;
		result = ok ? 1 : 0;
		return true;
	}
	// suspend_game(cur_lvl): flush live state to the temp files (items ->
	// ITEMS.TMP, current level's objects -> LVLxx.TMP). The original calls
	// this before launching a sub-program (camp/chargen) and before saving.
	if (fn == "suspend_game" && args.size() >= 1) {
		int lvl = static_cast<int>(args[0]);
		auto dir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		// Items half: flush live party + items to ITEMS.TMP in the fixed
		// round-trippable layout (writeItemsFixed), so "Continue the Quest"
		// resumes real progress -- position, HP, inventory, memorized spells.
		// preInstantiateWorld OFF (world already live; re-seeding would revive
		// consumed items). LVLxx.TMP round-trips via saveRange's CDESC stream.
		// Stage ITEMS.TMP + LVLxx.TMP together and commit only when both
		// landed -- a failed level flush must not leave ITEMS.TMP a
		// generation ahead of the levels (CodeRabbit).
		StagedCommit txn;
		int spcs = 0, sitems = 0, sworld = 0;
		bool ok = writeItemsFixed(ctx, txn.stage(dir / "ITEMS.TMP"),
		                          /*preInstantiateWorld=*/false,
		                          /*writeLivePosition=*/true, spcs, sitems, sworld);
		if (lvl >= 1 && lvl <= 14) {
			char ll[3];
			std::snprintf(ll, sizeof(ll), "%02d", lvl);
			ok = THIRDEYE::savegame::saveRange(
			         ctx.objects, txn.stage(dir / ("LVL" + std::string(ll) + ".TMP")),
			         1000, 1999) && ok;
		}
		if (ok)
			ok = txn.commit();
		else
			txn.discard();
		bool mapsOk = THIRDEYE::automap::saveTo(dir / "MAPS.TMP");
		rt() << "  [suspend_game lvl " << lvl << (ok ? " OK" : " FAILED")
		     << " (" << spcs << " PCs, " << sitems << " items; maps "
		     << (mapsOk ? "OK" : "FAILED") << ")]" << std::endl;
		result = 0;
		return true;
	}
	// resume_items(first, last, restoring): the original re-materializes
	// objects from ITEMS.TMP after a sub-program returns. Our launch()
	// unwind re-enters bootObject, whose resume_level path already rebuilds
	// PCs + items from ITEMS.TMP -- doing it again here would double-create.
	// create_initial_binary_files: dev-time TXT -> BIN translation; shipped
	// installs already have the .BINs.
	if (fn == "resume_items" || fn == "create_initial_binary_files") {
		rt() << "  [" << fn << ": no-op (covered by resume_level flow)]"
		     << std::endl;
		result = 0;
		return true;
	}
	// --- party transfer (EYE.C transfer-file API) ---
	// open_transfer_file(name): buffer the transfer save so player_attrib/
	// item_attrib can read the party out of it. Two callers:
	//   M:16 "convert created party" -> "CHARGEN\CREATE.SAV" (CHGEN.EXE output)
	//   M:14 "transfer from Eye II"  -> "TRANSFER.SAV"        (CHARCOPY.EXE output)
	// Both flow into M:15 "transfer" with the same offset layout -- CHARCOPY just
	// renames the EOB2 save into place (see ../eob3_research/CHARCOPY/README.md:
	// `ren temptemp.sav transfer.sav`). The DOS path uses a backslash and is
	// relative to the game dir; map it to the real sibling file beside the .RES.
	//
	// Return convention per arun/src/EYE.H:199 -- `void *cdecl open_transfer_file`
	// returns a file handle: non-zero on success, zero on failure. M:14 checks
	// staticVar0 == 0 to show "run CHARCOPY" dialog; M:16 ignores the return.
	if (fn == "open_transfer_file" && args.size() >= 1) {
		std::string name = ctx.vm.readCodeString(static_cast<uint32_t>(args[0]));
		std::filesystem::path full = ctx.res.resourcePath().parent_path();
		std::string part;
		for (size_t i = 0; i <= name.size(); ++i) {
			if (i == name.size() || name[i] == '\\') {
				if (!part.empty()) full /= part;
				part.clear();
			} else part.push_back(name[i]);
		}
		ctx.xfer.data.clear();
		std::ifstream in(full, std::ios::binary);
		if (in) {
			ctx.xfer.data.assign(std::istreambuf_iterator<char>(in),
			                     std::istreambuf_iterator<char>());
		}
		ctx.xfer.rebuildPlacement();
		rt() << "  [open_transfer_file \"" << name << "\" -> " << full.string()
		     << " (" << ctx.xfer.data.size() << " bytes"
		     << (ctx.xfer.data.empty() ? ", MISSING" : "") << ")]" << std::endl;
		for (int pc = 0; pc < 4 && !ctx.xfer.data.empty(); ++pc) {
			rt() << "  [xfer pc " << pc << " placement:";
			for (int s = 0; s < 26; ++s)
				if (ctx.xfer.pcItemAtSlot[pc][s] >= 0)
					rt() << " e" << s << "<-cs" << int(ctx.xfer.pcItemAtSlot[pc][s]);
			rt() << "]" << std::endl;
		}
		// ponytail: return a truthy "handle" value (buffer size) instead of a real
		// FILE*: the SOP only tests non-zero-ness, and we buffer the whole file.
		result = ctx.xfer.data.empty() ? 0 : static_cast<int32_t>(ctx.xfer.data.size());
		return true;
	}
	if (fn == "close_transfer_file") {
		ctx.xfer.data.clear();
		ctx.xfer.rebuildPlacement();
		rt() << "  [close_transfer_file]" << std::endl;
		result = 0;
		return true;
	}
	// player_attrib(pc, attr, size): read `size` bytes for player `pc`'s attribute
	// `attr` from the buffered CREATE.SAV (see TransferState). The xfer bytecode
	// copies these straight into the PC object's statics, rebuilding the party.
	if (fn == "player_attrib" && args.size() >= 3) {
		result = ctx.xfer.playerAttrib(static_cast<int>(args[0]),
		                               static_cast<int>(args[1]),
		                               static_cast<int>(args[2]));
		return true;
	}
	// item_attrib(pc, slot, attr): a created PC's inventory slot -> item field
	// (see TransferState::itemAttrib). attr 1 = type (the transfer maps it to an
	// EOB3 item object via table123), 0 = flags, 2 = bonus.
	if (fn == "item_attrib" && args.size() >= 3) {
		result = ctx.xfer.itemAttrib(static_cast<int>(args[0]),
		                             static_cast<int>(args[1]),
		                             static_cast<int>(args[2]));
		return true;
	}
	// --- party movement math (EYE.C step_X/step_Y/step_FDIR) ---
	// The kernel's "step" handler asks these for the party's new coordinate/facing
	// given its current position+facing and a move-request direction. Direction
	// codes (from the kernel's move handlers): 1=turn left, 2=forward, 3=turn right,
	// 4=strafe left, 5=backward, 6=strafe right. Facing fdir: 0=N,1=E,2=S,3=W, with
	// N=-y, E=+x, S=+y, W=-x. The bytecode then collision-checks (impedance) and
	// commits, so these are pure geometry.
	if (fn == "step_FDIR" && args.size() >= 2) {
		int fdir = static_cast<int>(args[0]) & 3;
		int dir = static_cast<int>(args[1]);
		if (dir == 1) fdir = (fdir + 3) & 3;      // turn left
		else if (dir == 3) fdir = (fdir + 1) & 3; // turn right
		result = fdir;
		return true;
	}
	// distance(x1, y1, x2, y2) -- Euclidean distance rounded up to nearest int,
	// clamped to 0..31 via a 32-entry square-root table (EYE.C distance()).
	// Used by NPC "my turn"/"watch for party" to decide idle-schedule vs
	// engage-schedule and whether to enter melee range.
	if (fn == "distance" && args.size() >= 4) {
		int32_t dx = std::abs(static_cast<int32_t>(args[0]) -
		                     static_cast<int32_t>(args[2]));
		int32_t dy = std::abs(static_cast<int32_t>(args[1]) -
		                     static_cast<int32_t>(args[3]));
		int32_t num = dx * dx + dy * dy;
		static const int32_t sq[32] = {
		    0, 1, 4, 9, 16, 25, 36, 49, 64, 81, 100, 121, 144, 169, 196, 225,
		    256, 289, 324, 361, 400, 441, 484, 529, 576, 625, 676, 729, 784,
		    841, 900, 961};
		int32_t root = 0;
		while (root < 31 && sq[root] < num) ++root;
		result = root;
		return true;
	}
	// seek_direction(cur_x, cur_y, dest_x, dest_y) -- octal direction (N=0,
	// NE=1, E=2, ..., NW=7) cur should move to approach dest, or -1 if
	// already there (EYE.C seek_direction()).
	if (fn == "seek_direction" && args.size() >= 4) {
		int32_t dx = static_cast<int32_t>(args[2]) - static_cast<int32_t>(args[0]);
		int32_t dy = static_cast<int32_t>(args[3]) - static_cast<int32_t>(args[1]);
		int32_t d;
		if (dx < 0)      d = (dy > 0) ? 5 : (dy < 0) ? 7 : 6;
		else if (dx > 0) d = (dy > 0) ? 3 : (dy < 0) ? 1 : 2;
		else if (dy > 0) d = 4;
		else if (dy < 0) d = 0;
		else             d = -1;
		result = d;
		return true;
	}
	if ((fn == "step_X" || fn == "step_Y") && args.size() >= 4) {
		// Faithful port of EYE.C step_X/step_Y: DX/DY_offset[mtype-1][fdir],
		// mtype 1..6 = TL/F/TR/L/B/R (turns are all-zero rows), 7/8/9 = the
		// maze-passage moves ML/MM/MR (diagonal fwd+left / 2-fwd / fwd+right,
		// used by NPC pathing), coordinates wrap at the 32-cell level edge.
		static const int8_t DX[6][4] = {{0, 0, 0, 0},  {0, 1, 0, -1},
		                                {0, 0, 0, 0},  {-1, 0, 1, 0},
		                                {0, -1, 0, 1}, {1, 0, -1, 0}};
		static const int8_t DY[6][4] = {{0, 0, 0, 0},  {-1, 0, 1, 0},
		                                {0, 0, 0, 0},  {0, -1, 0, 1},
		                                {1, 0, -1, 0}, {0, 1, 0, -1}};
		const auto &D = (fn == "step_X") ? DX : DY;
		int coord = static_cast<int>(args[0]);
		int fdir = static_cast<int>(args[1]) & 3;
		int mtype = static_cast<int>(args[2]);
		int dist = static_cast<int>(args[3]);
		if (!dist) { result = coord; return true; }
		switch (mtype) {
		case 7: coord += D[1][fdir] + D[3][fdir]; break; // ML: fwd + left
		case 8: coord += 2 * D[1][fdir]; break;          // MM: two fwd
		case 9: coord += D[1][fdir] + D[5][fdir]; break; // MR: fwd + right
		case 0: break;                                   // MTYP_INIT
		default:
			if (mtype >= 1 && mtype <= 6)
				coord += dist * D[mtype - 1][fdir];
			break;
		}
		result = coord & 31; // LVL_X/LVL_Y - 1
		return true;
	}
	// step_square_X/Y(coord, region, dir) + step_region(region, dir): half-cell
	// projectile flight (EYE.C). A cell is split into 4 quadrant regions
	// (bit 0 = east half, bit 1 = south half); a missile advances one HALF cell
	// per tick -- the region bit toggles every tick, and the cell coordinate
	// only moves when the missile crosses the cell boundary (i.e. when it's
	// already in the leading half). dir is cardinal 0=N 1=E 2=S 3=W.
	if (fn == "step_square_X" && args.size() >= 3) {
		int x = static_cast<int>(args[0]);
		int r = static_cast<int>(args[1]);
		int dir = static_cast<int>(args[2]);
		if (dir == 1 && (r & 1)) x = (x + 1) & 31;       // E, from east half
		else if (dir == 3 && !(r & 1)) x = (x - 1) & 31; // W, from west half
		result = x;
		return true;
	}
	if (fn == "step_square_Y" && args.size() >= 3) {
		int y = static_cast<int>(args[0]);
		int r = static_cast<int>(args[1]);
		int dir = static_cast<int>(args[2]);
		if (dir == 0 && r < 2) y = (y - 1) & 31;       // N, from north half
		else if (dir == 2 && r >= 2) y = (y + 1) & 31; // S, from south half
		result = y;
		return true;
	}
	if (fn == "step_region" && args.size() >= 2) {
		int r = static_cast<int>(args[0]);
		int dir = static_cast<int>(args[1]);
		if (dir == 0 || dir == 2) r ^= 2;      // N/S: flip vertical half
		else if (dir == 1 || dir == 3) r ^= 1; // E/W: flip horizontal half
		result = r;
		return true;
	}
	// spell_request(stat, cnt, typ, num): does any of the first `num` spell
	// slots of type `typ` (0=mage, 1=cleric; +10/+110 into the [2][10][10]
	// arrays) have fewer memorized (cnt) than requested (stat)? Used per
	// 10-minute rest tick in camp. spell_list(cnt, typ, lvl, list, max):
	// flatten the per-spell counts of one level into a menu list of spell ids.
	// Both take pointers to the PC's B:spell_stat/B:spell_cnt static arrays.
	if (fn == "spell_request" && args.size() >= 4) {
		uint32_t toff = args[2] ? 110u : 10u;
		uint32_t num = static_cast<uint32_t>(args[3]);
		const int8_t *stat = reinterpret_cast<int8_t *>(
		    staticBytePtr(ctx, args[0], toff + num));
		const int8_t *cnt = reinterpret_cast<int8_t *>(
		    staticBytePtr(ctx, args[1], toff + num));
		result = 0;
		if (stat && cnt) {
			for (uint32_t i = 0; i < num; ++i) {
				int n = stat[toff + i], h = cnt[toff + i];
				if (n != -1 && h < n) { result = 1; break; }
			}
		}
		return true;
	}
	if (fn == "spell_list" && args.size() >= 5) {
		uint32_t lvl = static_cast<uint32_t>(args[2]);
		uint32_t l = 10u * (lvl - 1);
		uint32_t base = (args[1] ? 110u : 10u) + l;
		uint32_t max = static_cast<uint32_t>(args[4]);
		const int8_t *cnt = reinterpret_cast<int8_t *>(
		    staticBytePtr(ctx, args[0], base + 10));
		uint8_t *list = staticBytePtr(ctx, args[3], max);
		uint32_t num = 0;
		if (cnt && list && max > 0) {
			for (uint32_t i = 0; i < 10 && num < max; ++i) {
				int n = cnt[base + i];
				for (int j = 0; j < n && num < max; ++j)
					list[num++] = static_cast<uint8_t>(i + l);
			}
		}
		result = static_cast<VM::Value>(num);
		return true;
	}
	// magic_field(p, redfield, yelfield, sparkle): draw the shield/prayer
	// field border around PC portrait p (0..5, 2 cols x 3 rows at x>=176).
	// Solid rect for one field, alternating dashed red/yellow for both
	// (EYE.C magic_field, verbatim incl. the sparkle colour offset).
	if (fn == "magic_field" && args.size() >= 4) {
		result = 0;
		if (!ctx.gfx)
			return true;
		static const int px[2] = {8, 80};
		static const int py[3] = {2, 54, 106};
		int p = static_cast<int>(args[0]);
		bool red = args[1] != 0, yel = args[2] != 0;
		int32_t sparkle = args[3];
		uint8_t redC = 0x23, yelC = 0x37;
		if (sparkle != -1) {
			redC = static_cast<uint8_t>(redC + sparkle);
			yelC = static_cast<uint8_t>(yelC + sparkle);
		}
		int x = px[p & 1] + 176;
		int y = py[(p >> 1) % 3];
		auto &g = *ctx.gfx;
		if (red && !yel) {
			g.drawLine(x, y, x + 63, y, redC);
			g.drawLine(x, y + 49, x + 63, y + 49, redC);
			g.drawLine(x, y, x, y + 49, redC);
			g.drawLine(x + 63, y, x + 63, y + 49, redC);
		} else if (yel && !red) {
			g.drawLine(x, y, x + 63, y, yelC);
			g.drawLine(x, y + 49, x + 63, y + 49, yelC);
			g.drawLine(x, y, x, y + 49, yelC);
			g.drawLine(x + 63, y, x + 63, y + 49, yelC);
		} else if (red && yel) {
			for (int lp = 0; lp < 64; lp += 16) {
				int sx = x + lp;
				g.drawLine(sx, y, sx + 7, y, redC);
				g.drawLine(sx + 8, y + 49, sx + 15, y + 49, redC);
				g.drawLine(sx + 8, y, sx + 15, y, yelC);
				g.drawLine(sx, y + 49, sx + 7, y + 49, yelC);
			}
			for (int lp = 1; lp < 48; lp += 12) {
				int sy = y + lp - 1;
				g.drawLine(x, sy + 1, x, sy + 6, yelC);
				g.drawLine(x + 63, sy + 7, x + 63, sy + 12, yelC);
				g.drawLine(x, sy + 7, x, sy + 12, redC);
				g.drawLine(x + 63, sy + 1, x + 63, sy + 6, redC);
			}
		}
		return true;
	}
	// read_initial_items / arrow_count / write_initial_tempfiles: the rest of the
	// transfer. Reading items + writing the game's initial save files is still to
	// do (the savegame format); stubbed so "convert created party" runs through.
	if (fn == "read_initial_items" || fn == "arrow_count") {
		result = 0;
		return true;
	}
	// write_initial_tempfiles: called by the chargen-transfer SOP after PCs +
	// items have been built in memory. EYE.C does
	//   save_range(items_tmp, FIRST_ITEM, LAST_ITEM);  // serialize live state
	//   for (lvl in 1..14) copy_file(LVLnn_00.BIN, LVLnn.TMP);
	// LVL half: direct file copy (matches the C source).
	// ITEMS half: we (1) seed bytes 0..676 from ITEMS_00.BIN (the kernel-state
	// scaffold the engine doesn't need to round-trip yet), (2) overwrite the 10
	// PC records @677 with live PC statics field-by-field per docs §2.2, and
	// (3) replace the item stream @6947 with a fresh serialization of every
	// live item object (class is a subclass of `items` 1371). Resume_level
	// reads this file back and reconstructs the party.
	if (fn == "write_initial_tempfiles") {
		auto saveDir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		int lvlCopied = THIRDEYE::savegame::restoreLevels(saveDir, 0);
		// Initial (chargen) party: bootstrap the world item pool from
		// ITEMS_00.BIN, and keep the scaffold's graveyard-entrance position.
		int pcs = 0, items = 0, world = 0;
		bool itemsOk = writeItemsFixed(ctx, saveDir / "ITEMS.TMP",
		                               /*preInstantiateWorld=*/true,
		                               /*writeLivePosition=*/false,
		                               pcs, items, world);
		rt() << "  [write_initial_tempfiles: ITEMS.TMP "
		     << (itemsOk ? "written" : "FAILED") << " (" << pcs << " PCs, "
		     << items << " items, " << world << " world pre-created), "
		     << lvlCopied << " LVL??_00.BIN -> LVL??.TMP]" << std::endl;
		result = 0;
		return true;
	}
	// change_level(old_level, new_level): the native EYE.C function does BOTH
	// halves inside the call -- save_range the DEPARTING level's objects to
	// LVLoo.TMP (or opened doors / dead monsters resurrect on return), then
	// restore_range the new level. The restore must happen here: the
	// teleporter trigger path SENDs area "enter level" right after this
	// returns with NO "init level" in between (that only fires on the boot /
	// restore-game flow), so deferring the load to the init-level hook left
	// the new level empty and the old level's chains live. loadLevelObjects
	// clears the lvlobj planes itself, so the boot-flow hook reload stays
	// harmless (destroy-before-restore cancels the first load's notifies).
	if (fn == "change_level" && args.size() >= 2) {
		int oldLvl = static_cast<int>(args[0]);
		int newLvl = static_cast<int>(args[1]);
		if (oldLvl >= 1 && oldLvl <= 14) {
			char ll[16];
			std::snprintf(ll, sizeof(ll), "%02d", oldLvl);
			auto path = ctx.res.resourcePath().parent_path() / "SAVEGAME" /
			            ("LVL" + std::string(ll) + ".TMP");
			if (!THIRDEYE::savegame::saveRange(ctx.objects, path, 1000, 1999))
				std::cerr << "[change_level: failed saving " << path << "]"
				          << std::endl;
		}
		if (newLvl >= 1 && newLvl <= 14)
			THIRDEYE::savegame::loadLevelObjects(newLvl, ctx.objects,
			                                     ctx.events, ctx.res);
		result = 0;
		return true;
	}
	// load_resource(dest, resource): read a resource's bytes into an object's static
	// buffer (e.g. the dungeon's `lvlmap` = the 32x32 level maze). One arg is a
	// Static/Extern address (the destination), the other the resource number.
	if (fn == "load_resource" && args.size() >= 2) {
		VM::Addr da = VM::decodeAddr(args[0]);
		bool aIsAddr = da.space == VM::AddrSpace::Static ||
		               da.space == VM::AddrSpace::Extern;
		VM::Addr dest = aIsAddr ? da : VM::decodeAddr(args[1]);
		int resNum = static_cast<int>(aIsAddr ? args[1] : args[0]);
		try {
			std::vector<uint8_t> &bytes = ctx.res.getAsset(static_cast<uint16_t>(resNum));
			uint8_t *p = ctx.objects.staticsPtr(dest.obj, dest.offset,
			                                    static_cast<uint32_t>(bytes.size()));
			std::memcpy(p, bytes.data(), bytes.size());
			rt() << "  [load_resource " << resNum << " -> obj " << dest.obj
			     << " @" << dest.offset << " (" << bytes.size() << " B)]"
			     << std::endl;
		} catch (const std::exception &e) {
			rt() << "  [load_resource " << resNum << " failed: " << e.what()
			     << "]" << std::endl;
		}
		result = 0;
		return true;
	}
	// resume_level(level): the real one reconstructs the dungeon level + party from
	// the savegame (.TMP). Our stand-in does enough of the bring-up that the HUD
	// shows the party and the dungeon renders:
	//   1. Create the "entities" singleton (the draw/move code SENDs to obj 15).
	//   2. Register live PC objects in the kernel's `player[]` array.
	//   3. (THIRDEYE_CONTINUE) patch each PC's name/HP/XP/abilities from ITEMS.TMP
	//      -- chargen ran first, so the unknown fields (race/classes/PCstat/etc.)
	//      stay as chargen set them; we just overwrite what the save file owns.
	//   4. Seed the party's dungeon position (THIRDEYE_PARTY/GOTO env vars or
	//      ITEMS.TMP, falling back to level 1).
	//   5. Load the level's tiles.
	if (fn == "resume_level") {
		constexpr uint16_t kKernelClass = 1382, kPcClass = 1369;
		constexpr uint16_t kEntitiesClass = 1370;
		constexpr uint32_t kPlayerOff = 229, kPlayerSlots = 6, kPcNumOff = 0;
		// The dungeon "entities" manager is a singleton the draw/move code addresses
		// at the fixed object index 15 (e.g. SXW place@1370 to obj 15). The real
		// resume_level rebuilds it from the level state; create an empty one so the
		// party-draw's extern writes land somewhere live instead of crashing.
		constexpr int kEntitiesIndex = 15;
		if (ctx.objects.firstObjectOfClass(kEntitiesClass) < 0)
			ctx.objects.createProgram(kEntitiesIndex, kEntitiesClass);
		int kernel = ctx.objects.firstObjectOfClass(kKernelClass);

		// Continue-from-save is now gated on "does the save file actually exist"
		// instead of the THIRDEYE_CONTINUE env var (legacy). With the boot-mode
		// auto-detect in bootObject, the same file-existence check also picks
		// MODE_CINE upstream -- so when we arrive here in CINE mode (no chargen
		// ran), wantContinue is true and we create+patch PCs from scratch below.
		auto savePath =
		    ctx.res.resourcePath().parent_path() / "SAVEGAME" / "ITEMS.TMP";
		std::error_code ec;
		bool wantContinue = std::filesystem::exists(savePath, ec);
		THIRDEYE::savegame::ItemsTmp items;
		if (wantContinue)
			items = THIRDEYE::savegame::loadItemsTmp(savePath);

		if (kernel >= 0) {
			auto setSlot = [&](uint32_t slot, int16_t val) {
				if (slot >= kPlayerSlots) return;
				if (uint8_t *p = ctx.objects.classStaticPtr(kernel, kKernelClass,
				                                            kPlayerOff + slot * 2, 2)) {
					p[0] = val & 0xFF;
					p[1] = (val >> 8) & 0xFF;
				}
			};
			for (uint32_t s = 0; s < kPlayerSlots; ++s)
				setSlot(s, -1); // empty all slots first
			int placed = 0;
			// Prefer ITEMS.TMP's slot order for player[]: each file record's
			// index IS the party slot (0..5 = party, 6..9 = reserve roster).
			// Fall back to PC.W:num only when there's no save file (chargen).
			// Reading PC.W:num of already-live objects during an in-game
			// restore was the "Rex ended up in slot 0" bug: existing PCs kept
			// stale W:num from prior state (Rex's obj-41 record had never had
			// its W:num rewritten since it was created for the reserve slot).
			if (wantContinue && !items.characters.empty()) {
				for (size_t slotIdx = 0; slotIdx < items.characters.size() &&
				     slotIdx < kPlayerSlots; ++slotIdx) {
					const auto &c = items.characters[slotIdx];
					if (c.classNumber != 1369) continue;
					if (c.objectIndex <= 0) continue;
					setSlot(static_cast<uint32_t>(slotIdx),
					        static_cast<int16_t>(c.objectIndex));
					++placed;
				}
			} else {
				for (int pc : ctx.objects.objectsOfClass(kPcClass)) {
					uint8_t *num = ctx.objects.classStaticPtr(pc, kPcClass, kPcNumOff, 1);
					int slot = num ? *num : -1;
					if (slot >= 0 && static_cast<uint32_t>(slot) < kPlayerSlots) {
						setSlot(static_cast<uint32_t>(slot), static_cast<int16_t>(pc));
						++placed;
					}
				}
			}

			// Slots the §2.3 stream explicitly mentions (live OR empty). An
			// explicit empty means "the player consumed this item" -- the
			// gap-fill below must NOT resurrect it from ITEMS_00.BIN.
			std::vector<uint16_t> coveredSlots;
			// (2.5) Recreate the live item objects from ITEMS.TMP §2.3 so the
			// PCs' equip[] pointers below resolve to real SOP objects (chain mail,
			// sword, holy symbol, etc.). createProgram allocates the SOP instance
			// (sending MSG_CREATE for first-time init); the saved static block is
			// then memcpy'd in to restore the item's persistent state.
			if (wantContinue && items.itemStreamOff > 0) {
				// Load the raw bytes once -- parseItemStream needs the buffer.
				auto path = ctx.res.resourcePath().parent_path() / "SAVEGAME" / "ITEMS.TMP";
				std::ifstream f(path, std::ios::binary);
				std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
				                          std::istreambuf_iterator<char>());
				auto stream = THIRDEYE::savegame::parseItemStream(
				    raw, items.itemStreamOff, &coveredSlots);
				int created = 0, failed = 0;
				for (const auto &rec : stream) {
					try {
						ctx.objects.createProgram(rec.id, rec.cls);
						// CDESC statics restore verbatim, matching the native
						// restore_range -- itmflags, arms.B:bonus, and the
						// entity place/x/y/lvl fields all arrive in-frame.
						// (The old 4-byte-shifted parse needed itmflags and
						// bonus patch-ups here; see items_tmp.hpp history.)
						if (uint8_t *p = ctx.objects.staticsPtr(
						        rec.id, 0,
						        static_cast<uint32_t>(rec.staticBlock.size())))
							std::memcpy(p, rec.staticBlock.data(),
							            rec.staticBlock.size());
						++created;
					} catch (const std::exception &) { ++failed; }
				}
				rt() << "  [resume_level: recreated " << created
				     << " item objects (" << coveredSlots.size()
				     << " slots covered) from ITEMS.TMP §2.3"
				     << (failed ? " (" + std::to_string(failed) + " failed)" : "")
				     << "]" << std::endl;
			}

			// Fill any world-item slots (15..999) that ITEMS.TMP §2.3 didn't
			// carry -- the initial pool of potions/scrolls/weapons that
			// niches, monsters, and containers reference. Older ITEMS.TMP
			// files (written before write_initial_tempfiles pre-created them)
			// don't have any, so this loads them from the pristine ITEMS_00.BIN.
			// Only pull entries whose class is a subclass of `items` (1371):
			// ITEMS_00.BIN has PC records (class 1369) at slots 32..41 too,
			// and re-creating those clobbers the user's ITEMS.TMP-restored
			// party -- that's how the "2 NPC party members vanished" bug shipped.
			// Slot 15 (bare-hands weapon) is also skipped: our runtime pins
			// the entities singleton there, and bare-hands lives elsewhere.
			// THIRDEYE_NO_WORLDITEMS=1 disables this entirely (bisect helper).
			if (wantContinue && std::getenv("THIRDEYE_NO_WORLDITEMS") == nullptr) {
				constexpr uint16_t kItemsBaseCls = 1371;
				auto saveDir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
				// O(1) membership for the stream-covered slots (see above).
				std::vector<bool> covered(1000, false);
				for (uint16_t cs2 : coveredSlots)
					if (cs2 < covered.size()) covered[cs2] = true;
				// Count actual creations ourselves: loadAreaInstances' return
				// counts callback VISITS, so the covered-slot early-returns
				// (and the other guards) would still tally and the trace
				// would claim resurrections that never happened.
				int gapFilled = 0;
				THIRDEYE::savegame::loadAreaInstances(
				    saveDir,
				    [&](int slot, uint16_t cls,
				        const std::vector<uint8_t> &data) {
					    if (slot == 15) return; // reserved for entities singleton
					    // Explicitly-covered slot (live record already
					    // recreated, or explicit empty = consumed item):
					    // never refill from the pristine pool.
					    if (slot >= 0 &&
					        static_cast<size_t>(slot) < covered.size() &&
					        covered[slot]) return;
					    if (!ctx.objects.isSubclassOf(cls, kItemsBaseCls)) return;
					    if (ctx.objects.classOf(slot) != 0xFFFF) return;
					    try {
						    ctx.objects.createProgram(slot, cls);
						    if (!data.empty())
							    if (uint8_t *sp = ctx.objects.staticsPtr(
							            slot, 0,
							            static_cast<uint32_t>(data.size())))
								    std::memcpy(sp, data.data(), data.size());
						    ++gapFilled;
					    } catch (const std::exception &) {}
				    },
				    /*firstSlot=*/15, /*lastSlot=*/999);
				rt() << "  [resume_level: gap-filled " << gapFilled
				     << " world items from ITEMS_00.BIN]" << std::endl;
			}

			// (3) PC patching from ITEMS.TMP. PC class static offsets verified
			// against EYE.RES res 1369 disassembly's exported-variable table.
			// Equipment slots are now restored too -- file slots 0..11 (body,
			// bracers, rhand, lring, rring, boots, lhand, pouchA, pouchB, pouchC,
			// necklace, helmet) map to W:inventory[14..25] in the live PC class.
			if (wantContinue && !items.characters.empty()) {
				// PC class statics offsets (= file offset - 18). From EYE.RES
				// res 1369's exported-variable table.
				constexpr uint32_t kNameOff = 137, kNameLen = 20;
				constexpr uint32_t kRaceOff = 157, kClassesOff = 158;
				constexpr uint32_t kPortraitOff = 159, kPCstatOff = 161;
				constexpr uint32_t kAlignmentOff = 162, kLevelsOff = 163;
				constexpr uint32_t kLostLevelsOff = 166, kLostHpOff = 169;
				constexpr uint32_t kHpCurOff = 171, kHpMaxOff = 173, kHbonOff = 175;
				constexpr uint32_t kFoodOff = 177;
				constexpr uint32_t kXpOff = 179; // L:experience[0..2]
				constexpr uint32_t kStrOff = 191, kExcStrOff = 192, kIntOff = 193;
				constexpr uint32_t kWisOff = 194, kDexOff = 195, kConOff = 196;
				constexpr uint32_t kChaOff = 197;
				int patched = 0, createdPcs = 0;
				for (size_t slotIdx = 0; slotIdx < items.characters.size(); ++slotIdx) {
					const auto &c = items.characters[slotIdx];
					if (c.classNumber != 1369) continue;
					int idx = c.objectIndex;
					if (idx <= 0) continue;
					// In CINE mode (boot bypassed chargen because a save exists)
					// the PC objects don't exist yet -- create them. In CHGN mode
					// they were just created by the chargen-transfer SOP and this
					// is a no-op. We also need to seed PC.W:num (offset 0) with the
					// slot index AND register the PC in kernel.player[slot] so the
					// HUD finds it -- chargen would have done that, CINE didn't.
					bool freshlyCreated = false;
					if (ctx.objects.objectLookup(idx) != idx) {
						try {
							ctx.objects.createProgram(idx, kPcClass);
							++createdPcs;
							freshlyCreated = true;
						} catch (const std::exception &) { continue; }
					}
					// PC.W:num is the PC's own copy of its party slot. Refresh
					// it from the file's slotIdx on EVERY restore, not only on
					// fresh-create -- otherwise existing PCs keep stale slot
					// numbers across sessions (the "Rex in slot 0" bug). The
					// player[] registration happens in the pre-load pass above,
					// which also uses slotIdx as the party position.
					if (uint8_t *np = ctx.objects.classStaticPtr(
					        idx, kPcClass, kPcNumOff, 1))
						*np = static_cast<uint8_t>(slotIdx);
					if (freshlyCreated && slotIdx < kPlayerSlots)
						++placed;
					// L:timer[0..15] (16 longs @ offset 1) = -1 "inactive". createProgram
					// zero-fills; the heartbeat treats timer == 0 as "fire now" and runs the
					// slot handler (slot 5 = poison damage, 8 = disease, 10 = lvl drain,
					// etc.), so a fresh PC starts poisoning itself on the first tick. -1
					// means "no event scheduled" and the handler skips.
					if (uint8_t *tp = ctx.objects.classStaticPtr(idx, kPcClass, 1, 64))
						std::memset(tp, 0xFF, 64);

					auto wb = [&](uint32_t off, int sz, int32_t v) {
						if (uint8_t *p = ctx.objects.classStaticPtr(idx, kPcClass, off, sz))
							for (int i = 0; i < sz; ++i) p[i] = (v >> (8 * i)) & 0xFF;
					};
					// Name: copy bytes incl. NUL terminator, clear the rest of the field.
					if (uint8_t *np = ctx.objects.classStaticPtr(idx, kPcClass, kNameOff,
					                                              kNameLen)) {
						size_t n = c.name.size() < kNameLen - 1 ? c.name.size()
						                                       : kNameLen - 1;
						for (size_t i = 0; i < n; ++i)
							np[i] = static_cast<uint8_t>(c.name[i]);
						for (size_t i = n; i < kNameLen; ++i) np[i] = 0;
					}
					wb(kRaceOff,      1, c.race);
					wb(kClassesOff,   1, c.classes);
					wb(kPortraitOff,  2, c.portrait);
					wb(kPCstatOff,    1, c.PCstat);
					wb(kAlignmentOff, 1, c.alignment);
					for (int i = 0; i < 3; ++i) {
						wb(kLevelsOff + i,     1, c.levels[i]);
						wb(kLostLevelsOff + i, 1, c.lostLevels[i]);
						wb(kXpOff + i * 4,     4, c.xp[i]);
					}
					wb(kLostHpOff, 2, c.lostHp);
					wb(kHpCurOff,  2, c.hpCurrent);
					wb(kHpMaxOff,  2, c.hpMax);
					wb(kHbonOff,   2, c.hbon);
					wb(kFoodOff,   2, c.foodPct);
					wb(kStrOff,    1, c.str);
					wb(kExcStrOff, 1, c.strPct);
					wb(kIntOff,    1, c.intel);
					wb(kWisOff,    1, c.wis);
					wb(kDexOff,    1, c.dex);
					wb(kConOff,    1, c.con);
					wb(kChaOff,    1, c.cha);
					// Tail (spell state + active magic effects + 4 smaller fields).
					// PC class offsets: sparkle 198, magiceffects 199, tiger 203,
					// lost_str 204, unknown 205..208, spell_cnt 209..408, spell_stat
					// 409..608.
					constexpr uint32_t kSparkleOff    = 198;
					constexpr uint32_t kMagicEffOff   = 199;
					constexpr uint32_t kTigerOff      = 203;
					constexpr uint32_t kLostStrOff    = 204;
					constexpr uint32_t kUnknownGapOff = 205;
					constexpr uint32_t kSpellCntOff   = 209;
					constexpr uint32_t kSpellStatOff  = 409;
					wb(kSparkleOff,    1, c.sparkle);
					wb(kMagicEffOff,   4, c.magicEffects);
					wb(kTigerOff,      1, c.tiger);
					wb(kLostStrOff,    1, c.lostStr);
					wb(kUnknownGapOff, 4, static_cast<int32_t>(c.unknownGap));
					if (uint8_t *p = ctx.objects.classStaticPtr(
					        idx, kPcClass, kSpellCntOff,
					        static_cast<uint32_t>(c.spellCnt.size())))
						std::memcpy(p, c.spellCnt.data(), c.spellCnt.size());
					if (uint8_t *p = ctx.objects.classStaticPtr(
					        idx, kPcClass, kSpellStatOff,
					        static_cast<uint32_t>(c.spellStat.size())))
						std::memcpy(p, c.spellStat.data(), c.spellStat.size());
					// W:inventory[14..25] <- file slots 0..11 (body, bracers, rhand,
					// lring, rring, boots, lhand, pouchA, pouchB, pouchC, necklace,
					// helmet). Each slot is one word (item-object id).
					constexpr uint32_t kInventoryOff = 81;
					for (int s = 0; s < THIRDEYE::savegame::ItemsTmp::Character::kEquipSlots; ++s) {
						uint32_t off = kInventoryOff + (14 + s) * 2;
						int16_t v = c.equip[s];
						if (uint8_t *p = ctx.objects.classStaticPtr(idx, kPcClass, off, 2)) {
							p[0] = static_cast<uint8_t>(v & 0xFF);
							p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
						}
					}
					// B:sp_lvl[2] @77 / B:mn_lvl[2] @79 <- file +95..+98. The
					// camp spell menu opens at mn_lvl; DOS saves carry 1s --
					// left zero-filled it shows "Level 0: 0 of 0 Available".
					for (int k = 0; k < 2; ++k) {
						if (uint8_t *p = ctx.objects.classStaticPtr(idx, kPcClass, 77 + k, 1))
							*p = c.spLvl[k];
						if (uint8_t *p = ctx.objects.classStaticPtr(idx, kPcClass, 79 + k, 1))
							*p = c.mnLvl[k];
					}
					// W:inventory[0..13] <- backpack @+99. Empty = -1: a
					// zero-filled slot reads as "item object 0" and the
					// inventory click handler picks the kernel up as an item.
					for (int s = 0; s < THIRDEYE::savegame::ItemsTmp::Character::kBackpackSlots; ++s) {
						uint32_t off = kInventoryOff + s * 2;
						int16_t v = c.backpack[s];
						if (uint8_t *p = ctx.objects.classStaticPtr(idx, kPcClass, off, 2)) {
							p[0] = static_cast<uint8_t>(v & 0xFF);
							p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
						}
					}
					++patched;
					rt() << "  [resume_level: patched PC " << idx << " <- \""
					     << c.name << "\" HP " << c.hpCurrent << "/" << c.hpMax
					     << " STR " << int(c.str) << "/" << int(c.strPct)
					     << " XP " << c.xp[0]
					     << " equip body=" << c.equip[0]
					     << " rh=" << c.equip[2]
					     << " lh=" << c.equip[6] << "]" << std::endl;
				}
				rt() << "  [resume_level: " << patched
				     << " PCs patched from ITEMS.TMP (with equip[])"
				     << (createdPcs ? "; " + std::to_string(createdPcs) +
				                      " created from scratch (CINE)"
				                    : "")
				     << "]" << std::endl;
				// SEND each live PC "restore" (M:2). PC.restore registers the
				// heartbeat notify via PROCEDURE_389 (notify(THIS, 154, SYS_TIMER,
				// tick+6)) and resets hand flags. Without it L:timer never
				// decrements, so the swing's `<MISS>`/`<HIT>` overlay -- set when
				// h_stat |= 4 -- never clears.
				for (size_t slotIdx = 0; slotIdx < items.characters.size(); ++slotIdx) {
					const auto &c = items.characters[slotIdx];
					if (c.classNumber != 1369) continue;
					int idx = c.objectIndex;
					if (idx <= 0) continue;
					if (ctx.objects.objectLookup(idx) != idx) continue;
					try { ctx.objects.send(idx, 2, {}); }
					catch (const std::exception &) {}
				}
			}

			// (4) Position seeding.
			constexpr uint32_t kPartyX = 243, kPartyY = 244, kPartyFdir = 245,
			                   kPartyLvl = 246;
			auto setKByte = [&](uint32_t off, uint8_t val) {
				if (uint8_t *p = ctx.objects.classStaticPtr(kernel, kKernelClass, off, 1))
					*p = val;
			};
			// Reseed party position when a save file exists. The old
			// "don't clobber if party_lvl != 0" guard was meant to skip the
			// fallback default on a fresh chargen boot, but it also skipped
			// the *ITEMS.TMP* seed on the in-game restore-game flow: the
			// kernel already had a party_lvl from the pre-restore session,
			// so the newly-copied ITEMS.TMP's position was ignored and the
			// party stayed at wherever they were before clicking Restore.
			// New rule: if wantContinue is true (ITEMS.TMP exists) we always
			// apply its position; otherwise fall through and keep whatever
			// the SOP has already set (chargen path, if any).
			uint8_t *lvlP = ctx.objects.classStaticPtr(kernel, kKernelClass, kPartyLvl, 1);
			if (lvlP && (wantContinue || *lvlP == 0)) {
				// Priority: ITEMS.TMP > default (lvl 1 (15,15)). Drive level
				// changes via the normal SOP path (menu / AUTOWALK) rather than
				// a debug override -- THIRDEYE_GOTO bypassed program/window
				// state the SOP relies on and produced page-numbering and HUD
				// glitches that didn't reproduce in real gameplay.
				// Default start: matches the shipped Quick Start Party cell in
				// SAVEGAME/ITEMS_00.BIN -- (7, 24) facing east on the graveyard
				// (LVL03). LVL01 (mausoleum) is NOT the narrative beginning of
				// EOB3; the graveyard is. The chargen-transfer's
				// write_initial_tempfiles preserves this position from the
				// scaffold, so the only time this default kicks in is when
				// resume_level is called with a fresh kernel but no save data
				// at all (rare; mostly debugging).
				uint8_t startLvl = 3;
				int px = 7, py = 24, pf = 1;
				bool seededFromSave = false;
				if (wantContinue && items.position.level >= 1 &&
				    items.position.level <= 14) {
					startLvl = items.position.level;
					px = items.position.x;
					py = items.position.y;
					pf = items.position.facing & 3;
					seededFromSave = true;
				}
				if (const char *pp = std::getenv("THIRDEYE_PARTY")) { // debug override
					if (std::sscanf(pp, "%d,%d,%d", &px, &py, &pf) == 3)
						seededFromSave = false;
				}
				setKByte(kPartyX, static_cast<uint8_t>(px));
				setKByte(kPartyY, static_cast<uint8_t>(py));
				setKByte(kPartyFdir, static_cast<uint8_t>(pf));
				setKByte(kPartyLvl, startLvl);
				rt() << "  [resume_level: seeded party at level " << int(startLvl)
				     << " (" << px << "," << py << ") facing " << pf
				     << (seededFromSave ? " (from ITEMS.TMP)" : " (default/env)")
				     << "]" << std::endl;
			}
			rt() << "  [resume_level: registered " << placed
			     << " party members in player[] (stand-in for savegame load)]"
			     << std::endl;
			// W:in_hand@241 sentinel: -1 = empty hand. create_program zero-fills
			// it to 0, which "take/drop topmost object" (M:248) reads as "holding
			// object 0" -- so every floor click took the DROP path and no item
			// could ever be picked up. The original save/new-game kernel record
			// carries -1 here; seed it since our resume path builds the kernel.
			if (uint8_t *ih = ctx.objects.classStaticPtr(kernel, kKernelClass, 241, 2)) {
				ih[0] = 0xFF;
				ih[1] = 0xFF;
			}
			// B:bar_graphs@247 + B:sounds@248: kernel OPTION bytes. The kernel
			// only ever READS B:sounds (kernel.dasm 4316-4320: "activate
			// adventure screen" does set_sound_status(B:sounds)); the original
			// restores the value with the object-0 record from ITEMS.TMP
			// (save_range 0..999 includes the kernel). Our stand-in resume
			// doesn't restore the kernel record, so the zero-filled byte turned
			// ALL in-game SFX/music off at boot. Seed from the save (both are 1
			// in the shipped QSP scaffold; parser defaults to 1 without a save).
			if (uint8_t *op = ctx.objects.classStaticPtr(kernel, kKernelClass, 247, 2)) {
				op[0] = items.barGraphs;
				op[1] = items.soundsOn;
			}
		}
		// Load the level's objects (doors/levers/monsters/items from LVLnn.TMP)
		// into the dungeon. The maze data, wall-set bitmap number, and all
		// three palettes (walls + PAL_M1 + PAL_M2) are loaded by the SOP
		// "enter level" cascade -- see the loadAreaInstances comment below.
		uint8_t lvl = 1;
		if (kernel >= 0)
			if (uint8_t *p = ctx.objects.classStaticPtr(kernel, kKernelClass, 246, 1))
				lvl = *p ? *p : 1;
		THIRDEYE::savegame::loadLevelObjects(lvl, ctx.objects, ctx.events,
		                                     ctx.res);
		// Pre-create the 14 area-class singletons from ITEMS_00.BIN. They live
		// at object slots 1..14 in the native CDESC format and are what dungeon's
		// SOP "init level" handler looks up via SOLE -- once present, the kernel
		// SOP's natural `SEND dungeon "init level"` (which fires right after we
		// return) propagates `SEND area, "enter level"` and the area class then
		// calls `set_palette(1, walls), set_palette(2, M1), set_palette(3, M2)`
		// directly from bytecode. No C++-side palette enumeration needed.
		auto saveDir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		int areaCount = THIRDEYE::savegame::loadAreaInstances(
		    saveDir, [&](int slot, uint16_t cls,
		                 const std::vector<uint8_t> &data) {
			    try {
				    ctx.objects.createProgram(slot, cls);
				    if (!data.empty())
					    if (uint8_t *sp = ctx.objects.staticsPtr(
					            slot, 0, static_cast<uint32_t>(data.size())))
						    std::memcpy(sp, data.data(), data.size());
			    } catch (const std::exception &) {}
		    });
		rt() << "  [resume_level: instantiated " << areaCount
		     << " area-class singletons (SOP \"enter level\" cascade)]"
		     << std::endl;
		// Register a one-time hook to re-populate lvlobj after dungeon.M:232
		// "init level" fires from kernel.enter_game (right after resume_level
		// returns). init_level clears all 3 lvlobj planes to -1 -- our earlier
		// loadLevelObjects placement is lost otherwise, so acquire_NPC_target
		// finds an empty grid and every swing returns "no target". Also fires
		// on change_level, keeping the semantics consistent across boot + level
		// transitions.
		// NB: capture the long-lived engine objects, NOT `&ctx` -- Context is a
		// stack local of defaultRuntimeCall and is gone by the time this hook
		// fires. The old `[&ctx]` capture was a dangling reference that only
		// "worked" while the dead stack bytes still held the references; adding
		// a Context member shifted the frame layout and it became a segfault.
		ctx.objects.setPostSendHook(
		    [&objects = ctx.objects, &events = ctx.events,
		     &res = ctx.res](int objIndex, int message) {
			    if (objIndex != 2001 || message != 232) return;
			    uint8_t lvl = 1;
			    int kn = objects.firstObjectOfClass(1382);
			    if (kn >= 0)
				    if (uint8_t *p = objects.classStaticPtr(kn, 1382, 246, 1))
					    lvl = *p ? *p : 1;
			    THIRDEYE::savegame::loadLevelObjects(lvl, objects, events, res);
		    });
		result = 0;
		return true;
	}
	return false;
}

} // namespace THIRDEYE::runtime::eye
