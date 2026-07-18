#include "lvl_tmp.hpp"

#include "../resources/res.hpp"
#include "../vm/events.hpp"
#include "../vm/objects.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace THIRDEYE::savegame {

int loadLevelObjects(int level, VM::ObjectSystem &objects,
                     VM::EventSystem &events, RESOURCES::Resource &res) {
	constexpr uint16_t kDungeonClass = 1381, kEntities = 1370, kNPC = 1622;
	int dn = objects.firstObjectOfClass(kDungeonClass);
	if (dn < 0)
		return 0;
	std::string fn = "LVL" + std::string(level < 10 ? "0" : "") +
	                 std::to_string(level) + ".TMP";
	auto path = res.resourcePath().parent_path() / "SAVEGAME" / fn;
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		std::cout << "  [loadLevelObjects: " << path << " not found]" << std::endl;
		return 0;
	}
	std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)),
	                       std::istreambuf_iterator<char>());
	// LVLnn.TMP is RTOBJECT.C save_range's SF_BIN stream (same as ITEMS.TMP):
	// a 0x1A byte, then for every slot 1000..1999 a CDESC {u16 slot, u32 class,
	// u16 size} + `size` bytes of RAW instance statics (dead slot = class
	// 0xFFFFFFFF, size 0). The statics are copied VERBATIM into the instance
	// (the original's restore_range), so per-class state -- features
	// W:decflags@10 (e.g. bit 0x10 = "tree can be cut"), NPC hitpts (-1 =
	// roll on restore), movable-tree B:direction -- all come straight from
	// the file. Layout (entities base): place@0, x@2, y@3, lvl@4, region@5,
	// next@6, prev@8, then subclass blocks. place/next/prev are stored as -1;
	// the relink below rebuilds them, as the original's post-restore pass does.
	if (d.empty() || d[0] != 0x1A) {
		std::cout << "  [loadLevelObjects: " << fn << " has no save_range magic]"
		          << std::endl;
		return 0;
	}
	// init_level's grid wipe: clear all 3 lvlobj planes (dungeon statics
	// @1024, 3 x 2048 B) to -1 before relinking. On the boot flow init_level
	// already did this; on a change_level mid-game nothing else does, and a
	// stale cell entry from the previous level would alias a same-numbered
	// slot that now belongs to a different object on the new level.
	try {
		if (uint8_t *grid = objects.classStaticPtr(dn, kDungeonClass, 1024, 3 * 2048))
			std::memset(grid, 0xFF, 3 * 2048);
	} catch (const std::exception &) {}
	int placed = 0;
	size_t pos = 1;
	while (pos + 8 <= d.size()) {
		int id = d[pos] | (d[pos + 1] << 8);
		uint32_t clsName = static_cast<uint32_t>(d[pos + 2]) |
		                   (d[pos + 3] << 8) | (d[pos + 4] << 16) |
		                   (static_cast<uint32_t>(d[pos + 5]) << 24);
		uint32_t size = d[pos + 6] | (d[pos + 7] << 8);
		const uint8_t *statics = d.data() + pos + 8;
		pos += 8 + size;
		if (pos > d.size())
			break; // truncated record
		// saveRange only emits slots 1000..1999; an id outside that range in
		// a corrupt/hand-edited file would destroyObject() a live party or
		// global object below. Don't trust the on-disk id.
		if (id < 1000 || id > 1999)
			continue;
		// restore_range: whatever lives in the slot is torn down first -- both
		// for dead file slots (destroy_object + continue in the original) and
		// before re-creating a live one. destroy_object in RTOBJECT.C is
		// MSG_DESTROY *plus* cancel_entity_requests -- the cancel is the
		// runtime's job, NOT the SOP handler's. Skipping it leaked every
		// destroyed object's notify requests; with the boot flow loading each
		// level twice, the 768-slot list overflowed already at LVL03, so the
		// tail of the second load's monsters never got their AI notifies
		// (watch-for-party / my-turn) registered -- passive mists.
		try { objects.destroyObject(id); }
		catch (const std::exception &) {}
		events.cancelEntityRequests(id);
		if (clsName == 0xFFFFFFFFu || size < 6)
			continue; // dead slot
		uint16_t cls = static_cast<uint16_t>(clsName);
		int x = statics[2], y = statics[3];
		if (x > 31 || y > 31 || objects.classByNumber(cls) == nullptr)
			continue;
		bool created = false;
		try {
			objects.createInstance(id, cls);
			created = true;
			// restore_range: raw statics into the instance (createInstance
			// zero-fills whatever the record doesn't cover).
			uint32_t want = std::min<uint32_t>(size, objects.instanceStaticSize(cls));
			if (want > 0) {
				if (uint8_t *sp = objects.staticsPtr(id, 0, want))
					std::memcpy(sp, statics, want);
			}
			auto setS = [&](uint16_t klass, uint32_t off, int v, int n) {
				try {
					if (uint8_t *p = objects.classStaticPtr(id, klass, off, n))
						for (int i = 0; i < n; ++i) p[i] = (v >> (8 * i)) & 0xFF;
				} catch (const std::exception &) {}
			};
			// Relink pass. place is stored as -1; next/prev may carry the
			// chain links of the SAVED moment (the QSP monsters at (12,14)
			// ship with next=1773/prev=1772) -- those describe the original
			// engine's insertion order, not ours. We rebuild the cell chains
			// below in record order, so clear both link fields first: a
			// half-stale prev with our own next produced `1772.next = 1772`
			// the first time a monster moved out of a shared cell (the SOP's
			// unlink does prev.next = my.next), and every chain walker (draw,
			// AI) then spun forever -- the strafe-at-the-treeline beachball.
			setS(kEntities, 0, x + y * 32, 2);
			setS(kEntities, 4, level, 1);
			setS(kEntities, 6, -1, 2); // W:next
			setS(kEntities, 8, -1, 2); // W:prev
			// Monster? = the record's class has NPC (1622) in its parent chain.
			bool isMonster = objects.isSubclassOf(cls, kNPC);
			// Link into the cell grid so "draw objects" renders it. lvlobj has 3
			// planes (plane*2048 + y*64 + x*2): features/decorations (doors) go in
			// PLANE 0, MONSTERS go in PLANE 2 -- NPC.draw walks the cell's object
			// chain in plane 2 (the disassembly reads `lvlobj[2<<11 + ...]`).
			uint32_t plane = isMonster ? 4096u : 0u;
			uint8_t *cellp = objects.classStaticPtr(
			                     dn, kDungeonClass,
			                     1024 + plane + (y * 64 + x * 2), 2);
			if (cellp) {
				// APPEND to the tail of the cell's link-chain -- matches
				// dungeon.init_level (M:232, LBL_2738 in the disassembly):
				// walk W:next until -1, insert there. features.draw paints
				// head-first and recurses down W:next, so tail wins the
				// overpaint. Prepending gave chain [button, door] at LVL01
				// (14,10) and LVL02 (26,2) -- the door drew LAST and hid the
				// button. Appending gives file order [door, button] so the
				// button paints on top of the door frame.
				int head = cellp[0] | (cellp[1] << 8);
				if (head == 0xFFFF) {
					cellp[0] = id & 0xFF;
					cellp[1] = (id >> 8) & 0xFF;
				} else {
					int tail = head;
					for (int guard = 0; guard < 1000; ++guard) {
						uint8_t *np = objects.classStaticPtr(
						    tail, kEntities, 6, 2);
						if (!np) break;
						int nxt = static_cast<int16_t>(np[0] | (np[1] << 8));
						if (nxt < 0) break;
						tail = nxt;
					}
					if (uint8_t *tp = objects.classStaticPtr(
					        tail, kEntities, 6, 2)) {
						tp[0] = id & 0xFF;
						tp[1] = (id >> 8) & 0xFF;
					}
					setS(kEntities, 8, tail, 2); // W:prev = tail
				}
			}
			// W:NPCstat comes VERBATIM from the save file (the memcpy above),
			// exactly like the original restore_range -- the 0x0001 the shipped
			// QSP LVLnn.TMP carries is correct, not stale. A previous bring-up
			// hack seeded it from report(1) (the class CAPABILITY flags) to
			// force hits to land; report(1)'s bit 0x40 overlaps NPCstat's
			// "petrified" bit, so every monster died to one blow with the
			// weapons class's petrified-kill message ("The statue crumbles to
			// dust"). The real reason swings missed was the misported rnd()
			// (see rtcode.cpp): rnd(1,20) rolled 0 forever.
			// SEND "restore" (M:2) to EVERY restored object -- restore_range does
			// `RT_execute(index, MSG_RESTORE)` unconditionally (restoring=1 at all
			// level-load call sites in EYE.C). NPC.restore registers the
			// watch-for-party notify + "schedule attack"; teleporters.restore arms
			// the step-on trigger (`notify(THIS, trigger, 32, y<<16|x)`) that fires
			// change_level -- with monsters-only, stairs never triggered.
			try { objects.send(id, 2, {}); }
			catch (const std::exception &) {}
			++placed;
		} catch (const std::exception &) {
			// Rollback: a later step may have thrown after createProgram
			// succeeded, which would leave a partially-initialized live object
			// in the table. Destroy it so the obj id is free for re-use.
			if (created) {
				try { objects.destroyObject(id); }
				catch (const std::exception &) {}
				events.cancelEntityRequests(id);
			}
		}
	}
	// Chain floor items (slots 1..999) into lvlobj plane 1 for the current
	// level. dungeon.init_level (M:232) does this itself in DOS, but our
	// post-hook re-runs loadLevelObjects which clears the grid -- so any item
	// with W:place == -1 and B:lvl == level needs to be re-linked here or a
	// scroll dropped in the dungeon renders nowhere. Append to match features
	// (draw is head-first so tail wins the overpaint; irrelevant for items in
	// separate cells but consistent with the plane-0 pass above).
	int itemsPlaced = 0;
	for (int id = 1; id <= 999; ++id) {
		if (objects.classOf(id) == 0xFFFF) continue;
		uint8_t *sp = nullptr;
		try { sp = objects.classStaticPtr(id, kEntities, 0, 10); }
		catch (const std::exception &) { continue; }
		if (!sp) continue;
		int place = static_cast<int16_t>(sp[0] | (sp[1] << 8));
		int ix = sp[2], iy = sp[3], ilvl = sp[4];
		if (place != -1) continue;                    // not on the floor
		if (ilvl != level) continue;                  // different dungeon
		if (ix > 31 || iy > 31) continue;             // guard bad coords
		// Reset chain links -- init_level clears the whole grid, so W:next
		// and W:prev inherited from a prior chain are stale.
		sp[6] = 0xFF; sp[7] = 0xFF; sp[8] = 0xFF; sp[9] = 0xFF;
		uint8_t *cellp = nullptr;
		try {
			cellp = objects.classStaticPtr(
			    dn, kDungeonClass,
			    1024 + 2048 + (iy * 64 + ix * 2), 2);
		} catch (const std::exception &) {}
		if (!cellp) continue;
		int head = cellp[0] | (cellp[1] << 8);
		if (head == 0xFFFF) {
			cellp[0] = id & 0xFF;
			cellp[1] = (id >> 8) & 0xFF;
		} else {
			int tail = head;
			for (int guard = 0; guard < 1000; ++guard) {
				uint8_t *np = objects.classStaticPtr(
				    tail, kEntities, 6, 2);
				if (!np) break;
				int nxt = static_cast<int16_t>(np[0] | (np[1] << 8));
				if (nxt < 0) break;
				tail = nxt;
			}
			if (uint8_t *tp = objects.classStaticPtr(
			        tail, kEntities, 6, 2)) {
				tp[0] = id & 0xFF;
				tp[1] = (id >> 8) & 0xFF;
			}
			sp[8] = tail & 0xFF; sp[9] = (tail >> 8) & 0xFF;
		}
		++itemsPlaced;
	}
	std::cout << "  [loadLevelObjects: placed " << placed << " objects from " << fn
	          << ", linked " << itemsPlaced << " floor items into plane 1]"
	          << std::endl;
	return placed;
}

