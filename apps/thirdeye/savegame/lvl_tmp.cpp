#include "lvl_tmp.hpp"

#include "../resources/res.hpp"
#include "../vm/objects.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace THIRDEYE::savegame {

int loadLevelObjects(int level, VM::ObjectSystem &objects,
                     RESOURCES::Resource &res) {
	constexpr uint16_t kDungeonClass = 1381, kEntities = 1370, kFeatures = 1994,
	                   kNPC = 1622;
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
	// The records are variable-length and the size table is fiddly (guessing it
	// drifts and mis-sizes, e.g. 26-byte doors read as 8). Instead scan every offset
	// for a valid record signature -- id@+1 in 1000-4999, class@+3 a real object
	// class (1300-2450), cell@+11/+12 in 0-31 -- and dedupe by id. The combined
	// filters make a false match astronomically unlikely (~1.6e-5/offset), so this
	// robustly finds every position-bearing object regardless of its record size.
	std::vector<bool> seen(5000, false);
	int placed = 0;
	for (size_t o = 0; o + 15 <= d.size(); ++o) {
		int id = d[o + 1] | (d[o + 2] << 8);
		int cls = d[o + 3] | (d[o + 4] << 8);
		int x = d[o + 11], y = d[o + 12], df = d[o + 14];
		if (id < 1000 || id > 4999 || seen[id] || cls < 1300 || cls > 2450 ||
		    x > 31 || y > 31)
			continue;
		int hp = d[o + 7] | (d[o + 8] << 8); // monster HP (constant per creature)
		bool created = false;
		try {
			objects.createProgram(id, static_cast<uint16_t>(cls));
			created = true;
			// setS writes a static via the given *defining* class; it no-ops if that
			// class isn't in the object's chain (so the feature- vs NPC-specific writes
			// below each apply only to the right object kind).
			auto setS = [&](uint16_t klass, uint32_t off, int v, int n) {
				try {
					if (uint8_t *p = objects.classStaticPtr(id, klass, off, n))
						for (int i = 0; i < n; ++i) p[i] = (v >> (8 * i)) & 0xFF;
				} catch (const std::exception &) {}
			};
			setS(kEntities, 0, x + y * 32, 2); // place  (entities = all objects)
			setS(kEntities, 2, x, 1);          // x
			setS(kEntities, 3, y, 1);          // y
			setS(kEntities, 4, level, 1);      // lvl
			// W:next@6 = -1 (chain terminator). createProgram zero-initializes statics,
			// but features.draw walks the cell chain at its tail (LXW W:next; if != -1
			// SEND "draw" recursively) -- an uninitialized 0 makes every feature draw
			// recursively SEND "draw" to object 0, painting whatever it returns into
			// the viewcell ("stairs in the wall" / green-line artifacts when strafing).
			// Monsters re-set W:next below to prepend to the cell's link chain.
			// Also init W:prev@8 = -1: entities.remove walks W:prev/W:next on death;
			// an uninitialized W:prev=0 writes "prev.W:next = my W:next" into obj 0,
			// silently corrupting its static block.
			setS(kEntities, 6, -1, 2);
			setS(kEntities, 8, -1, 2);
			// decflags — the record byte +14 encodes different things per class:
			//   • wall-side flag 1/2/4/8 for wall-mounted features (doors, levers)
			//     — goes into decflags bits 0-3, features.get_state returns it
			//     and the draw code uses it as orientation
			//   • 0x0F for floor features (stairs, grave, trees, statues, transitions)
			//     — a "no wall side / on the floor" sentinel. Setting decflags = 0x0F
			//     makes features.get_state return 15, and class render CASEs only
			//     handle 0..3, so the object falls through CASE_DEFAULT and never
			//     draws. That was the QSP LVL03 (22,22) "invisible trees block the
			//     path" bug: 89 movable trees on level 3, all with +14=0x0F, none
			//     rendered but all blocked impedance.
			// For floor features the render state clearly isn't the raw +14; treat
			// 0x0F as "unset" (decflags = 0) so state = 0 selects the default
			// render variant. Wall-side values (1..8) still land in decflags as
			// before.
			// ponytail: keep this in the loader; if it turns out a specific floor
			// feature *does* want a non-zero saved state, revisit here.
			setS(kFeatures, 0, df == 0x0F ? 0 : df, 2);
			// Monster? = the record's class has NPC (1622) in its parent chain. (The
			// old test -- classStaticPtr(id, kNPC) != nullptr -- mis-fired: that returns
			// a pointer regardless of whether NPC is actually an ancestor, so stairs/
			// doors were flagged as monsters too.)
			auto isNpcClass = [&](int c) {
				for (int cur = c, guard = 0; cur > 0 && guard < 16; ++guard) {
					if (cur == static_cast<int>(kNPC))
						return true;
					const VM::SopClass *sc = objects.classByNumber(
					    static_cast<uint16_t>(cur));
					if (sc == nullptr)
						break;
					cur = sc->header.parent;
				}
				return false;
			};
			bool isMonster = isNpcClass(cls);
			if (isMonster) {
				setS(kNPC, 3, hp, 2);     // hitpts
				setS(kNPC, 5, -1, 2);     // carried (head of carried-item chain).
				// NPC.die loops on W:carried != -1 walking the chain; an
				// uninitialized 0 walks into obj 0 and stalls death.
				setS(kNPC, 7, 0, 1);      // fdir (creature facing; default 0)
				setS(kNPC, 9, 0, 1);      // stage (idle frame)
				// mission@2 default 0: the monster renders + faces the party (its frame
				// follows party facing via table482 -- visible "animation" as you move
				// around it). Idle-bob ("bounce", M:86) only applies to non-bit-64
				// creatures (the wraith skips it); action-frame animation is AI-driven
				// (mission=1 -> watch-for-party -> my turn), wired with the attack flow.
				// entities: B:region@5 = sub-cell quadrant (the +14 value, 0-3); the
				// draw buckets by table546[party_fdir*5 + region]. W:next@6 links the
				// cell's monster chain -- set at link time below (prepend to the head).
				setS(kEntities, 5, df & 3, 1);
				// Seed W:NPCstat (NPC@0) from the creature's own report(1) -- the stat
				// flags the draw reads to choose the sprite size tier (the native
				// monster-loader does this; our create_program doesn't). report(4) gives
				// the sprite sheet at draw time (e.g. sword wraith -> bitmap 197).
				int32_t npcstat = 0;
				try { npcstat = objects.send(id, 18, { 1 }); } // report(1)
				catch (const std::exception &) {}
				setS(kNPC, 0, npcstat, 2);
			}
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
				// it becomes the head. (Doors/features in plane 0 stay single per cell.)
				if (isMonster) {
					int head = cellp[0] | (cellp[1] << 8); // current head (0xFFFF = none)
					setS(kEntities, 6, head == 0xFFFF ? -1 : head, 2);
				}
				cellp[0] = id & 0xFF;
				cellp[1] = (id >> 8) & 0xFF;
			}
			// SEND "restore" (M:2) to monsters. The native loader does this -- it's
			// NPC.restore that registers the watch-for-party notify (event 34) and
			// SENDs "schedule attack" to queue the first AI turn. Without it
			// monsters never tick: fdir stays 0 (always show their back), they
			// never advance toward the party, never swing back.
			if (isMonster) {
				try { objects.send(id, 2, {}); }
				catch (const std::exception &) {}
			}
			seen[id] = true;
			++placed;
		} catch (const std::exception &) {
			// Rollback: a later step may have thrown after createProgram
			// succeeded, which would leave a partially-initialized live object
			// in the table. Destroy it so the next scan pass can't pick it up
			// and the obj id is free for re-use.
			if (created) {
				try { objects.destroyObject(id); }
				catch (const std::exception &) {}
			}
			seen[id] = true; // mark scanned -- skip, keep going.
		}
	}
	std::cout << "  [loadLevelObjects: placed " << placed << " objects from " << fn
	          << "]" << std::endl;
	return placed;
}

} // namespace THIRDEYE::savegame
