#include "internal.hpp"

#include "../graphics/graphics.hpp"
#include "../resources/res.hpp"
#include "../vm/events.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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
		// arg[6] = flip/mirror (GIL2VFX: 1=X, 2=Y, 3=both); the view draws
		// right-hand walls as the X-mirror of the left-hand shape.
		int mirror = args.size() > 6 ? static_cast<int>(args[6]) : 0;
		// The compass facing indicator (page 102, resource 187) is only drawn on
		// a turn; mark it so the next compass refresh re-snapshots (see below).
		if (page == 102 && table == 187)
			gCompassDirty = true;
		try {
			auto t0 = gPerf ? std::chrono::steady_clock::now()
			                : std::chrono::steady_clock::time_point{};
			// table = the AESOP resource number; stable for the lifetime of
			// the loaded asset, so it's a safe shape-cache identity.
			ctx.gfx->drawImage(fetch(table), number, x, y, true, mirror,
			                   static_cast<uint32_t>(table));
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
		// Debug aid: with THIRDEYE_DUMP set, snapshot every presented frame so a
		// headless run (no window) can still be inspected as a BMP. If the path
		// contains "%d", write numbered frames (otherwise overwrite the same file).
		// We substitute "%d" by hand rather than passing the env var to snprintf
		// as a format string: env input is data, not a format spec, and stray
		// conversions like "%s"/"%n" in the path would otherwise crash or worse.
		if (const char *d = std::getenv("THIRDEYE_DUMP")) {
			static int frameNo = 0;
			std::string path = d;
			auto pos = path.find("%d");
			if (pos != std::string::npos) {
				path.replace(pos, 2, std::to_string(frameNo++));
			}
			ctx.gfx->saveScreenshot(path.c_str());
		}
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
	return false;
}

} // namespace THIRDEYE::runtime::graphics
