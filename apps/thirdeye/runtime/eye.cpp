#include "internal.hpp"

#include "../resources/res.hpp"
#include "../savegame/items_tmp.hpp"
#include "../savegame/savegame_dir.hpp"
#include "../savegame/transfer.hpp"
#include "../vm/objects.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
		std::memset(p, 0, kSlotNameLen);
		if (name.empty()) {
			std::memset(p, '_',
			            std::min<size_t>(kEmptyMarkLen, kSlotNameLen - 1));
		} else {
			size_t n = std::min(name.size(),
			                    static_cast<size_t>(kSlotNameLen - 1));
			std::memcpy(p, name.data(), n);
		}
		result = VM::makeAddr(VM::AddrSpace::Static,
		                      static_cast<uint32_t>(slot * kSlotNameLen),
		                      kSaveBufObj);
		rt() << "  [savegame_title slot=" << slot
		     << (name.empty() ? " -> empty (\"_____\")" : " -> \"" + name + "\"")
		     << "]" << std::endl;
		return true;
	}
	// restore_items(slot) -- copy SAVEGAME/ITEMS_(slot-1):02d.BIN -> ITEMS.TMP.
	// The SOP's save picker calls this after the user clicks a used slot;
	// resume_level later reads ITEMS.TMP and seeds the party from it.
	if (fn == "restore_items" && args.size() >= 1) {
		int slot = static_cast<int>(args[0]);
		int idx  = slot - 1; // SOP is 1-based; backup files are 00, 01.
		auto dir = ctx.res.resourcePath().parent_path() / "SAVEGAME";
		bool ok = THIRDEYE::savegame::restoreItems(dir, idx);
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
		int idx  = slot - 1;
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
		// strcmp semantics: 0 = equal, nonzero = different. Both args are
		// tagged addresses (Code string OR Static/Extern buffer); readString
		// handles either and returns "" for an unresolvable address.
		std::string a = ctx.vm.readString(args[0]);
		std::string b = ctx.vm.readString(args[1]);
		int cmp = a.compare(b);
		result = cmp;
		rt() << "  [string_compare \"" << a << "\" vs \"" << b
		     << "\" -> " << cmp << "]" << std::endl;
		return true;
	}

	// --- char-gen party transfer (EYE.C transfer-file API) ---
	// open_transfer_file(name): buffer the CHGEN.EXE output (CHARGEN\CREATE.SAV)
	// so player_attrib/item_attrib can read the created party out of it. The DOS
	// path uses a backslash and is relative to the game dir; map it to the real
	// sibling file beside the .RES.
	if (fn == "open_transfer_file" && args.size() >= 1) {
		std::string name = ctx.vm.readCodeString(static_cast<uint32_t>(args[0]));
		// Map the DOS path (e.g. "CHARGEN\CREATE.SAV") to a real sibling of the
		// .RES: split on '\\' and rebuild with the host separator.
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
		     << " (" << ctx.xfer.data.size() << " bytes)]" << std::endl;
		for (int pc = 0; pc < 4 && !ctx.xfer.data.empty(); ++pc) {
			rt() << "  [xfer pc " << pc << " placement:";
			for (int s = 0; s < 26; ++s)
				if (ctx.xfer.pcItemAtSlot[pc][s] >= 0)
					rt() << " e" << s << "<-cs" << int(ctx.xfer.pcItemAtSlot[pc][s]);
			rt() << "]" << std::endl;
		}
		result = ctx.xfer.data.empty() ? -1 : 0;
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
	if ((fn == "step_X" || fn == "step_Y") && args.size() >= 4) {
		int coord = static_cast<int>(args[0]);
		int fdir = static_cast<int>(args[1]) & 3;
		int dir = static_cast<int>(args[2]);
		int dist = static_cast<int>(args[3]);
		int eff; // effective compass direction of the step (-1 = no move, a turn)
		switch (dir) {
		case 2: eff = fdir; break;            // forward
		case 5: eff = (fdir + 2) & 3; break;  // backward
		case 4: eff = (fdir + 3) & 3; break;  // strafe left
		case 6: eff = (fdir + 1) & 3; break;  // strafe right
		default: result = coord; return true; // turns: position unchanged
		}
		static const int dx[4] = {0, 1, 0, -1}; // N,E,S,W
		static const int dy[4] = {-1, 0, 1, 0};
		result = coord + (fn == "step_X" ? dx[eff] : dy[eff]) * dist;
		return true;
	}
	// read_initial_items / arrow_count / write_initial_tempfiles: the rest of the
	// transfer. Reading items + writing the game's initial save files is still to
	// do (the savegame format); stubbed so "convert created party" runs through.
	if (fn == "read_initial_items" || fn == "arrow_count") {
		result = 0;
		return true;
	}
	if (fn == "write_initial_tempfiles") {
		rt() << "  [write_initial_tempfiles: stub -- party not persisted yet]"
		     << std::endl;
		result = 0;
		return true;
	}
	// change_level(old_level, new_level): the native EYE.C function that swaps the
	// dungeon to a new level. The bytecode then sets party_lvl=new_level + SENDs
	// "init level" (which reads the loaded tiles). We load the new level's maze +
	// tileset; the bytecode/stairs logic owns the party's landing position.
	if (fn == "change_level" && args.size() >= 2) {
		loadDungeonLevel(static_cast<int>(args[1]), ctx.objects, ctx.res, ctx.gfx);
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
			for (int pc : ctx.objects.objectsOfClass(kPcClass)) {
				uint8_t *num = ctx.objects.classStaticPtr(pc, kPcClass, kPcNumOff, 1);
				int slot = num ? *num : -1;
				if (slot >= 0 && static_cast<uint32_t>(slot) < kPlayerSlots) {
					setSlot(static_cast<uint32_t>(slot), static_cast<int16_t>(pc));
					++placed;
				}
			}

			// (2.5) Recreate the live item objects from ITEMS.TMP §2.3 so the
			// PCs' equip[] pointers below resolve to real SOP objects (chain mail,
			// sword, holy symbol, etc.). createProgram allocates the SOP instance
			// (sending MSG_CREATE for first-time init); the saved static block is
			// then memcpy'd in to restore the item's persistent state.
			if (wantContinue && items.itemStreamOff > 0) {
				auto staticSize = [&](uint16_t cls) -> uint32_t {
					try { return ctx.objects.instanceStaticSize(cls); }
					catch (const std::exception &) { return 0; }
				};
				// Load the raw bytes once -- parseItemStream needs the buffer.
				auto path = ctx.res.resourcePath().parent_path() / "SAVEGAME" / "ITEMS.TMP";
				std::ifstream f(path, std::ios::binary);
				std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),
				                          std::istreambuf_iterator<char>());
				auto stream = THIRDEYE::savegame::parseItemStream(
				    raw, items.itemStreamOff, staticSize);
				int created = 0, failed = 0;
				for (const auto &rec : stream) {
					try {
						ctx.objects.createProgram(rec.id, rec.cls);
						if (uint8_t *p = ctx.objects.staticsPtr(
						        rec.id, 0,
						        static_cast<uint32_t>(rec.staticBlock.size())))
							std::memcpy(p, rec.staticBlock.data(),
							            rec.staticBlock.size());
						++created;
					} catch (const std::exception &) { ++failed; }
				}
				rt() << "  [resume_level: recreated " << created
				     << " item objects from ITEMS.TMP §2.3"
				     << (failed ? " (" + std::to_string(failed) + " failed)" : "")
				     << "]" << std::endl;
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
					if (freshlyCreated && slotIdx < kPlayerSlots) {
						// W:num is the PC's own copy of its party slot.
						if (uint8_t *np = ctx.objects.classStaticPtr(
						        idx, kPcClass, kPcNumOff, 1))
							*np = static_cast<uint8_t>(slotIdx);
						setSlot(static_cast<uint32_t>(slotIdx), static_cast<int16_t>(idx));
						++placed;
					}

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
			}

			// (4) Position seeding.
			constexpr uint32_t kPartyX = 243, kPartyY = 244, kPartyFdir = 245,
			                   kPartyLvl = 246;
			auto setKByte = [&](uint32_t off, uint8_t val) {
				if (uint8_t *p = ctx.objects.classStaticPtr(kernel, kKernelClass, off, 1))
					*p = val;
			};
			uint8_t *lvlP = ctx.objects.classStaticPtr(kernel, kKernelClass, kPartyLvl, 1);
			if (lvlP && *lvlP == 0) { // don't clobber a level a real save provided
				// Priority: ITEMS.TMP > default (lvl 1 (15,15)). Drive level
				// changes via the normal SOP path (menu / AUTOWALK) rather than
				// a debug override -- THIRDEYE_GOTO bypassed program/window
				// state the SOP relies on and produced page-numbering and HUD
				// glitches that didn't reproduce in real gameplay.
				uint8_t startLvl = 1;
				int px = 15, py = 15, pf = 0;
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
					std::sscanf(pp, "%d,%d,%d", &px, &py, &pf);
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
		}
		// Load the level's tiles (maze + wall set + palette) into the dungeon. The
		// real resume_level reads these from LVLnn.TMP; this stand-in loads the static
		// map resource + its tileset (see loadDungeonLevel) so "init level"/"draw
		// walls" have real data instead of an empty level.
		uint8_t lvl = 1;
		if (kernel >= 0)
			if (uint8_t *p = ctx.objects.classStaticPtr(kernel, kKernelClass, 246, 1))
				lvl = *p ? *p : 1;
		loadDungeonLevel(lvl, ctx.objects, ctx.res, ctx.gfx);
		result = 0;
		return true;
	}
	return false;
}

} // namespace THIRDEYE::runtime::eye
