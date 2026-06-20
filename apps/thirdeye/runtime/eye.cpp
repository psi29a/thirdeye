#include "internal.hpp"

#include "../resources/res.hpp"
#include "../savegame/items_tmp.hpp"
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

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
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

		// THIRDEYE_CONTINUE: load the save once; reused for position + PC patching.
		bool wantContinue = std::getenv("THIRDEYE_CONTINUE") != nullptr;
		THIRDEYE::savegame::ItemsTmp items;
		if (wantContinue)
			items = THIRDEYE::savegame::loadItemsTmp(
			    ctx.res.resourcePath().parent_path() / "SAVEGAME" / "ITEMS.TMP");

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
				int patched = 0;
				for (const auto &c : items.characters) {
					if (c.classNumber != 1369) continue;
					int idx = c.objectIndex;
					if (idx <= 0) continue;
					if (ctx.objects.objectLookup(idx) != idx) continue;

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
				     << " PCs patched from ITEMS.TMP (with equip[])]" << std::endl;
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
				// Priority: THIRDEYE_PARTY/GOTO env vars > ITEMS.TMP > default (lvl 1 (15,15)).
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
				if (const char *g = std::getenv("THIRDEYE_GOTO")) { // debug override
					int n = std::atoi(g);
					if (n >= 1 && n <= 14) { startLvl = static_cast<uint8_t>(n); seededFromSave = false; }
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
