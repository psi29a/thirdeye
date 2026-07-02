#include "lvl_tmp.hpp"

#include "../resources/res.hpp"
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
                     RESOURCES::Resource &res) {
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
		// restore_range: whatever lives in the slot is torn down first (its
		// MSG_DESTROY handler cancels its notify requests) -- both for dead
		// file slots (destroy_object + continue in the original) and before
		// re-creating a live one. Without this, a level transition leaves the
		// old level's entities alive and event-registered as ghosts.
		try { objects.destroyObject(id); }
		catch (const std::exception &) {}
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
				// Monsters: prepend to the cell's link-chain so up to 4-per-cell all
				// render -- the new monster's W:next@6 points at the previous head, then
				// it becomes the head. Keep the list properly DOUBLY linked: the old
				// head's W:prev must point back at us, or the SOP's unlink
				// (prev.next = my.next / next.prev = my.prev) corrupts the chain the
				// first time a mid-chain monster moves. (Doors/features in plane 0
				// stay single per cell.)
				if (isMonster) {
					int head = cellp[0] | (cellp[1] << 8); // current head (0xFFFF = none)
					setS(kEntities, 6, head == 0xFFFF ? -1 : head, 2);
					if (head != 0xFFFF) {
						try {
							if (uint8_t *hp = objects.classStaticPtr(
							        head, kEntities, 8, 2)) {
								hp[0] = id & 0xFF;
								hp[1] = (id >> 8) & 0xFF;
							}
						} catch (const std::exception &) {}
					}
				}
				cellp[0] = id & 0xFF;
				cellp[1] = (id >> 8) & 0xFF;
			}
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
			}
		}
	}
	std::cout << "  [loadLevelObjects: placed " << placed << " objects from " << fn
	          << "]" << std::endl;
	return placed;
}

bool saveRange(VM::ObjectSystem &objects, const std::filesystem::path &path,
               int first, int last) {
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
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
	return !!out;
}

} // namespace THIRDEYE::savegame
