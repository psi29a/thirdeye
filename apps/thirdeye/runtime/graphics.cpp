#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../vm/events.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
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
	if (fn == "assign_window" && args.size() >= 5) {
		result = ctx.events.assignWindow(args[0], args[1], args[2], args[3], args[4]);
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
		uint16_t first = region < 5 ? kFirstColor[region] : 0;
		try {
			ctx.gfx->setPaletteRange(fetch(args[1]), first);
			rt() << "  [palette region " << region << "]" << std::endl;
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
		if (table == 187)
			gCompassDirty = true;
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
			if (table == 190)
				ctx.gfx->fillRect(x, y, x + 319, y + 199, 0);
			ctx.gfx->drawImage(fetch(table), number, x, y, true, mirror,
			                   static_cast<uint32_t>(table), scale);
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
		std::string out = formatSop(ctx.vm.readString(args[0]), args, 1, ctx.vm);
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
		std::string out = formatSop(ctx.vm.readString(args[1]), args, 2, ctx.vm);
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
			std::string text = formatSop(fmt, args, 2, ctx.vm);
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
		constexpr int kMaxDots = 150, kAccur = 6, kFriction = 1, kGravity = 5;
		static std::minstd_rand rng{0xD075};
		auto rnd = [&](int lo, int hi) -> int { // EYE.C rnd(): inclusive range
			if (hi <= lo) return lo;
			return lo + static_cast<int>(rng() % (hi - lo + 1));
		};
		// colors is a BYTE* -- a Code-space ramp table (or a static buffer).
		auto colAt = [&](VM::Value addr, uint32_t idx) -> int {
			int b = ctx.vm.codeByte(addr, idx);
			if (b >= 0)
				return b;
			VM::Addr a = VM::decodeAddr(addr);
			if (a.space == VM::AddrSpace::Static ||
			    a.space == VM::AddrSpace::Extern) {
				try {
					if (uint8_t *p = ctx.objects.staticsPtr(a.obj, a.offset + idx, 1))
						return *p;
				} catch (const std::exception &) {}
			}
			return 0; // XCOLOR: ends the particle
		};
		std::array<int16_t, kMaxDots> xpos{}, ypos{}, xvel{}, yvel{}, colcnt{},
		    colidx{}, delay{};
		// Saved background per dot: position + raw pixel, restored each frame.
		std::array<int, kMaxDots> sx{}, sy{};
		std::array<uint32_t, kMaxDots> sbg{};
		std::array<bool, kMaxDots> saved{};
		int guard = 0;
		if (fn == "do_dots" && args.size() >= 10) {
			// do_dots(view, scrn, exp_x, exp_y, scale, power, dots, life, upval,
			//         colors)
			static const int kFloorTbl[4] = {119, 103, 79, 63};
			int scale = static_cast<int>(args[4]) & 3;
			int power = static_cast<int>(args[5]);
			int dots = std::min<int>(static_cast<int>(args[6]), kMaxDots);
			int life = std::max<int>(1, static_cast<int>(args[7]));
			int upval = std::clamp<int>(static_cast<int>(args[8]), 0, 7);
			int floor = kFloorTbl[scale];
			if (scale) scale--;
			int roof = 0, lwall = -100, rwall = 276;
			int top = 0, bottom = 119, lside = 0, rside = 175;
			ctx.events.windowRect(args[0], lside, top, rside, bottom);
			int cx = static_cast<int>(args[2]), cy = static_cast<int>(args[3]);
			for (int i = 0; i < dots; i++) {
				xpos[i] = ypos[i] = 0;
				xvel[i] = static_cast<int16_t>(rnd(0, power) - (power >> 1));
				yvel[i] = static_cast<int16_t>(rnd(0, power) - (power >> 1) -
				                               (power >> (8 - upval)));
				colcnt[i] = static_cast<int16_t>(
				    rnd((4 << 8) / life, (8 << 8) / life));
				colidx[i] = static_cast<int16_t>(scale << 8);
			}
			int active = 2;
			while (active && ++guard < 1500) {
				if (active != 2)
					for (int i = dots - 1; i >= 0; i--)
						if (saved[i])
							ctx.gfx->pokePixel(sx[i], sy[i], sbg[i]);
				active = 0;
				for (int i = 0; i < dots; i++) {
					if (xvel[i] > 0) xvel[i] -= kFriction;
					else             xvel[i] += kFriction;
					xpos[i] += xvel[i];
					yvel[i] += kGravity;
					ypos[i] += yvel[i];
					colidx[i] += colcnt[i];
					int px = ((xpos[i] >> kAccur) >> scale) + cx;
					int py = ((ypos[i] >> kAccur) >> scale) + cy;
					if (py >= floor || py < roof) yvel[i] = static_cast<int16_t>(-(yvel[i] >> 1));
					if (px >= rwall || px < lwall) xvel[i] = static_cast<int16_t>(-(xvel[i] >> 1));
					if (py > floor) py = floor;
					int pixcol = colAt(args[9],
					                   static_cast<uint16_t>(colidx[i]) >> 8);
					saved[i] = false;
					if (pixcol != 0) {
						active = 1;
						if (px >= lside && px <= rside && py >= top && py <= bottom) {
							sbg[i] = ctx.gfx->peekPixel(px, py);
							sx[i] = px; sy[i] = py; saved[i] = true;
							ctx.gfx->pokePixel(px, py,
							                   ctx.gfx->mapColor(
							                       static_cast<uint8_t>(pixcol)));
						}
					} else colcnt[i] = 0;
				}
				ctx.gfx->update();
				SDL_Delay(32); // two vblank waits in the original (~30 fps)
			}
			result = 0;
			return true;
		}
		if (fn == "do_ice" && args.size() >= 7) {
			// do_ice(view, scrn, dots, mag, grav, life, colors)
			int dots = std::min<int>(static_cast<int>(args[2]), kMaxDots);
			int mag = static_cast<int>(args[3]) << kAccur;
			int grav = std::max<int>(1, static_cast<int>(args[4]));
			int life = std::max<int>(1, static_cast<int>(args[5]));
			int top = 0, bottom = 119, lside = 0, rside = 175;
			ctx.events.windowRect(args[0], lside, top, rside, bottom);
			const int cx = 88, cy = 48;
			for (int i = 0; i < dots; i++) {
				int m = rnd(mag >> 2, mag);
				int16_t v = 0, t = 0;
				while (t < m) { v = static_cast<int16_t>(v + grav);
				                t = static_cast<int16_t>(t + v); }
				switch (rng() & 3) {
				case 0: xpos[i] = 1 << (kAccur - 1); ypos[i] = t;
				        xvel[i] = v; yvel[i] = 0; break;
				case 1: xpos[i] = t; ypos[i] = 1 << (kAccur - 1);
				        xvel[i] = 0; yvel[i] = v; break;
				case 2: xpos[i] = 1 << (kAccur - 1); ypos[i] = static_cast<int16_t>(-t);
				        xvel[i] = v; yvel[i] = 0; break;
				default: xpos[i] = static_cast<int16_t>(-t); ypos[i] = 1 << (kAccur - 1);
				        xvel[i] = 0; yvel[i] = v; break;
				}
				if (rng() & 1) { xvel[i] = static_cast<int16_t>(-xvel[i]);
				                 yvel[i] = static_cast<int16_t>(-yvel[i]); }
				colcnt[i] = static_cast<int16_t>(
				    rnd((4 << 8) / life, (8 << 8) / life));
				colidx[i] = 0;
				delay[i] = static_cast<int16_t>(rnd(0, life >> 2));
			}
			int active = 2;
			while (active && ++guard < 1500) {
				if (active != 2)
					for (int i = dots - 1; i >= 0; i--)
						if (saved[i])
							ctx.gfx->pokePixel(sx[i], sy[i], sbg[i]);
				active = 0;
				int grav78 = (grav >> 1) + (grav >> 2) + (grav >> 3);
				for (int i = 0; i < dots; i++) {
					if (delay[i])
						delay[i]--;
					else {
						if (xpos[i] > 0)
							xvel[i] = static_cast<int16_t>(
							    xvel[i] - (xvel[i] > 0 ? grav : grav78));
						else
							xvel[i] = static_cast<int16_t>(
							    xvel[i] + (xvel[i] < 0 ? grav : grav78));
						if (ypos[i] > 0)
							yvel[i] = static_cast<int16_t>(
							    yvel[i] - (yvel[i] > 0 ? grav : grav78));
						else
							yvel[i] = static_cast<int16_t>(
							    yvel[i] + (yvel[i] < 0 ? grav : grav78));
						xpos[i] += xvel[i];
						ypos[i] += yvel[i];
						colidx[i] += colcnt[i];
					}
					int px = (xpos[i] >> kAccur) + cx;
					int py = (ypos[i] >> kAccur) + cy;
					int pixcol = colAt(args[6],
					                   static_cast<uint16_t>(colidx[i]) >> 8);
					saved[i] = false;
					if (pixcol != 0) {
						active = 1;
						if (!delay[i] && px >= lside && px <= rside &&
						    py >= top && py <= bottom) {
							sbg[i] = ctx.gfx->peekPixel(px, py);
							sx[i] = px; sy[i] = py; saved[i] = true;
							ctx.gfx->pokePixel(px, py,
							                   ctx.gfx->mapColor(
							                       static_cast<uint8_t>(pixcol)));
						}
					} else colcnt[i] = 0;
				}
				ctx.gfx->update();
				SDL_Delay(16); // one vblank wait in the original (~60 fps)
			}
			result = 0;
			return true;
		}
	}
	return false;
}

} // namespace THIRDEYE::runtime::graphics