bool saveRange(VM::ObjectSystem &objects, const std::filesystem::path &path,
               int first, int last) {
	// Stage to a same-directory temp and rename at the end -- a crash or
	// full disk mid-write must not truncate an existing LVLxx.TMP/BIN
	// (CodeRabbit: transactional save artifacts).
	std::filesystem::path tmpPath = path;
	tmpPath += ".tmpwrite";
	std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.put('\x1a'); // save_range's typetest byte
	std::vector<uint8_t> rec;
	for (int i = first; i <= last; ++i) {
		uint16_t cls = objects.classOf(i);
		uint32_t name = cls == 0xFFFF ? 0xFFFFFFFFu : cls;
		uint32_t size = 0;
		const uint8_t *statics = nullptr;
		if (cls != 0xFFFF) {
			try {
				size = objects.instanceStaticSize(cls);
				if (size > 0)
					statics = objects.staticsPtr(i, 0, size);
			} catch (const std::exception &) {
				size = 0; // corrupt class chain / unreadable statics:
				statics = nullptr; // degrade to an empty record
			}
			if (size > 0 && statics == nullptr)
				size = 0; // unreadable statics: write an empty record
		}
		rec.assign(8 + size, 0);
		rec[0] = i & 0xFF;
		rec[1] = (i >> 8) & 0xFF;
		rec[2] = name & 0xFF;
		rec[3] = (name >> 8) & 0xFF;
		rec[4] = (name >> 16) & 0xFF;
		rec[5] = (name >> 24) & 0xFF;
		rec[6] = size & 0xFF;
		rec[7] = (size >> 8) & 0xFF;
		if (size > 0)
			std::memcpy(rec.data() + 8, statics, size);
		out.write(reinterpret_cast<const char *>(rec.data()),
		          static_cast<std::streamsize>(rec.size()));
	}
	// close() flushes the tail of the buffer and sets failbit on error; the
	// state is only meaningful after it completes (CodeRabbit).
	out.close();
	bool ok = !out.fail();
	std::error_code ec;
	if (ok) {
		std::filesystem::rename(tmpPath, path, ec);
		ok = !ec;
	}
	if (!ok)
		std::filesystem::remove(tmpPath, ec);
	return ok;
}

} // namespace THIRDEYE::savegame
