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

// The dungeon 3D view renders into off-screen page 94, blitted to its view
// window (the dungeon's assign_subwindow 0,0-175,119 = 176x120). We composite
// onto one screen surface, so page-94 draws are clipped to this window.
constexpr uint16_t kViewPage = 94;
constexpr int kViewW = 176, kViewH = 120;

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
	// set_x1/set_x2(window, x): set the active draw clip for `window` to a
	// left/right pixel column. The dungeon's "draw objects" calls these per
	// view-cell to narrow each cell's draws to a horizontal strip; at the end
	// of the handler it restores set_x1(view, 0) / set_x2(view, 175). x = -1
	// means "no clip" (use the window's natural edge). We track the most
	// recent pair as a single global and apply it to the view-page clip in
	// draw_bitmap (see kViewPage block). The `window` arg is ignored: the
	// dungeon's view is the only consumer in the in-game render path, so a
	// single global pair matches every observed call. Without honoring these,
	// wide shapes (stairs/doors) for an oblique cell bleed past their column
	// into adjacent cells -- the "stair shape inside the side wall" artifact
	// when strafing past a stair cell.
	if (fn == "set_x1" && args.size() >= 2) {
		gViewClipX1 = static_cast<int>(args[1]);
		result = 0;
		return true;
	}
	if (fn == "set_x2" && args.size() >= 2) {
		gViewClipX2 = static_cast<int>(args[1]);
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
		// The dungeon 3D view renders into off-screen page 94; we composite onto
		// the one screen surface, so without the page model the wide wall shapes
		// would bleed out of the view into the character panels. Clip page-94
		// draws to the dungeon's view window (assign_subwindow 0,0-175,119).
		bool viewPage = page == kViewPage;
		if (viewPage) {
			// Honor the per-cell horizontal clip set by set_x1/set_x2 (see
			// runtime-fn block). x = -1 means "use the view's natural edge".
			int cx1 = gViewClipX1 < 0 ? 0 : std::max(0, gViewClipX1);
			int cx2 = gViewClipX2 < 0 ? kViewW - 1 : std::min(kViewW - 1, gViewClipX2);
			if (cx2 < cx1) cx2 = cx1; // empty clip = nothing draws
			ctx.gfx->setClip(cx1, 0, cx2 - cx1 + 1, kViewH);
		}
		// arg[6] = flip/mirror (GIL2VFX: 1=X, 2=Y, 3=both); the view draws
		// right-hand walls as the X-mirror of the left-hand shape.
		int mirror = args.size() > 6 ? static_cast<int>(args[6]) : 0;
		// The compass facing indicator (page 104, resource 187) is only drawn on
		// a turn; mark it so the next compass refresh re-snapshots (see below).
		if (page == 104 && table == 187)
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
			rt() << "  [drew " << table << ":" << number << " @ " << x
			     << "," << y << (mirror ? " M" : "") << "]" << std::endl;
		} catch (const std::exception &e) {
			rt() << "  [draw failed: " << e.what() << "]" << std::endl;
		}
		if (viewPage)
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
			std::cerr << "[perf] " << gDrawCount << " draws (" << gDrawNanos/1000.0
			          << " us total), present=" << updateUs/1000.0
			          << " ms, gap=" << sincePrev/1000.0 << " ms\n";
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
			ctx.gfx->setTextWindow(static_cast<int>(args[0]), x0, y0, x1, y1);
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
	if (fn == "print" && args.size() >= 2) {
		try {
			std::vector<uint8_t> &s = fetch(args[1]);
			size_t off = (s.size() >= 2 && s[0] == 'S' && s[1] == ':') ? 2 : 0;
			std::string fmt;
			for (size_t i = off; i < s.size() && s[i] != 0; ++i)
				fmt.push_back(static_cast<char>(s[i]));
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
