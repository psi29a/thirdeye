#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../savegame/lvl_tmp.hpp"
#include "../vm/events.hpp"
#include "../vm/objects.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>

namespace THIRDEYE::runtime::event {

// dispatch_event hosts a pile of THIRDEYE_* debug spawn/monitor logic that
// pre-dates a proper debug surface. Lifted here verbatim from engine.cpp so
// the chain stays identical. Each block self-gates on its env var; off by
// default has zero cost beyond the getenv.
namespace {

void runDebugHooks(VM::ObjectSystem &objects, RESOURCES::Resource &res) {
	// (Level-object loading lives in resume_level + change_level. A one-shot
	// here previously raced resume_level's party_lvl patch and loaded
	// LVL01.TMP into whatever maze had been swapped in -- don't reintroduce.)
	(void)res;

	// Creature notes: the 3D view redraws ~4 Hz even when idle (the kernel "timer
	// tick" sends "draw view" gated on B:update@249), so monsters re-render live --
	// the "0 redraws idle" seen earlier was just the runtime trace being off without
	// --debug. The wraith re-faces the party as you move (relative facing); it has
	// no idle bob (NPCstat bit 64 skips "bounce"); action-frame animation is part of
	// the AI ("my turn"), exercised by the attack below.
	// Debug (THIRDEYE_ATTACK): drive the party->monster attack. Enable the kernel's
	// auto-attack (B:auto_button@260) and SEND "auto-attack" (M:208) periodically;
	// it walks the front-row player[] and SENDs each PC "use/attack request" (hand
	// 16=right/20=left, flag 7) -> acquire NPC target -> roll to hit -> roll for
	// damage -> the monster's "take damage" -> "die". Logs the first live monster's
	// HP so we can watch it drop. (Stand-in for the weapon-click/auto-attack UI.)
	if (std::getenv("THIRDEYE_ATTACK")) {
		static int af = 0;
		static int monIdx = -1;
		int dn = objects.firstObjectOfClass(1381);
		int kn = objects.firstObjectOfClass(1382);
		// One-time: spawn a target monster in the cell directly in front of the
		// party (so "acquire NPC target" finds it via lvlobj plane 2) with a known
		// HP, so we can watch combat resolve. Class/HP overridable for testing
		// against a non-bit-64 target (THIRDEYE_MONCLASS=1943 = warrior shade).
		if (af == 0 && dn >= 0 && kn >= 0) {
			auto kb = [&](int off) -> int {
				uint8_t *p = objects.classStaticPtr(kn, 1382, off, 1);
				return p ? *p : 0;
			};
			// Party position lives in the kernel's statics at 243/244/245
			// (party_lvl@246 is read above) -- NOT the import XR offsets 6/8/10.
			int px = kb(243), py = kb(244), pf = kb(245);
			int fx = px, fy = py;
			if (pf == 0) fy = py - 1;
			else if (pf == 1) fx = px + 1;
			else if (pf == 2) fy = py + 1;
			else fx = px - 1;
			int mc = 1904; // sword wraith
			if (const char *e = std::getenv("THIRDEYE_MONCLASS")) mc = std::atoi(e);
			int hp = 30;
			if (const char *e = std::getenv("THIRDEYE_MONHP")) hp = std::atoi(e);
			monIdx = 2600;
			objects.createProgram(monIdx, static_cast<uint16_t>(mc));
			auto setS = [&](uint16_t k, uint32_t o, int v, int n) {
				if (uint8_t *p = objects.classStaticPtr(monIdx, k, o, n))
					for (int i = 0; i < n; ++i) p[i] = (v >> (8 * i)) & 0xFF;
			};
			setS(1370, 0, fx + fy * 32, 2); // entities place
			setS(1370, 2, fx, 1);           // x
			setS(1370, 3, fy, 1);           // y
			setS(1370, 4, 1, 1);            // lvl
			setS(1622, 3, hp, 2);           // NPC hitpts
			setS(1622, 5, -1, 2);           // NPC carried (head of carried-item chain)
			// ponytail: this exists because NPC.die loops on W:carried != -1; an
			// uninitialized 0 walks into obj 0 and stalls the death path.
			int32_t ns = 0;
			try { ns = objects.send(monIdx, 18, {1}); } catch (...) {}
			setS(1622, 0, ns, 2); // NPCstat (from report(1))
			if (uint8_t *p = objects.classStaticPtr(
			        dn, 1381, 1024 + 4096 + (fy * 64 + fx * 2), 2)) {
				p[0] = monIdx & 0xFF; p[1] = (monIdx >> 8) & 0xFF;
			}
			std::cerr << "[atk] spawned mon " << monIdx << " class " << mc << " hp "
			          << hp << " NPCstat 0x" << std::hex << ns << std::dec << " at ("
			          << fx << "," << fy << ") party (" << px << "," << py
			          << ") fdir " << pf << "\n";
		}
		if (++af % 4 == 0) {
			// Each PC's own B:auto_attack@75 must be set, or use/attack request
			// (flag 7) bails before sending "use"(M:41) to the weapon (the char-gen
			// transfer leaves it 0). Then the kernel auto-attack drives the swing.
			for (int pc : objects.objectsOfClass(1369))
				try {
					if (uint8_t *aa = objects.classStaticPtr(pc, 1369, 75, 1))
						*aa = 1; // B:auto_attack -- else use/attack request bails
					// Clear both hands' recovery status (B:h_stat@67, 2 bytes) so the
					// PC swings each tick instead of one-and-done.
					if (uint8_t *hs = objects.classStaticPtr(pc, 1369, 67, 2))
						hs[0] = hs[1] = 0;
				} catch (const std::exception &) {}
			if (kn >= 0) {
				if (uint8_t *ab = objects.classStaticPtr(kn, 1382, 260, 1))
					*ab = 1; // auto_button on
				try { objects.send(kn, 208, {}); } // "auto-attack"
				catch (const std::exception &) {}
			}
			// Log the spawned target's HP so we can watch it drop.
			if (monIdx >= 0) {
				try {
					if (uint8_t *hp = objects.classStaticPtr(monIdx, 1622, 3, 2))
						std::cerr << "[atk] mon " << monIdx << " hp "
						          << static_cast<int16_t>(hp[0] | (hp[1] << 8))
						          << " (live=" << objects.objectLookup(monIdx) << ")\n";
				} catch (const std::exception &) {}
			}
			// PC hand contents: W:inventory[16]=right, [20]=left (PC@81, words).
			for (int pc : objects.objectsOfClass(1369))
				try {
					uint8_t *r = objects.classStaticPtr(pc, 1369, 81 + 16 * 2, 2);
					uint8_t *l = objects.classStaticPtr(pc, 1369, 81 + 20 * 2, 2);
					if (r && l)
						std::cerr << "[hand] pc " << pc << " R="
						          << static_cast<int16_t>(r[0] | (r[1] << 8)) << " L="
						          << static_cast<int16_t>(l[0] | (l[1] << 8)) << "\n";
				} catch (const std::exception &) {}
		}
	}
	// Debug (THIRDEYE_TESTOBJ): once the game loop is running (so this is AFTER
	// init level has cleared lvlobj), create a Mausoleum door, set its position
	// (entities place@0/x@2/y@3/lvl@4) and drop its index into lvlobj plane 0 at
	// its cell -- validates create -> position -> lvlobj -> "draw objects".
	// lvlobj byte offset = plane*2048 + y*64 + x*2 (the AIS<<11/<<6/<<1 strides).
	if (std::getenv("THIRDEYE_TESTOBJ")) {
		static int frame = 0;
		int dn = objects.firstObjectOfClass(1381);
		constexpr uint16_t kEntities = 1370;
		constexpr int doorIdx = 60, x = 15, y = 13; // 2 N of the (15,15) start
		if (dn >= 0) {
			if (frame == 0) {
				objects.createProgram(doorIdx, 2211); // mausoleum skull door
				auto setE = [&](uint32_t o, int v, int sz) {
					if (uint8_t *p =
					        objects.classStaticPtr(doorIdx, kEntities, o, sz))
						for (int i = 0; i < sz; ++i) p[i] = (v >> (8 * i)) & 0xFF;
				};
				setE(0, x + y * 32, 2); setE(2, x, 1); setE(3, y, 1); setE(4, 1, 1);
				// features W:decflags (class 1994 @0) -- the door's orientation/state;
				// 0x0001 renders a clean skull door facing the party.
				int df = 0x0001;
				if (const char *e = std::getenv("THIRDEYE_DECFLAGS"))
					df = static_cast<int>(std::strtol(e, nullptr, 16));
				if (uint8_t *p = objects.classStaticPtr(doorIdx, 1994, 0, 2)) {
					p[0] = df & 0xFF; p[1] = (df >> 8) & 0xFF;
				}
				std::cout << "  [testobj: skull door " << doorIdx << " @(" << x
				          << "," << y << ")]" << std::endl;
			}
			// Re-assert the lvlobj entry every frame (in case it's re-cleared).
			if (uint8_t *p =
			        objects.classStaticPtr(dn, 1381, 1024 + (y * 64 + x * 2), 2)) {
				p[0] = doorIdx & 0xFF; p[1] = (doorIdx >> 8) & 0xFF;
			}
			if (frame == 120) {
				std::cout << "  [view cells:";
				for (int i = 0; i < 18; ++i) {
					uint8_t *vx = objects.classStaticPtr(dn, 1381, 7172 + i, 1);
					uint8_t *vy = objects.classStaticPtr(dn, 1381, 7190 + i, 1);
					uint8_t *vv = objects.classStaticPtr(dn, 1381, 7208 + i, 1);
					if (vx && vy && vv)
						std::cout << " (" << int(*vx) << "," << int(*vy) << ")"
						          << (*vv ? "v" : "-");
				}
				std::cout << "]" << std::endl;
			}
			++frame;
		}
	}
	// Debug (THIRDEYE_TESTITEM): drop a short sword on the floor in front of the
	// party, to verify floor-item rendering + pickup. Item = short sword (1325):
	// weapons->arms->items->entities. Set entities position + items.itmflags, and
	// link into lvlobj like any level object so "draw objects" -> items.draw shows
	// it on the ground. Placed once, after init level clears lvlobj.
	if (std::getenv("THIRDEYE_TESTITEM")) {
		static bool done = false;
		int dn = objects.firstObjectOfClass(1381);
		if (!done && dn >= 0) {
			done = true;
			// Use an existing char-gen item (its full state is set -- it renders in
			// the inventory) and move it onto the floor in front of the party.
			int itemIdx = 999, ix = 15, iy = 14; // 1 N of (15,15) start
			if (const char *e = std::getenv("THIRDEYE_ITEMOBJ"))
				itemIdx = std::atoi(e);
			auto setS = [&](uint16_t k, uint32_t o, int v, int n) {
				try {
					if (uint8_t *p = objects.classStaticPtr(itemIdx, k, o, n))
						for (int i = 0; i < n; ++i) p[i] = (v >> (8 * i)) & 0xFF;
				} catch (const std::exception &) {}
			};
			setS(1370, 0, ix + iy * 32, 2); // entities place = floor cell
			setS(1370, 2, ix, 1);           // x
			setS(1370, 3, iy, 1);           // y
			setS(1370, 4, 1, 1);            // lvl
			if (uint8_t *p =
			        objects.classStaticPtr(dn, 1381, 1024 + (iy * 64 + ix * 2), 2)) {
				p[0] = itemIdx & 0xFF; p[1] = (itemIdx >> 8) & 0xFF;
			}
			std::cout << "  [testitem: item obj " << itemIdx << " -> floor @(" << ix
			          << "," << iy << ")]" << std::endl;
		}
	}
	// Debug: log the party position when it changes (verifies movement).
	if (std::getenv("THIRDEYE_AUTOWALK") || std::getenv("THIRDEYE_CLICK") ||
	    std::getenv("THIRDEYE_AUTOKEY")) {
		int kn = objects.firstObjectOfClass(1382);
		if (kn >= 0) {
			uint8_t *xp = objects.classStaticPtr(kn, 1382, 243, 1);
			uint8_t *yp = objects.classStaticPtr(kn, 1382, 244, 1);
			uint8_t *fp = objects.classStaticPtr(kn, 1382, 245, 1);
			static int lx = -1, ly = -1, lf = -1;
			if (xp && yp && fp && (*xp != lx || *yp != ly || *fp != lf)) {
				lx = *xp; ly = *yp; lf = *fp;
				std::cout << "  [party @ (" << lx << "," << ly << ") facing "
				          << lf << "]" << std::endl;
			}
		}
	}
}

} // namespace

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// --- event system (EVENT.C) -- real, and intentionally quiet: the kernel's
	// main loop calls these in a tight spin, so logging each would bury the
	// trace. dispatchEvent itself logs the messages it delivers when verbose.
	if (fn == "notify" && args.size() >= 4) {
		ctx.events.notify(args[0], static_cast<uint32_t>(args[1]), args[2], args[3]);
		if (std::getenv("THIRDEYE_AUTOWALK"))
			std::cout << "  [notify obj " << args[0] << " msg " << args[1]
			          << " event " << args[2] << " param 0x" << std::hex << args[3]
			          << std::dec << "]" << std::endl;
		result = 0;
		return true;
	}
	if (fn == "cancel" && args.size() >= 4) {
		ctx.events.cancel(args[0], static_cast<uint32_t>(args[1]), args[2], args[3]);
		result = 0;
		return true;
	}
	if (fn == "post_event" && args.size() >= 3) {
		ctx.events.postEvent(args[0], args[1], args[2]);
		result = 0;
		return true;
	}
	if (fn == "send_event" && args.size() >= 3) {
		ctx.events.sendEvent(args[0], args[1], args[2]);
		result = 0;
		return true;
	}
	if (fn == "dispatch_event") {
		// THIRDEYE_TIMING: log the first dispatch_event arrival (= the bytecode
		// finished its synchronous boot and the kernel main loop has begun).
		// Anything between launch and this point is pure interpreter work --
		// the boot path's cost goes here.
		static bool firstDispatch = true;
		if (firstDispatch) {
			firstDispatch = false;
			if (std::getenv("THIRDEYE_TIMING")) {
				auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				              std::chrono::steady_clock::now() - gBootStart).count();
				std::cout << "[timing] first dispatch_event (boot bytecode done): "
				          << ms << " ms\n";
			}
		}
		if (ctx.gfx) pumpHost(*ctx.gfx, ctx.events); // host seam: present + input + yield
		ctx.events.dispatchEvent();
		runDebugHooks(ctx.objects, ctx.res);
		result = 0;
		return true;
	}
	if (fn == "drain_event_queue") {
		ctx.events.drainEventQueue();
		result = 0;
		return true;
	}
	if (fn == "peek_event") {
		if (ctx.gfx) pumpHost(*ctx.gfx, ctx.events);
		result = ctx.events.peekEvent() ? 1 : 0;
		return true;
	}
	if (fn == "flush_event_queue" && args.size() >= 3) {
		ctx.events.flushEventQueue(args[0], args[1], args[2]);
		result = 0;
		return true;
	}
	if (fn == "flush_input_events") {
		ctx.events.flushInputEvents();
		result = 0;
		return true;
	}
	// mouse_XY(): return the current cursor position packed as the same
	// (y << 16) | (x & 0xFFFF) the SOP gets from a SYS_MOUSEMOVE event's
	// parameter. The save-picker reads this to pick which slot row the user
	// clicked; without it, every click resolved to (0, 0) and the SOP either
	// auto-confirmed the wrong slot or ignored the click.
	if (fn == "mouse_XY") {
		int32_t x = ctx.events.pointX();
		int32_t y = ctx.events.pointY();
		result = static_cast<int32_t>(
		    (static_cast<uint32_t>(y) << 16) |
		    (static_cast<uint32_t>(x) & 0xFFFFu));
		return true;
	}
	return false;
}

} // namespace THIRDEYE::runtime::event
