#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../vm/events.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>    // sscanf (THIRDEYE_PALBASE override)
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace THIRDEYE::runtime::graphics {

namespace {

// first_color[region]: where each palette region starts in the 256-colour DAC
// (GRAPHICS.C). PAL_FIXED=0x00, PAL_WALLS=0xB0, PAL_M1=0xC0, PAL_M2=0xE0,
// PAL_OUT=0xB0.
constexpr uint16_t kFirstColor[5] = {0x00, 0xB0, 0xC0, 0xE0, 0xB0};

// Dungeon Hack carves the DAC up differently: one big fixed palette plus a
// 16-colour floor palette and a 16-colour wall palette at the very top. Its
// kernel loads them as set_palette(1, wallpal[lvl]) / set_palette(2,
// floorpal[lvl]) (HACK.RES/kernel "enter level"), and the art confirms the
// bases -- wallset shapes index 0xF6..0xFD, floor shapes 0xC4..0xEE:
//     region 0 fixed : 0x00, 225 colours -> 0x00..0xE0
//     region 1 walls : 0xF0, 16 colours  -> 0xF0..0xFF
//     region 2 floor : 0xE0, 16 colours  -> 0xE0..0xEF
// With EOB3's bases the wall art landed on never-loaded (black) DAC entries,
// which is why the dungeon view rendered solid black. See
// docs/dungeon_hack_maze.md.
constexpr uint16_t kFirstColorDH[5] = {0x00, 0xF0, 0xE0, 0xE0, 0xB0};

// All clipping is now per-pane via events.windowRect(), matching the original
// GIL2VFX_draw_bitmap. No hardcoded view dimensions live here -- the dungeon
// view, the HUD, the save-picker windows etc. all carry their natural rects
// in the events table from assign_subwindow / set_x* edits.

} // namespace

bool tryHandle(Context &ctx, const std::string &fn,
               const std::vector<VM::Value> &args, VM::Value &result) {
	// --- windowing (GRAPHICS.C assign_subwindow/release_window) ---
	// Backed by the event layer's window table so region (click/hover) events
	// can hit-test against these rectangles. assign_subwindow(owner, parent, x1,
	// y1, x2, y2) returns the handle the SOP code passes to notify(); we ignore
	// `parent` since subwindow coords are absolute (matches GIL2VFX).
	if (fn == "assign_subwindow" && args.size() >= 6) {
		int32_t h = ctx.events.assignWindow(args[0], args[2], args[3], args[4], args[5]);
		if (std::getenv("THIRDEYE_REGIONS"))
			std::cout << "  [subwindow handle " << h << " = (" << args[2] << ","
			          << args[3] << ")-(" << args[4] << "," << args[5] << ")]"
			          << std::endl;
		result = h;
		return true;
	}
	// assign_window(owner, x0, y0, x1, y1): unlike assign_subwindow (a rect in
	// absolute screen coords), this registers an OFFSCREEN PAGE whose contents
	// the SOP addresses in page-local coords and later composites with
	// copy_window. Only Dungeon Hack uses it; EOB3 has no copy_window, so
	// nothing there ever takes the offscreen path.
	if (fn == "assign_window" && args.size() >= 5) {
		result = ctx.events.assignWindow(args[0], args[1], args[2], args[3],
		                                 args[4], /*offscreen=*/true);
		if (std::getenv("THIRDEYE_REGIONS"))
			std::cout << "  [page handle " << result << " = " << args[1] << ","
			          << args[2] << " .. " << args[3] << "," << args[4] << "]"
			          << std::endl;
		return true;
	}
	if (fn == "release_window" && args.size() >= 1) {
		ctx.events.releaseWindow(args[0]);
		result = 0;
		return true;
	}
	// set_x1/x2/y1/y2(wnd, val): mutate one edge of the subwindow. Matches
	// GIL2VFX_set_x{1,2}/set_y{1,2}: assigns panes[wnd].{x0,x1,y0,y1}, which
	// the original GIL2VFX_draw_bitmap then reads as the natural clip rect.
	// We mirror that: setWindowEdge updates events.windowRect(wnd), which
	// draw_bitmap below reads to clip the blit. Used by:
	//   * dungeon view's per-cell narrowing (set_x2(view, 127) etc. for each
	//     cell's horizontal strip, then set_x2(view, 175) to restore wide).
	//   * save-picker's slot-number column (set_x2(99, 13) narrows window 99
	//     for the digits, set_x2(99, 175) widens it back for the names).
	// For text windows bound to the changed subwindow, the new edges propagate
	// to printText so the next render uses them.
	auto refreshBoundText = [&](int32_t handle) {
		if (!ctx.gfx) return;
		int32_t x0, y0, x1, y1;
		if (ctx.events.windowRect(handle, x0, y0, x1, y1))
			ctx.gfx->updateTextWindowsFor(handle, x0, y0, x1, y1);
	};
	if (fn == "set_x1" && args.size() >= 2) {
		ctx.events.setWindowEdge(args[0], 'l', args[1]);
		refreshBoundText(args[0]);
		result = 0;
		return true;
	}
	if (fn == "set_x2" && args.size() >= 2) {
		ctx.events.setWindowEdge(args[0], 'r', args[1]);
		refreshBoundText(args[0]);
		result = 0;
		return true;
	}
	if (fn == "set_y1" && args.size() >= 2) {
		ctx.events.setWindowEdge(args[0], 't', args[1]);
		refreshBoundText(args[0]);
		result = 0;
		return true;
	}
	if (fn == "set_y2" && args.size() >= 2) {
		ctx.events.setWindowEdge(args[0], 'b', args[1]);
		refreshBoundText(args[0]);
		result = 0;
		return true;
	}
	// get_x1/get_y1/get_x2/get_y2: a window's rectangle edges.
	if ((fn == "get_x1" || fn == "get_y1" || fn == "get_x2" || fn == "get_y2") &&
	    args.size() >= 1) {
		int32_t x1, y1, x2, y2;
		if (!ctx.events.windowRect(args[0], x1, y1, x2, y2)) {
			result = 0;
			return true;
		}
		if (fn == "get_x1") { result = x1; return true; }
		if (fn == "get_y1") { result = y1; return true; }
		if (fn == "get_x2") { result = x2; return true; }
		result = y2; // get_y2
		return true;
	}

	// Cursor / cache / init no-ops: SDL owns the real OS cursor (set_mouse_
	// pointer swaps its bitmap; show/hide/lock and the standby "hourglass"
	// have no equivalent), our resource layer keeps assets resident (no LRU
	// cache to flush/thrash), graphics+interface init happens natively in
	// bootObject, and presents are vsynced (no retrace wait).
	if (fn == "show_mouse" || fn == "hide_mouse" || fn == "lock_mouse" ||
	    fn == "unlock_mouse" || fn == "standby_cursor" ||
	    fn == "resume_cursor" || fn == "set_wait_pointer" ||
	    fn == "wait_vertical_retrace" || fn == "init_graphics" ||
	    fn == "shutdown_graphics" || fn == "init_interface" ||
	    fn == "shutdown_interface" || fn == "flush_cache" ||
	    fn == "thrash_cache") {
		result = 0;
		return true;
	}
	// mouse_in_window(wnd): is the pointer inside the window's rect right now
	// (INTRFACE.C -- reads point_X/point_Y against the pane edges). Pure event
	// state, so it works headless too.
	if (fn == "mouse_in_window" && args.size() >= 1) {
		int32_t x0, y0, x1, y1;
		result = 0;
		if (ctx.events.windowRect(args[0], x0, y0, x1, y1)) {
			int32_t px = ctx.events.pointX(), py = ctx.events.pointY();
			result = (px >= x0 && px <= x1 && py >= y0 && py <= y1) ? 1 : 0;
		}
		return true;
	}
	// text_refresh_window(wndnum, wnd): the original just records which
	// graphics window to refresh after prints to text window wndnum; our
	// present path refreshes the whole screen, so recording is unnecessary.
	if (fn == "text_refresh_window") {
		result = 0;
		return true;
	}

	// The rest of this category needs the graphics target. In headless mode
	// (no display) we fall through so the engine logs a stub trace.
	if (!ctx.gfx)
		return false;

	auto fetch = [&](VM::Value n) -> std::vector<uint8_t> & {
		return ctx.res.getAsset(static_cast<uint16_t>(n));
	};
	// set_palette(region, resource): load a palette resource into the region.
	if (fn == "set_palette" && args.size() >= 2) {
		uint16_t region = static_cast<uint16_t>(args[0]);
		const uint16_t *bases = gDungeonHack ? kFirstColorDH : kFirstColor;
		uint16_t first = region < 5 ? bases[region] : 0;
		// THIRDEYE_PALBASE=b0,b1,b2,b3 overrides the region bases (how the DH
		// map above was worked out; keep it for the next game we bring up).
		if (const char *pb = std::getenv("THIRDEYE_PALBASE")) {
			int b[5] = {bases[0], bases[1], bases[2], bases[3], bases[4]};
			std::sscanf(pb, "%d,%d,%d,%d", &b[0], &b[1], &b[2], &b[3]);
			if (region < 5) first = static_cast<uint16_t>(b[region]);
		}
		try {
			ctx.gfx->setPaletteRange(fetch(args[1]), first);
			rt() << "  [palette region " << region << " -> base "
			     << first << ", "
			     << GRAPHICS::Palette(fetch(args[1])).getNumOfColours()
			     << " colours]" << std::endl;
		} catch (const std::exception &e) {
			rt() << "  [palette failed: " << e.what() << "]" << std::endl;
		}
		result = 0;
		return true;
	}
	// draw_bitmap(page, table, number, x, y, scale, flip, fade_table, fade_level)
	if (fn == "draw_bitmap" && args.size() >= 5) {
		uint16_t page = static_cast<uint16_t>(args[0]);
		uint16_t table = static_cast<uint16_t>(args[1]);
		uint16_t number = static_cast<uint16_t>(args[2]);
		int x = static_cast<int>(args[3]), y = static_cast<int>(args[4]);
		// Match the original GIL2VFX_draw_bitmap: every draw clips to its pane's
		// natural rect (panes[wnd].x0..x1, y0..y1). Our events table is the SOP's
		// pane table -- assign_subwindow registered the rect when the SOP created
		// the page, and set_x1/x2/y1/y2 mutate the rect (so per-cell narrowing on
		// the dungeon view page is already reflected here). PAGE1/PAGE2 (handles
		// 0/1) are seeded as full 320x200 in EventSystem::EventSystem, so HUD
		// draws to page 1 see no narrowing and reach the portrait panels.
		int32_t px0, py0, px1, py1;
		bool clipped = ctx.events.windowRect(static_cast<int32_t>(page),
		                                     px0, py0, px1, py1);
		// Offscreen page (DH's assign_window): redirect the draw into the page's
		// own surface, addressed in page-local coords, and skip the screen-rect
		// clip -- the page IS the clip. endPage() at the bottom of this branch
		// puts the visible screen back. Without this every DH panel draws at
		// screen (0,0) and stomps the previous one (see docs/dungeon_hack_maze.md).
		bool onPage = false;
		if (clipped && ctx.events.windowIsOffscreen(static_cast<int32_t>(page))) {
			onPage = ctx.gfx->beginPage(static_cast<int32_t>(page),
			                            px1 - px0 + 1, py1 - py0 + 1);
			// Only drop the pane clip when the redirect actually took. If
			// beginPage failed (a page is already the target, the rect is
			// degenerate because set_x1/set_x2 put px1 below px0, or the
			// surface allocation failed) the draw still goes to the visible
			// screen -- and clearing `clipped` there would paint a DH panel
			// unclipped over the whole frame.
			if (onPage) clipped = false;
		}
		if (clipped)
			ctx.gfx->setClip(px0, py0, px1 - px0 + 1, py1 - py0 + 1);
		// arg[5] = depth-tier scale (256 = native, 128 = half, 64 = quarter);
		// arg[6] = flip/mirror (GIL2VFX: 1=X, 2=Y, 3=both).
		// Matches native EOB3's `draw_bitmap(page, table, number, x, y, scale,
		// flip, fade_table, fade_level)` -- ignoring the scale flattens all
		// monster sprites to native size, so mists at 1/2/3 cells forward all
		// render the same size instead of shrinking with distance.
		int scale = args.size() > 5 ? static_cast<int>(args[5]) : 0;
		int mirror = args.size() > 6 ? static_cast<int>(args[6]) : 0;
		// The compass facing indicator (resource 187) is only drawn on a turn or
		// when the HUD is being restored after a cutscene; mark it so the next
		// compass refresh re-snapshots. The initial-boot flow draws it to
		// page 102 + refreshes window 104; `close outtake dialog` (the Florn
		// cutscene finalizer) redraws it to page 108 and refreshes window 1,
		// so we can't gate on either page or refresh alone. Marking dirty on
		// ANY page-187 draw and re-snapshotting inline is enough — the compass
		// is a small bitmap and the snapshot is cheap. Without this the
		// compass snapshot captured mid-outtake pixels (light-brown fill from
		// `prepare outtake box`'s `wipe_window(96, 20)`) and every subsequent
		// present's `restoreCompass()` blitted the brown box back onto the
		// bottom-left HUD region.
		if (table == 187) {
			// The kernel's timer keeps redrawing the compass even while a
			// dialog/menu screen is up. The original drew it to page 104,
			// which the dialog screens never composite -- our page
			// flattening would paint it straight onto the visible frame
			// (compass ghosting over the mausoleum-entry decision box).
			// Skip the draw entirely outside the adventure screen.
			if (uiScreenActive(ctx.objects)) {
				// Drop the pane clip set above -- the normal path clears it
				// after the draw, and leaving it armed here clipped every
				// later dialog/menu draw to the compass pane (CodeRabbit).
				if (clipped)
					ctx.gfx->clearClip();
				if (onPage)
					ctx.gfx->endPage();
				result = 0;
				return true;
			}
			gCompassDirty = true;
		}
		try {
			auto t0 = gPerf ? std::chrono::steady_clock::now()
			                : std::chrono::steady_clock::time_point{};
			// table = the AESOP resource number; stable for the lifetime of
			// the loaded asset, so it's a safe shape-cache identity.
			// Backdrop bitmap (res 190) is the whole-screen HUD frame — 320x200,
			// designed to sit on top of a black clear (VFX_shape_draw treats
			// palette 0 as transparent, and the DOS init clears to 0 before any
			// draw). `close outtake dialog` (M:312) draws it to page 1 after the
			// outtake box painted a light-brown fill into window 96 (y=119..199).
			// Without a black underlay the transparent pixels of the Backdrop
			// leave that fill visible, which shows up as a brown text-log region
			// after the Florn Falconhand cutscene ends. Clear to color 0 first
			// so the Backdrop's transparent regions read as their DOS-native
			// black. Only triggers on Backdrop (a fixed one-of resource) so it
			// costs nothing on every other draw. (fillRect also writes the
			// text-free backdrop snapshot, so later text-window restores of
			// the log area come back black, not the outtake's brown fill.)
			if (table == 190) {
				ctx.gfx->fillRect(x, y, x + 319, y + 199, 0);
				// Entering the game from the title menu ("Continue the
				// Quest" / "Summon the Heroes") happens INSIDE the same
				// `start` instance, so the engine's per-launch
				// setTextRestoreBackground(mode != MODE_INTR) never
				// re-fires and text boxes stay in the menu's flat-fill
				// mode. Flat-fill samples a pixel just inside the box --
				// after a coloured party comment scrolls, that sample can
				// hit a green glyph and the whole message bar fills green
				// (and stays green, since the next sample hits the fill).
				// The HUD Backdrop draw is the definitive "now in-game"
				// signal: switch to backdrop-restore mode here.
				ctx.gfx->setTextRestoreBackground(true);
			}
			// Dungeon Hack's equivalent full-screen HUD backdrop is resource
			// 59 ("Backdrop", drawn as draw_bitmap(1, 59, 0, 0)). DH boots
			// with mode INTR so the engine left text boxes in flat-fill mode,
			// and flat-fill samples a pixel just inside the box: one stray
			// light pixel in the message bar turned the whole bar white and
			// then kept it white (the next wipe samples its own fill). Same
			// "now in-game" switch as EOB3's 190 above.
			if (gDungeonHack && table == 59) {
				ctx.gfx->setTextRestoreBackground(true);
			}
			ctx.gfx->drawImage(fetch(table), number, x, y, true, mirror,
			                   static_cast<uint32_t>(table), scale);
			// Right after the HUD Backdrop lands, the compass rect holds
			// PRISTINE frame art (the compass 187 hasn't been stamped yet).
			// Capture it as the underlay that panels drawn over the compass
			// area (spell book) restore first, so their transparent pixels
			// reveal clean leather instead of stale compass pixels.
			// ONLY capture when this draw actually repainted the region: a
			// 190 draw clipped to a window elsewhere (the boot flow draws it
			// to several pages) leaves the already-stamped compass on screen,
			// and a blind capture would poison the underlay with compass
			// pixels -- exactly the gold-shrapnel-in-the-arrow-glyphs bug.
			const SDL_Rect &ur = GRAPHICS::Graphics::menuUnderlayRect();
			if (table == 190 &&
			    (!clipped ||
			     (px0 <= ur.x && py0 <= ur.y &&
			      px1 >= ur.x + ur.w - 1 && py1 >= ur.y + ur.h - 1)))
				ctx.gfx->snapshotCompassUnderlay();
			// The compass snapshot rect (0, 120, 116, 49 in
			// Graphics::kCompassRect) sits in the bottom-left HUD region. If
			// the compass 187 was drawn just now, the snapshot needs to
			// capture the *fresh* pixels — not the previous frame's leftover.
			// The old logic only snapshotted on `refresh_window(104, ...)`,
			// which fires at boot but NOT on `close outtake dialog`'s HUD
			// redraw (which uses page 108 + `refresh_window(1, 0)`). Left
			// stale, the snapshot could latch mid-outtake pixels (the
			// light-brown `wipe_window(96, 20)` fill) and every present's
			// `restoreCompass()` would blit that brown box back onto the
			// screen — exactly the Florn Falconhand aftermath the user saw.
			// Snapshot inline right after the compass draw so both flows
			// (boot's page 102, close-outtake's page 108) capture a valid
			// compass.
			if (table == 187 && gCompassDirty) {
				ctx.gfx->snapshotCompass();
				gCompassDirty = false;
			}
			if (gPerf) {
				gDrawNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
				                  std::chrono::steady_clock::now() - t0).count();
				++gDrawCount;
			}
			rt() << "  [drew p" << page << " " << table << ":" << number
			     << " @ " << x << "," << y << (mirror ? " M" : "") << "]"
			     << std::endl;
		} catch (const std::exception &e) {
			rt() << "  [draw failed: " << e.what() << "]" << std::endl;
		}
		if (clipped)
			ctx.gfx->clearClip();
		if (onPage)
			ctx.gfx->endPage();
		result = 0;
		return true;
	}
	// fill_rectangle(page, x1, y1, x2, y2, color): clear an inclusive rect to a
	// palette colour. The SOP screens call this to erase a panel before redrawing
	// (e.g. the character-stats screen clears the equipment area first). Stubbed,
	// it left the old screen showing through the new one. (We ignore the page arg,
	// as with draw_bitmap -- everything composites onto the one screen surface.)
	if (fn == "fill_rectangle" && args.size() >= 6) {
		ctx.gfx->fillRect(static_cast<int>(args[1]), static_cast<int>(args[2]),
		                  static_cast<int>(args[3]), static_cast<int>(args[4]),
		                  static_cast<uint8_t>(args[5]));
		rt() << "  [fill " << args[1] << "," << args[2] << "-" << args[3] << ","
		     << args[4] << " c" << args[5] << "]" << std::endl;
		result = 0;
		return true;
	}
	// refresh_window / color_fade / light_fade: present the screen.
	if (fn == "refresh_window" || fn == "color_fade" || fn == "light_fade") {
		// The compass widget is on AESOP page 104; when its page is refreshed the
		// freshly-drawn compass (disc + facing indicator) is on screen -- snapshot
		// it so later HUD redraws can't erase the indicator (it's only redrawn by
		// the bytecode on a turn). See Graphics::snapshotCompass/restoreCompass.
		if (fn == "refresh_window" && !args.empty() && args[0] == 104 &&
		    gCompassDirty) {
			ctx.gfx->snapshotCompass();
			gCompassDirty = false;
		}
		auto presentStart = std::chrono::steady_clock::now();
		ctx.gfx->update();
		if (gPerf) {
			auto now = std::chrono::steady_clock::now();
			auto sincePrev = std::chrono::duration_cast<std::chrono::microseconds>(
			                    now - gLastPresent).count();
			auto updateUs = std::chrono::duration_cast<std::chrono::microseconds>(
			                    now - presentStart).count();
			std::cerr << "[perf] " << gDrawCount << " draws ("
			          << static_cast<double>(gDrawNanos) / 1000.0
			          << " us total), present="
			          << static_cast<double>(updateUs) / 1000.0
			          << " ms, gap="
			          << static_cast<double>(sincePrev) / 1000.0 << " ms\n";
			gDrawCount = 0;
			gDrawNanos = 0;
			gLastPresent = now;
		}
		if (!gFirstPresentLogged) {
			gFirstPresentLogged = true;
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			              std::chrono::steady_clock::now() - gBootStart).count();
			std::cout << "First frame presented at " << ms
			          << " ms after launch." << std::endl;
			// THIRDEYE_QUIT_AFTER_FIRST: exit right after the first frame so
			// `time <cmd>` measures wall-clock launch -> first window (the session
			// otherwise runs until you close the window).
			if (std::getenv("THIRDEYE_QUIT_AFTER_FIRST"))
				throw QuitRequested{};
		}
		// (THIRDEYE_DUMP frame snapshots moved into Graphics::update() so
		// EVERY present is captured -- including pixel_fade / particle-effect
		// frames presented outside this handler.)
		rt() << "  [present]" << std::endl;
		result = 0;
		return true;
	}
	// set_mouse_pointer(table, number, hot_X, hot_Y, ...)
	if (fn == "set_mouse_pointer" && args.size() >= 2) {
		try {
			ctx.gfx->loadMouse(fetch(args[0]), static_cast<uint16_t>(args[1]));
			rt() << "  [cursor]" << std::endl;
		} catch (const std::exception &e) {
			rt() << "  [cursor failed: " << e.what() << "]" << std::endl;
		}
		result = 0;
		return true;
	}
	// --- text output (GRAPHICS.C): numbered text windows + print ---
	// text_window(wndnum, wnd) -- bind the text window to a graphics window;
	// we record its horizontal extent so centered/right text can be placed.
	if (fn == "text_window" && args.size() >= 2) {
		int32_t x0, y0, x1, y1;
		if (ctx.events.windowRect(args[1], x0, y0, x1, y1))
			ctx.gfx->setTextWindow(static_cast<int>(args[0]), x0, y0, x1, y1,
			                       static_cast<int>(args[1]));
		result = 0;
		return true;
	}
	// wipe_window(wnd, color) -- fill the bound subwindow's rectangle with
	// the given palette index. Matches the original GIL2VFX_wipe_window /
	// VFX_pane_wipe: a flat-colour fill, not a backdrop restore. The encounter
	// "prepare outtake box" handler calls this with color 20 (the stone-panel
	// palette index) to clear the bottom dialog box before drawing the border
	// lines + text -- using wipeTextBox here brought back whatever HUD art
	// (compass / portraits) was last drawn into the box instead of a clean
	// stone background, so the Florin dialog read as floating over the HUD.
	// The save-picker contract about text_window not wiping is unaffected;
	// that's a separate handler.
	if (fn == "wipe_window" && args.size() >= 2) {
		int32_t x0, y0, x1, y1;
		if (ctx.gfx && ctx.events.windowRect(args[0], x0, y0, x1, y1)) {
			uint8_t color = static_cast<uint8_t>(args[1] & 0xFF);
			ctx.gfx->fillRect(x0, y0, x1, y1, color);
		}
		result = 0;
		return true;
	}
	// text_style(wndnum, font, justify) -- font + justification (0/1/2).
	if (fn == "text_style" && args.size() >= 2) {
		try {
			ctx.gfx->setTextFont(static_cast<int>(args[0]),
			                     static_cast<int>(args[1]), fetch(args[1]));
		} catch (const std::exception &) {}
		if (args.size() >= 3)
			ctx.gfx->setTextJustify(static_cast<int>(args[0]),
			                        static_cast<int>(args[2]));
		result = 0;
		return true;
	}
	// text_color(wndnum, current, new) -- the remap target colour.
	if (fn == "text_color" && args.size() >= 3) {
		ctx.gfx->setTextColor(static_cast<int>(args[0]),
		                      static_cast<uint8_t>(args[2]));
		result = 0;
		return true;
	}
	// text_xy(wndnum, htab, vtab) -- move the text cursor.
	if (fn == "text_xy" && args.size() >= 3) {
		ctx.gfx->setTextXY(static_cast<int>(args[0]), static_cast<int>(args[1]),
		                   static_cast<int>(args[2]));
		result = 0;
		return true;
	}
	// get_text_x/get_text_y(wndnum) -- the text cursor (htab/vtab). The SOP
	// uses these to position follow-on prints (e.g. inline item names).
	if ((fn == "get_text_x" || fn == "get_text_y") && args.size() >= 1) {
		int x = 0, y = 0;
		ctx.gfx->textCursor(static_cast<int>(args[0]), x, y);
		result = fn == "get_text_x" ? x : y;
		return true;
	}
	// char_width(wndnum, ch) / font_height(wndnum) -- glyph metrics of the
	// window's bound font. The spell/camp menus measure text with these.
	if (fn == "char_width" && args.size() >= 2) {
		result = ctx.gfx->textCharWidth(static_cast<int>(args[0]),
		                                static_cast<uint8_t>(args[1]));
		return true;
	}
	if (fn == "font_height" && args.size() >= 1) {
		result = ctx.gfx->textFontHeight(static_cast<int>(args[0]));
		return true;
	}
	// crout(wndnum): newline (GRAPHICS.C sprint(wnd, "\n")).
	if (fn == "crout" && args.size() >= 1) {
		ctx.gfx->printText(static_cast<int>(args[0]), "\n");
		result = 0;
		return true;
	}
	// dprint(format, ...): diagnostic print into the main text window (0).
	if (fn == "dprint" && args.size() >= 1) {
		std::string out = formatSop(ctx.vm.readString(args[0]), args, 1, ctx);
		ctx.gfx->printText(0, out);
		rt() << "  [dprint \"" << out << "\"]" << std::endl;
		result = 0;
		return true;
	}
	// draw_line(page, x1, y1, x2, y2, color) / draw_rectangle(wnd, x1, y1,
	// x2, y2, color) / hash_rectangle(...): GIL2VFX line/outline/checkerboard
	// primitives. Pages composite onto the one screen surface (as elsewhere).
	if (fn == "draw_line" && args.size() >= 6) {
		ctx.gfx->drawLine(static_cast<int>(args[1]), static_cast<int>(args[2]),
		                  static_cast<int>(args[3]), static_cast<int>(args[4]),
		                  static_cast<uint8_t>(args[5]));
		result = 0;
		return true;
	}
	if (fn == "draw_rectangle" && args.size() >= 6) {
		int x1 = static_cast<int>(args[1]), y1 = static_cast<int>(args[2]);
		int x2 = static_cast<int>(args[3]), y2 = static_cast<int>(args[4]);
		uint8_t c = static_cast<uint8_t>(args[5]);
		ctx.gfx->drawLine(x1, y1, x2, y1, c);
		ctx.gfx->drawLine(x1, y2, x2, y2, c);
		ctx.gfx->drawLine(x1, y1, x1, y2, c);
		ctx.gfx->drawLine(x2, y1, x2, y2, c);
		result = 0;
		return true;
	}
	if (fn == "hash_rectangle" && args.size() >= 6) {
		ctx.gfx->hashRect(static_cast<int>(args[1]), static_cast<int>(args[2]),
		                  static_cast<int>(args[3]), static_cast<int>(args[4]),
		                  static_cast<uint8_t>(args[5]));
		result = 0;
		return true;
	}
	// solid_bar_graph(x0, y0, x1, y1, lb_border, tr_border, bkgnd, grn, yel,
	// red, val, min, crit, max): the HUD HP/food bars (GRAPHICS.C, verbatim).
	if (fn == "solid_bar_graph" && args.size() >= 14) {
		int x0 = static_cast<int>(args[0]), y0 = static_cast<int>(args[1]);
		int x1 = static_cast<int>(args[2]), y1 = static_cast<int>(args[3]);
		uint8_t lb = static_cast<uint8_t>(args[4]);
		uint8_t tr = static_cast<uint8_t>(args[5]);
		uint8_t bkgnd = static_cast<uint8_t>(args[6]);
		uint8_t grn = static_cast<uint8_t>(args[7]);
		uint8_t yel = static_cast<uint8_t>(args[8]);
		uint8_t red = static_cast<uint8_t>(args[9]);
		int32_t val = args[10], mn = args[11], crit = args[12], mx = args[13];
		ctx.gfx->drawLine(x0, y0, x0, y1, lb);
		ctx.gfx->drawLine(x0, y1, x1, y1, lb);
		ctx.gfx->drawLine(x1, y1 - 1, x1, y0, tr);
		ctx.gfx->drawLine(x1, y0, x0 + 1, y0, tr);
		int btop = y0 + 1, bbtm = y1 - 1, blft = x0 + 1, brgt = x1 - 1;
		int width = brgt - blft;
		if (val > mx) val = mx;
		else if (val < mn) val = mn;
		int range = mx - mn;
		if (range == 0) range = 1; // degenerate SOP args; original would /0
		int grayx = blft + (val - mn) * width / range;
		if (grayx != brgt)
			ctx.gfx->fillRect(grayx, btop, brgt, bbtm, bkgnd);
		uint8_t color = (val <= crit) ? red : (val * 3 >= mx ? grn : yel);
		if (val != mn && grayx == blft)
			grayx = blft + 1;
		if (grayx != blft)
			ctx.gfx->fillRect(blft, btop, grayx, bbtm, color);
		result = 0;
		return true;
	}
	// pixel_fade(src_wnd, dest_wnd, intervals): VFX_pixel_fade -- a random-
	// pixel dissolve from the src pane's content into the dest pane. We
	// composite on one surface, so Graphics::pixelFade dissolves from the
	// last PRESENTED frame (what the player sees) to the current surface
	// content over `intervals` frames -- covers both the wipe-then-fade-out
	// and draw-then-fade-in idioms without per-page surfaces.
	if (fn == "pixel_fade" && args.size() >= 3) {
		int x0 = 0, y0 = 0, x1 = 319, y1 = 199;
		// The dest window's rect bounds the effect; src as fallback.
		if (!ctx.events.windowRect(args[1], x0, y0, x1, y1))
			ctx.events.windowRect(args[0], x0, y0, x1, y1);
		ctx.gfx->pixelFade(x0, y0, x1, y1, static_cast<int>(args[2]));
		result = 0;
		return true;
	}
	// visible_bitmap_rect(x, y, flip, table, number, array): bounding box of
	// the shape's visible pixels -> 4 WORDs into a SOP static array; returns
	// 0 if nothing visible. The SOP hit-tests monster clicks with this.
	if (fn == "visible_bitmap_rect" && args.size() >= 6) {
		int out[4] = {0, 0, 0, 0};
		bool vis = false;
		try {
			vis = ctx.gfx->visibleBitmapRect(
			    fetch(args[3]), static_cast<uint16_t>(args[4]),
			    static_cast<int>(args[0]), static_cast<int>(args[1]),
			    static_cast<int>(args[2]), out);
		} catch (const std::exception &) {}
		if (uint8_t *arr = staticBytePtr(ctx, args[5], 8)) {
			for (int i = 0; i < 4; ++i) { // little-endian, unaligned-safe
				uint16_t v = static_cast<uint16_t>(out[i]);
				arr[i * 2] = static_cast<uint8_t>(v & 0xff);
				arr[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
			}
		}
		result = vis ? 1 : 0;
		return true;
	}
	// sprint(wndnum, format_addr, args...) -- printf-style print to a text
	// window. The format string lives at a tagged address (e.g. a PC's "name"
	// in static space, or an inline code string like "%d of %d"); %d/%s are
	// filled from the trailing args. Used for character names + HP readouts.
	if (fn == "sprint" && args.size() >= 2) {
		std::string out = formatSop(ctx.vm.readString(args[1]), args, 2, ctx);
		ctx.gfx->printText(static_cast<int>(args[0]), out);
		rt() << "  [sprint \"" << out << "\"]" << std::endl;
		result = 0;
		return true;
	}
	// print(wndnum, string_resource, args...) -- the resource is a "S:"+text
	// format string; trailing args fill %d/%s (e.g. the HP "%d of %d" readout).
	// The save-picker also uses this for slot names, where args[1] is a
	// Static-tagged address into our runtime-owned slot-name buffer (not a
	// resource id) -- try readString first when the value looks like a
	// tagged address (top nibble = Code/Static/Extern, i.e. >= 0x80000000).
	if (fn == "print" && args.size() >= 2) {
		try {
			std::string fmt;
			uint32_t u = static_cast<uint32_t>(args[1]);
			// Code/Stack/Static/Extern (top nibble >= 0x8) -- treat as a
			// tagged string address. We commit to the tagged-read path:
			// even if the resolved string is empty (a legitimate empty slot
			// name, say), DON'T fall back to fetch() on a u32 that isn't a
			// resource id -- that would either print garbage or swallow a
			// "resource not found" exception silently.
			bool isTagged = (u >> 28) >= 0x8u;
			if (isTagged) {
				fmt = ctx.vm.readString(args[1]);
			} else {
				std::vector<uint8_t> &s = fetch(args[1]);
				size_t off = (s.size() >= 2 && s[0] == 'S' && s[1] == ':') ? 2 : 0;
				for (size_t i = off; i < s.size() && s[i] != 0; ++i)
					fmt.push_back(static_cast<char>(s[i]));
			}
			std::string text = formatSop(fmt, args, 2, ctx);
			ctx.gfx->printText(static_cast<int>(args[0]), text);
			rt() << "  [text \"" << text << "\"]" << std::endl;
		} catch (const std::exception &) {}
		result = 0;
		return true;
	}
	// --- do_dots / do_ice: EYE.C's blocking particle effects (fireball burst /
	// cone of cold). Verbatim ports of the fixed-point WORD physics; the
	// sprite-occlusion mask reads (GIL2VFX_read_dot on the view page) are
	// skipped -- we composite pages onto one surface, so the particles draw
	// over whatever is there, clipped to the view window. Both effects save
	// the exact pixels they cover and restore them each frame, so they leave
	// no residue. Frame-paced like the originals' vblank waits.
	if ((fn == "do_dots" || fn == "do_ice") && ctx.gfx != nullptr) {
		constexpr int kMaxParticles = 150;
		constexpr int kAccurShift   = 6;  // ACCUR: fixed-point fraction bits
		constexpr int kFriction     = 1;  // per-frame horizontal drag
		constexpr int kGravity      = 5;  // per-frame downward accel

		static std::minstd_rand prng{0xD075};
		// EYE.C rnd(): inclusive range on both ends.
		auto randInRange = [&](int lo, int hi) -> int {
			if (hi <= lo) return lo;
			return lo + static_cast<int>(prng() % (hi - lo + 1));
		};
		// The `colors` argument is a BYTE* -- a Code-space colour ramp table
		// (most common), or occasionally a static buffer. Return 0 (XCOLOR) if
		// unreadable: the particle loop treats that as "particle expired".
		auto colorRampAt = [&](VM::Value colorsAddr, uint32_t rampIdx) -> int {
			int codeByteVal = ctx.vm.codeByte(colorsAddr, rampIdx);
			if (codeByteVal >= 0)
				return codeByteVal;
			VM::Addr addr = VM::decodeAddr(colorsAddr);
			if (addr.space == VM::AddrSpace::Static ||
			    addr.space == VM::AddrSpace::Extern) {
				try {
					if (uint8_t *sp = ctx.objects.staticsPtr(
					        addr.obj, addr.offset + rampIdx, 1))
						return *sp;
				} catch (const std::exception &) {}
			}
			return 0;
		};

		// One entry per particle. WORD physics on positions/velocities
		// (matches the original's 16-bit overflow behaviour); saved
		// background is the exact 32-bit pixel we overwrote last frame.
		struct Particle {
			int16_t  xPos = 0, yPos = 0;   // fixed-point position (>>ACCUR = px)
			int16_t  xVel = 0, yVel = 0;
			int16_t  colorStep = 0;        // rate: per-frame increment of...
			int16_t  colorIdx  = 0;        //   ...index into the colour ramp
			int16_t  emitDelay = 0;        // ice: frames before this dot starts
			int      savedX = 0, savedY = 0;
			uint32_t savedBg = 0;
			bool     bgIsSaved = false;
		};
		std::array<Particle, kMaxParticles> particles{};
		int frameGuard = 0; // runaway backstop (both effects fizzle out)

		if (fn == "do_dots" && args.size() >= 10) {
			// do_dots(view, scrn, expX, expY, scaleIn, power, count, life,
			//         upval, colors) -- fireball burst centred on expX/expY.
			static const int kFloorPerScale[4] = {119, 103, 79, 63};
			int scaleIn = static_cast<int>(args[4]) & 3;
			int power   = static_cast<int>(args[5]);
			int count   = std::min<int>(static_cast<int>(args[6]), kMaxParticles);
			int life    = std::max<int>(1, static_cast<int>(args[7]));
			int upBias  = std::clamp<int>(static_cast<int>(args[8]), 0, 7);
			int floorY  = kFloorPerScale[scaleIn];
			int scale   = scaleIn ? scaleIn - 1 : 0; // >>scale applied to positions
			// Simulation bounds (matches EYE.C literals; wider than the view).
			constexpr int kRoofY  = 0;
			constexpr int kLWallX = -100;
			constexpr int kRWallX = 276;
			// View clip (from the SOP window handle) -- dots only draw inside.
			int viewX0 = 0, viewY0 = 0, viewX1 = 175, viewY1 = 119;
			ctx.events.windowRect(args[0], viewX0, viewY0, viewX1, viewY1);
			int centerX = static_cast<int>(args[2]);
			int centerY = static_cast<int>(args[3]);
			int colorHi = (8 << 8) / life;
			int colorLo = (4 << 8) / life;

			for (int i = 0; i < count; i++) {
				Particle &p = particles[i];
				p.xVel = static_cast<int16_t>(randInRange(0, power) - (power >> 1));
				p.yVel = static_cast<int16_t>(randInRange(0, power) - (power >> 1)
				                              - (power >> (8 - upBias)));
				p.colorStep = static_cast<int16_t>(randInRange(colorLo, colorHi));
				p.colorIdx  = static_cast<int16_t>(scale << 8);
			}
			int active = 2; // sentinel: "just entered, nothing to erase yet"
			while (active && ++frameGuard < 1500) {
				if (active != 2)
					for (int i = count - 1; i >= 0; i--) {
						Particle &p = particles[i];
						if (p.bgIsSaved)
							ctx.gfx->pokePixel(p.savedX, p.savedY, p.savedBg);
					}
				active = 0;
				for (int i = 0; i < count; i++) {
					Particle &p = particles[i];
					// Friction: nudge each frame TOWARDS zero horizontal velocity.
					p.xVel = static_cast<int16_t>(p.xVel + (p.xVel > 0 ? -kFriction : kFriction));
					p.xPos = static_cast<int16_t>(p.xPos + p.xVel);
					p.yVel = static_cast<int16_t>(p.yVel + kGravity);
					p.yPos = static_cast<int16_t>(p.yPos + p.yVel);
					p.colorIdx = static_cast<int16_t>(p.colorIdx + p.colorStep);
					int screenX = ((p.xPos >> kAccurShift) >> scale) + centerX;
					int screenY = ((p.yPos >> kAccurShift) >> scale) + centerY;
					// Bounce off floor / ceiling / side walls (half-elastic).
					if (screenY >= floorY || screenY < kRoofY)
						p.yVel = static_cast<int16_t>(-(p.yVel >> 1));
					if (screenX >= kRWallX || screenX < kLWallX)
						p.xVel = static_cast<int16_t>(-(p.xVel >> 1));
					if (screenY > floorY) screenY = floorY;
					int pixColor = colorRampAt(args[9],
					    static_cast<uint16_t>(p.colorIdx) >> 8);
					p.bgIsSaved = false;
					if (pixColor != 0) {
						active = 1;
						if (screenX >= viewX0 && screenX <= viewX1 &&
						    screenY >= viewY0 && screenY <= viewY1) {
							p.savedBg = ctx.gfx->peekPixel(screenX, screenY);
							p.savedX = screenX;
							p.savedY = screenY;
							p.bgIsSaved = true;
							ctx.gfx->pokePixel(screenX, screenY,
							    ctx.gfx->mapColor(static_cast<uint8_t>(pixColor)));
						}
					} else p.colorStep = 0;
				}
				ctx.gfx->update();
				// The particle loop blocks for up to ~50s of wall clock;
				// pumpHost drains the SDL queue (including SDL_EVENT_QUIT via
				// QuitRequested) so the OS doesn't beach-ball the window.
				pumpHost(*ctx.gfx, ctx.events, ctx.objects, ctx.res);
				SDL_Delay(32); // two vblank waits in the original (~30 fps)
			}
			result = 0;
			return true;
		}
		if (fn == "do_ice" && args.size() >= 7) {
			// do_ice(view, scrn, count, magnitude, gravity, life, colors) --
			// cone of cold: particles launched outward on the 4 cardinal axes
			// from screen centre, arcing back toward it.
			int count     = std::min<int>(static_cast<int>(args[2]), kMaxParticles);
			int magnitude = static_cast<int>(args[3]) << kAccurShift;
			int gravity   = std::max<int>(1, static_cast<int>(args[4]));
			int life      = std::max<int>(1, static_cast<int>(args[5]));
			int viewX0 = 0, viewY0 = 0, viewX1 = 175, viewY1 = 119;
			ctx.events.windowRect(args[0], viewX0, viewY0, viewX1, viewY1);
			constexpr int kCenterX = 88, kCenterY = 48;
			// Half-pixel offset: launch position sits mid-pixel so the
			// discrete >>ACCUR quantises symmetrically around each axis.
			constexpr int16_t kHalf = 1 << (kAccurShift - 1);
			// Per-particle scaling factor for the eventual colour-ramp step.
			int colorHi = (8 << 8) / life;
			int colorLo = (4 << 8) / life;

			// Ballistics helper: integrate v += gravity, distance += v until
			// distance reaches `magnitude`. Both `distance` and `velocity` are
			// wide `int` here (was `int16_t` = CodeQL cpp/comparison-with-wider-
			// type on `while (t < magnitude)`: overflow before reach = infinite
			// loop). We narrow to int16_t when stashing into the particle so
			// the WORD physics behaviour still holds through the sim.
			auto simulateLaunch = [gravity](int mag, int16_t &outVel, int16_t &outDist) {
				int velocity = 0, distance = 0;
				while (distance < mag) {
					velocity += gravity;
					distance += velocity;
				}
				outVel  = static_cast<int16_t>(velocity);
				outDist = static_cast<int16_t>(distance);
			};

			for (int i = 0; i < count; i++) {
				Particle &p = particles[i];
				int launchMag = randInRange(magnitude >> 2, magnitude);
				int16_t launchVel = 0, launchDist = 0;
				simulateLaunch(launchMag, launchVel, launchDist);

				// Emit direction: 4 cardinals + optional 180° flip.
				switch (prng() & 3) {
				case 0: // East:  x mid-pixel, y offset, moving east
					p.xPos = kHalf; p.yPos = launchDist; p.xVel = launchVel; break;
				case 1: // South: x offset, y mid-pixel, moving south
					p.xPos = launchDist; p.yPos = kHalf; p.yVel = launchVel; break;
				case 2: // West:  x mid-pixel, negated y offset, moving west
					p.xPos = kHalf; p.yPos = static_cast<int16_t>(-launchDist);
					p.xVel = launchVel; break;
				default: // North: negated x offset, y mid-pixel, moving north
					p.xPos = static_cast<int16_t>(-launchDist); p.yPos = kHalf;
					p.yVel = launchVel; break;
				}
				if (prng() & 1) { // half the particles emit in the opposite direction
					p.xVel = static_cast<int16_t>(-p.xVel);
					p.yVel = static_cast<int16_t>(-p.yVel);
				}
				p.colorStep = static_cast<int16_t>(randInRange(colorLo, colorHi));
				p.emitDelay = static_cast<int16_t>(randInRange(0, life >> 2));
			}
			int active = 2;
			// The ice cone uses a weaker "attractive" gravity (gravity*7/8)
			// when a velocity is already moving toward centre -- accelerate
			// less, so particles overshoot and oscillate around the origin
			// rather than snapping to it.
			int gravitySoft = (gravity >> 1) + (gravity >> 2) + (gravity >> 3);
			while (active && ++frameGuard < 1500) {
				if (active != 2)
					for (int i = count - 1; i >= 0; i--) {
						Particle &p = particles[i];
						if (p.bgIsSaved)
							ctx.gfx->pokePixel(p.savedX, p.savedY, p.savedBg);
					}
				active = 0;
				for (int i = 0; i < count; i++) {
					Particle &p = particles[i];
					if (p.emitDelay) {
						p.emitDelay--;
					} else {
						// Gravity pulls each particle back toward the origin
						// on each axis: full gravity when already moving TOWARDS,
						// softer 7/8 gravity when moving AWAY.
						if (p.xPos > 0)
							p.xVel = static_cast<int16_t>(p.xVel - (p.xVel > 0 ? gravity : gravitySoft));
						else
							p.xVel = static_cast<int16_t>(p.xVel + (p.xVel < 0 ? gravity : gravitySoft));
						if (p.yPos > 0)
							p.yVel = static_cast<int16_t>(p.yVel - (p.yVel > 0 ? gravity : gravitySoft));
						else
							p.yVel = static_cast<int16_t>(p.yVel + (p.yVel < 0 ? gravity : gravitySoft));
						p.xPos = static_cast<int16_t>(p.xPos + p.xVel);
						p.yPos = static_cast<int16_t>(p.yPos + p.yVel);
						p.colorIdx = static_cast<int16_t>(p.colorIdx + p.colorStep);
					}
					int screenX = (p.xPos >> kAccurShift) + kCenterX;
					int screenY = (p.yPos >> kAccurShift) + kCenterY;
					int pixColor = colorRampAt(args[6],
					    static_cast<uint16_t>(p.colorIdx) >> 8);
					p.bgIsSaved = false;
					if (pixColor != 0) {
						active = 1;
						if (!p.emitDelay &&
						    screenX >= viewX0 && screenX <= viewX1 &&
						    screenY >= viewY0 && screenY <= viewY1) {
							p.savedBg = ctx.gfx->peekPixel(screenX, screenY);
							p.savedX = screenX;
							p.savedY = screenY;
							p.bgIsSaved = true;
							ctx.gfx->pokePixel(screenX, screenY,
							    ctx.gfx->mapColor(static_cast<uint8_t>(pixColor)));
						}
					} else p.colorStep = 0;
				}
				ctx.gfx->update();
				pumpHost(*ctx.gfx, ctx.events, ctx.objects, ctx.res); // drain quit / OS events
				SDL_Delay(16); // one vblank wait in the original (~60 fps)
			}
			result = 0;
			return true;
		}
	}
	return false;
}

} // namespace THIRDEYE::runtime::graphics
