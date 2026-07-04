#include "engine.hpp"
#include "resources/res.hpp"
#include "resources/gffi.hpp"
#include "sound/sound.hpp"
#include "graphics/graphics.hpp"
#include "vm/vm.hpp"
#include "vm/objects.hpp"
#include "vm/events.hpp"
#include "savegame/transfer.hpp"
#include "savegame/lvl_tmp.hpp"
#include "chargen/screen.hpp"
#include "runtime/internal.hpp"

#include <components/files/configurationmanager.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <fstream>
#include <set>
#include <string_view>
#include <iterator>
#include <map>
#include <memory>
#include <random>

THIRDEYE::Engine::Engine(Files::ConfigurationManager& configurationManager) :
		mNewGame(false), mUseSound(true), mDebug(false), mRenderer(false), mGame(
				GAME_UNKN), mScale(1), mCfgMgr(configurationManager) {
	std::cout << "Initializing Thirdeye... ";

	std::srand(std::time(NULL));

	std::cout << "done!" << std::endl;
}

THIRDEYE::Engine::~Engine() {

}

// Setup engine via parameters
void THIRDEYE::Engine::setGame(std::string game) {
	std::transform(game.begin(), game.end(), game.begin(),
			[](unsigned char c) { return std::tolower(c); });

	if (game == "eob3")
		mGame = GAME_EOB3;
	else if (game == "hack")
		mGame = GAME_HACK;
	else
		mGame = GAME_UNKN;
}

void THIRDEYE::Engine::setGameData(std::string gameData) {
	mGameData = std::filesystem::path(gameData);
}

void THIRDEYE::Engine::setForceVM(bool forceVM) {
	mForceVM = forceVM;
}
void THIRDEYE::Engine::setSkipMenu(bool skipMenu) {
	mSkipMenu = skipMenu;
}
void THIRDEYE::Engine::setSkipIntro(bool skipIntro) {
	mSkipIntro = skipIntro;
}
void THIRDEYE::Engine::setChargen(bool chargen) {
	mChargen = chargen;
}
void THIRDEYE::Engine::setChargenTest(bool chargenTest) {
	mChargenTest = chargenTest;
}

// The game-data path may be either a directory (in which case we append the
// selected game's main .RES) or a direct path to a .RES file (used as-is, with
// the game auto-detected from the filename).
std::filesystem::path THIRDEYE::Engine::resolveResourceFile() {
	namespace fs = std::filesystem;

	if (fs::is_regular_file(mGameData)) {
		std::string name = mGameData.filename().string();
		std::transform(name.begin(), name.end(), name.begin(),
				[](unsigned char c) { return std::tolower(c); });
		if (name == "eye.res")
			mGame = GAME_EOB3;
		else if (name == "hack.res" || name == "open.res")
			mGame = GAME_HACK;
		// Any other .RES (e.g. SAMPLE.RES) is loaded directly; game stays as-is.
		return mGameData;
	}

	// Treat as a directory: append the main resource file for the game.
	switch (mGame) {
	case GAME_HACK:
		return mGameData / "HACK.RES";
	case GAME_EOB3:
	default:
		return mGameData / "EYE.RES";
	}
}
void THIRDEYE::Engine::setDebugMode(bool debug) {
	mDebug = debug;
}
void THIRDEYE::Engine::setSoundUsage(bool nosound) {
	mUseSound = !nosound;
}
void THIRDEYE::Engine::setScale(uint16_t scale) {
	mScale = scale;
}
void THIRDEYE::Engine::setRenderer(bool renderer) {
	mRenderer = renderer;
}

namespace {

// MSG_CREATE: the message the engine sends to a freshly created instance to
// boot/initialize it (see eob3_research/runtime/DEFS.H + RTOBJECT.C).
constexpr int MSG_CREATE = 0;

// Runtime functions + savegame parsers live in dedicated subdirs (see
// CLAUDE.md). Pull the names we still reference here into this TU.
using TransferState = THIRDEYE::savegame::TransferState;
using THIRDEYE::savegame::loadLevelObjects;
using THIRDEYE::runtime::QuitRequested; // thrown by pumpHost on quit/ESC
using THIRDEYE::runtime::Relaunch;      // thrown by launch() for the program chain
using THIRDEYE::runtime::gRtTrace;      // per-call trace gate; set from --debug
using THIRDEYE::runtime::gBootStart;    // set at go(); for timing prints
using THIRDEYE::runtime::gPerf;         // THIRDEYE_PERF=1 -- per-present timing
using THIRDEYE::runtime::gLastPresent;  // wall-clock of previous present

// Resolve a child directory case-insensitively: try the exact name first
// (the fast path on macOS/Windows and Linux installs that already match),
// fall back to scanning the parent for any entry whose name compares
// equal under tolower(). Returns the resolved path on success, or
// `parent/name` unchanged (so callers still get a stable path for error
// messages) when no match is found. Used for CHARGEN/, SAVEGAME/, etc.
// where the install's on-disk casing varies (see existing chargen/savegame
// case-checks elsewhere in this TU).
std::filesystem::path resolveChildCI(const std::filesystem::path &parent,
                                     const std::string &name) {
	std::error_code ec;
	auto exact = parent / name;
	if (std::filesystem::exists(exact, ec)) return exact;
	if (!std::filesystem::is_directory(parent, ec)) return exact;
	std::string lower = name;
	for (auto &c : lower) c = static_cast<char>(std::tolower(
	    static_cast<unsigned char>(c)));
	for (auto &entry : std::filesystem::directory_iterator(parent, ec)) {
		auto candidate = entry.path().filename().string();
		std::string cl = std::move(candidate);
		for (auto &c : cl) c = static_cast<char>(std::tolower(
		    static_cast<unsigned char>(c)));
		if (cl == lower) return entry.path();
	}
	return exact;
}

// THIRDEYE_RECORD=1 (print at exit) or =<path> (also write the raw sequence to
// a file): capture the session's real keypresses + clicks in THIRDEYE_AUTOWALK's
// token language, on its 40-pump window cadence, and emit the replayable
// sequence when the engine quits. Closes the record->replay loop: play a
// session once by hand, then pin it in a CI/valgrind run via the printed
// THIRDEYE_AUTOWALK. Only input that reaches pumpHost's SDL loop is captured
// (the chargen screen runs its own SDL loop and is not recorded).
struct InputRecorder {
	bool enabled = false;
	std::string dest;   // env value: "1" = print only; anything else = file path
	long tick = -1;     // pump counter -- the same timebase AUTOWALK replays on
	struct Rec {
		long tick;
		std::string token;
	};
	std::vector<Rec> recs;

	std::string sequence() const {
		// One token per 40-pump window, `_` filling the idle windows so replay
		// keeps the session's pacing. Round UP to the next window: replay fires
		// at a window's first pump, so rounding down could fire up to 39 pumps
		// EARLIER than the human pressed the key (e.g. before the menu that
		// consumed it existed). A same-window burst spills into consecutive
		// windows -- order preserved, timing stretched.
		std::string out;
		long nextWin = 0;
		bool first = true;
		for (const Rec &r : recs) {
			long win = std::max(r.tick / 40 + 1, nextWin);
			// Cap the lead-in: idle-at-the-title-screen before the first input
			// is dead time, and replay crawls through it (badly so under
			// valgrind). Keep a few windows for boot to settle; inner gaps
			// stay faithful (they may be waiting out a cutscene/dialog).
			if (first) {
				win = std::min(win, 4L);
				first = false;
			}
			for (; nextWin < win; ++nextWin)
				out += "_,";
			out += r.token;
			out += ',';
			++nextWin;
		}
		out += '_'; // AUTOWALK repeats its last token forever; end on a no-op
		return out;
	}

	void dump() const {
		if (recs.empty()) {
			std::cout << "  [record: no input captured]" << std::endl;
			return;
		}
		const std::string seq = sequence();
		if (dest != "1") {
			std::ofstream f(dest, std::ios::trunc);
			f << seq << '\n';
			std::cout << "  [record: sequence "
			          << (f ? "written to " + dest : "FAILED writing " + dest)
			          << "]" << std::endl;
		}
		std::cout << "  [record: " << recs.size() << " inputs over "
		          << (tick + 1) << " pumps]\n"
		          << "THIRDEYE_AUTOWALK=" << seq << std::endl;
	}
};
InputRecorder gRecorder;

} // close anon namespace -- pumpHost lives in THIRDEYE::runtime so the
  // dispatch_event handler in runtime/event.cpp can call it via internal.hpp.

// The host seam (see the design note in CLAUDE.md). The kernel's main loop is a
// busy-wait -- `while (!quit) dispatch_event();` -- so each time the bytecode
// polls dispatch_event/peek_event we do the work DOS did with interrupts +
// vblank: pump SDL input into AESOP events, present the frame, and yield the CPU
// when the queue is idle. That turns the 100% spin into an event-driven,
// frame-paced loop and makes the window render live (instead of only after the
// loop ends). Throws QuitRequested on window-close / ESC.
void THIRDEYE::runtime::pumpHost(GRAPHICS::Graphics &gfx, VM::EventSystem &events) {
	// THIRDEYE_RECORD: arm the input recorder (see InputRecorder above) and
	// advance its pump clock BEFORE polling, so input seen this pump gets this
	// pump's tick.
	static const bool recording = [] {
		if (const char *v = std::getenv("THIRDEYE_RECORD")) {
			gRecorder.enabled = true;
			gRecorder.dest = v;
		}
		return gRecorder.enabled;
	}();
	if (recording)
		++gRecorder.tick;

	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		// SDL3 dropped SDL2's automatic event-coordinate rescaling under
		// logical presentation; convert in place so motion/button events
		// land in the 320x200 logical space the bytecode regions use.
		SDL_ConvertEventToRenderCoordinates(gfx.getRenderer(), &ev);
		switch (ev.type) {
		case SDL_EVENT_QUIT:
			throw QuitRequested{};
		case SDL_EVENT_KEY_DOWN: {
			// The SOP code expects DOS/BIOS key codes: printable keys are ASCII
			// (SDL keysyms already are, incl. ESC=0x1b/Enter=0x0d), but the arrow
			// keys are (scancode << 8) with a zero ASCII byte. SDL's own arrow
			// keysyms (0x4000_00xx) match nothing, so translate them. ESC is *not*
			// special-cased to quit: the bytecode handles it (in-game it opens the
			// camp menu via notify msg 272; in the title menu it's "Abandon the
			// Quest"). Forced quit is the window close button (SDL_EVENT_QUIT above).
			//
			// WASD/QE/C use the PHYSICAL key position (SDL_SCANCODE_*), so they work
			// regardless of layout (QWERTY/AZERTY/QWERTZ): W/S = fwd/back, A/D = strafe,
			// Q/E = turn, C = camp. They post the same DOS codes the arrow keys do (the
			// kernel notifies move forward/back/strafe/turn on those). (No I->inventory:
			// it defaulted to Bob with no indication of whose pack it opened.)
			int32_t key = 0;
			switch (ev.key.scancode) {
			case SDL_SCANCODE_W: key = 0x4800; break; // move forward
			case SDL_SCANCODE_S: key = 0x5000; break; // move backward
			case SDL_SCANCODE_A: key = 0x4b00; break; // strafe left
			case SDL_SCANCODE_D: key = 0x4d00; break; // strafe right
			case SDL_SCANCODE_Q: key = 0x4700; break; // turn left
			case SDL_SCANCODE_E: key = 0x4900; break; // turn right
			case SDL_SCANCODE_C: key = 0x63;   break; // camp (kernel notifies 'c'->272)
			default: break;
			}
			if (key == 0) { // not a movement scancode -> arrows + ASCII via keysym
				SDL_Keycode k = ev.key.key;
				switch (k) {
				case SDLK_UP:    key = 0x4800; break;
				case SDLK_DOWN:  key = 0x5000; break;
				case SDLK_LEFT:  key = 0x4b00; break;
				case SDLK_RIGHT: key = 0x4d00; break;
				default:
					key = (k > 0 && k < 0x80) ? k : 0; // ASCII (ESC, Enter, ...)
					break;
				}
			}
			if (key != 0) {
				events.postEvent(0, VM::SYS_KEYDOWN, key);
				if (gRecorder.enabled) {
					char tok[16];
					std::snprintf(tok, sizeof(tok), "%x", key);
					gRecorder.recs.push_back({gRecorder.tick, tok});
				}
			}
			break;
		}
		case SDL_EVENT_MOUSE_MOTION: {
			int lx, ly;
			gfx.mouseToLogical(static_cast<int>(ev.motion.x),
			                   static_cast<int>(ev.motion.y), lx, ly);
			events.mouseMove(lx, ly);
			break;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP: {
			// Drive click state from the event edge, not SDL_GetMouseState():
			// EventSystem::mouseButton() only reacts to transitions, and a
			// snapshot can miss a queued press/release pair when several
			// events arrive in the same pump.
			static bool leftDown = false, rightDown = false;
			bool down = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
			if (ev.button.button == SDL_BUTTON_LEFT)  leftDown = down;
			else if (ev.button.button == SDL_BUTTON_RIGHT) rightDown = down;
			int lx, ly;
			gfx.mouseToLogical(static_cast<int>(ev.button.x),
			                   static_cast<int>(ev.button.y), lx, ly);
			events.mouseMove(lx, ly);
			events.mouseButton(leftDown, rightDown);
			// Record press edges only: the AUTOWALK L/R click token replays the
			// release itself (~5 pumps after the press), so drags don't round-trip.
			if (gRecorder.enabled && down &&
			    (ev.button.button == SDL_BUTTON_LEFT ||
			     ev.button.button == SDL_BUTTON_RIGHT)) {
				char tok[24];
				std::snprintf(tok, sizeof(tok), "%c%d:%d",
				              ev.button.button == SDL_BUTTON_LEFT ? 'L' : 'R',
				              lx, ly);
				gRecorder.recs.push_back({gRecorder.tick, tok});
			}
			break;
		}
		default:
			break;
		}
	}
	// Heartbeat: a ~30 Hz monotonic timer that drives the bytecode's timed
	// behaviour (menu fade-in, cursor blink, animation). Coalesced into a single
	// SYS_TIMER event (see EventSystem::postTimer / INTRFACE.C timer_callback).
	events.postTimer(static_cast<int32_t>(SDL_GetTicks() >> 5));

	// Debug: THIRDEYE_AUTOWALK is the scripted-input test harness. Comma-separated
	// tokens, one per ~40 pumps (last token repeats):
	//   <hex>      hex scancode SYS_KEYDOWN (4800=fwd 5000=back 4b00/4d00=strafe-L/R
	//              4700/4900=turn-L/R 0d=Enter 1b=Esc)
	//   L<x>:<y>   left-click at logical (x,y) -- press, then release ~5 ticks later
	//   R<x>:<y>   right-click  (so a real attack click on a weapon icon is one token)
	//   _          no-op (wait one window)
	// Examples:
	//   4800                     hold forward
	//   5000,0d,0d               menu Down + Enter + Enter (title -> save picker)
	//   4900,4800,4800,R126:133  turn-right, walk x2, right-click PC0's right hand
	if (const char *aw = std::getenv("THIRDEYE_AUTOWALK")) {
		// Cache the parsed sequence in statics: AUTOWALK is a process-launch
		// env var (set once before exec), so re-parsing every event pump
		// (~30 Hz) is wasted work.
		enum class TokKind : uint8_t { Key, Lclick, Rclick, Wait };
		struct Tok { TokKind kind; int32_t scancode; int x; int y; };
		static std::vector<Tok> toks;
		static int tick = -1;
		if (toks.empty()) {
			std::string s = aw;
			for (size_t p = 0; p < s.size(); ) {
				size_t e = s.find(',', p);
				std::string one = s.substr(p, e == std::string::npos ? e : e - p);
				if (e == std::string::npos) p = s.size(); else p = e + 1;
				if (one.empty()) continue;
				Tok t{TokKind::Wait, 0, 0, 0};
				if (one == "_") {
					t.kind = TokKind::Wait;
				} else if ((one[0] == 'L' || one[0] == 'R') &&
				           one.find(':') != std::string::npos) {
					int x = 0, y = 0;
					if (std::sscanf(one.c_str() + 1, "%d:%d", &x, &y) == 2) {
						t.kind = (one[0] == 'L') ? TokKind::Lclick : TokKind::Rclick;
						t.x = x; t.y = y;
					} else continue;
				} else {
					int32_t c = static_cast<int32_t>(
						std::strtol(one.c_str(), nullptr, 16));
					if (c == 0) continue;
					t.kind = TokKind::Key;
					t.scancode = c;
				}
				toks.push_back(t);
			}
			if (toks.empty()) toks.push_back({TokKind::Key, 0x4800, 0, 0});
		}
		++tick;
		size_t idx = static_cast<size_t>(tick / 40);
		if (idx >= toks.size()) idx = toks.size() - 1;
		const auto &tok = toks[idx];
		int phase = tick % 40;
		if (phase == 0) {
			switch (tok.kind) {
			case TokKind::Key:
				events.postEvent(0, VM::SYS_KEYDOWN, tok.scancode); break;
			case TokKind::Lclick: case TokKind::Rclick: {
				int lx, ly;
				gfx.mouseToLogical(tok.x, tok.y, lx, ly);
				events.mouseMove(lx, ly);
				events.mouseButton(tok.kind == TokKind::Lclick,
				                   tok.kind == TokKind::Rclick);
				break;
			}
			case TokKind::Wait: break;
			}
		} else if (phase == 5 &&
		           (tok.kind == TokKind::Lclick || tok.kind == TokKind::Rclick)) {
			events.mouseButton(false, false);
		}
	}
	// Debug: THIRDEYE_AUTOKEY=<decimal SDL scancode> pushes a synthetic key-down
	// with that physical scancode every ~40 pumps, so the real key handler (WASD/QE/
	// I/C scancode mapping) can be exercised headless (e.g. 26=W, 4=A, 22=S, 7=D,
	// 20=Q, 8=E, 12=I, 6=C).
	if (const char *ak = std::getenv("THIRDEYE_AUTOKEY")) {
		static int ktick = 0;
		// Repeat for movement keys; THIRDEYE_AUTOKEY1 fires ONCE (for toggles I/C, so
		// the screen doesn't flicker open/closed each repeat).
		bool once = std::getenv("THIRDEYE_AUTOKEY1") != nullptr;
		++ktick;
		if ((once && ktick == 60) || (!once && ktick % 40 == 0)) {
			SDL_Event ke{};
			ke.type = SDL_EVENT_KEY_DOWN;
			ke.key.scancode = static_cast<SDL_Scancode>(std::atoi(ak));
			ke.key.key = SDLK_UNKNOWN;
			SDL_PushEvent(&ke);
		}
	}
	// Debug: THIRDEYE_CLICK=x,y left-clicks that screen point every ~50 pumps
	// (mouse-move there, press, then release a few pumps later) so a headless run
	// can verify region clicks (compass arrows, CAMP, weapon icons).
	if (const char *cl = std::getenv("THIRDEYE_CLICK")) {
		// THIRDEYE_CLICK="x,y" or a ";"-separated sequence "x,y;x,y;..." cycled one
		// per ~50 pumps (so e.g. "200,26;188,75" = open Bob's inventory, then click an
		// item). Coords are logical (320x200) -- the space SDL auto-scales events to.
		static std::vector<std::pair<int, int>> pts;
		static size_t idx = 0; // index into pts (compared against pts.size())
		static int ctick = 0;
		if (pts.empty()) {
			std::string s = cl;
			size_t p = 0;
			while (p < s.size()) {
				size_t e = s.find(';', p);
				std::string one = s.substr(p, e == std::string::npos ? e : e - p);
				int x = 0, y = 0;
				if (std::sscanf(one.c_str(), "%d,%d", &x, &y) == 2)
					pts.emplace_back(x, y);
				if (e == std::string::npos) break;
				p = e + 1;
			}
		}
		// THIRDEYE_CLICK1: play the sequence once and then STOP (don't re-click), so
		// a toggle screen (equipment/camp) stays open for a screenshot.
		static bool clickOnce = std::getenv("THIRDEYE_CLICK1") != nullptr;
		int phase = (++ctick) % 50;
		if (phase == 0 && !pts.empty() && !(clickOnce && idx >= pts.size())) {
			// Play the sequence once, then hold on the last point (so e.g.
			// "openInv;clickItem" opens once, then repeatedly clicks the item).
			size_t i = idx < pts.size() ? idx : pts.size() - 1;
			auto [cx, cy] = pts[i];
			++idx;
			int lx, ly;
			gfx.mouseToLogical(cx, cy, lx, ly);
			std::cout << "  [click (" << lx << "," << ly << ")]" << std::endl;
			events.mouseMove(lx, ly);
			events.mouseButton(true, false);
		} else if (phase == 3) {
			events.mouseButton(false, false);
		}
	}
	// Debug: THIRDEYE_HOVER="x,y" parks the mouse there (move only, no click) so a
	// headless run can verify hover behaviour (e.g. the menu highlight tracking the
	// cursor). Re-asserted each pump with a 1px wobble so the region machinery sees a
	// move and fires ENTER once the bytecode has registered its regions (a single
	// early move would land before the menu's option regions exist).
	if (const char *hv = std::getenv("THIRDEYE_HOVER")) {
		static int wob = 0;
		int x = 0, y = 0;
		if (std::sscanf(hv, "%d,%d", &x, &y) == 2) {
			int lx, ly;
			gfx.mouseToLogical(x, y, lx, ly);
			events.mouseMove(lx + (wob ^= 1), ly);
		}
	}

	gfx.update();                 // present whatever the bytecode has drawn
	if (!events.peekEvent())
		SDL_Delay(10);            // idle: yield instead of spinning
}

namespace { // reopen the anon namespace -- defaultRuntimeCall + registerClasses
            // are TU-private.

// Runtime-function dispatcher. Each category mirrors one of
// ../eob3_research/runtime/*.C (see apps/thirdeye/runtime/). Ordering:
// rtcode + event run first (hot paths) so the timing/trace prints below don't
// drown the high-frequency calls; rtobject/eye/graphics get the trace + first-
// time-seen log, since those are where new bring-up work surfaces.
VM::Value defaultRuntimeCall(VM::ObjectSystem &objects, VM::EventSystem &events,
                             GRAPHICS::Graphics *gfx, RESOURCES::Resource &res,
                             std::map<int32_t, int32_t> &mem, TransferState &xfer,
                             VM::Interpreter &vm, const std::string &fn,
                             const std::vector<VM::Value> &args,
                             MIXER::Mixer *mixer = nullptr) {
	THIRDEYE::runtime::Context ctx{objects, events, gfx, res, mem, xfer, vm, mixer};
	VM::Value result = 0;

	// Hot paths: math, peekmem/pokemem, the event queue + dispatch_event host
	// pump. These fire thousands of times per frame; keep them above the trace.
	if (THIRDEYE::runtime::rtcode::tryHandle(ctx, fn, args, result)) return result;
	if (THIRDEYE::runtime::event::tryHandle(ctx, fn, args, result))  return result;

	// THIRDEYE_TIMING: stamp every distinct runtime call with elapsed-ms-since-
	// launch so the boot path's cost is visible. We log the FIRST call to each
	// name (i.e. when it first becomes hot); `=2` logs every call (noisy, but
	// pinpoints exact spend). The first dispatch_event timing closes the loop:
	// everything up to it is pure interpreter work driving these calls.
	if (const char *tv = std::getenv("THIRDEYE_TIMING")) {
		bool verbose = std::string(tv) == "2";
		static std::set<std::string> seen;
		bool first = seen.insert(fn).second;
		if (first || verbose) {
			auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			              std::chrono::steady_clock::now() - gBootStart).count();
			std::cout << "[timing " << ms << " ms] " << fn;
			if (fn == "create_program" && args.size() >= 2)
				std::cout << "(" << args[0] << ", " << args[1] << ")";
			std::cout << (first ? " (first)\n" : "\n");
		}
	}

	if (gRtTrace) {
		std::cout << "    CALL " << fn << "(";
		for (size_t i = 0; i < args.size(); ++i) {
			std::cout << (i ? ", " : "");
			std::string str = vm.readCodeString(static_cast<uint32_t>(args[i]));
			if (!str.empty())
				std::cout << '"' << str << '"';
			else
				std::cout << args[i];
		}
		std::cout << ")";
	}

	if (THIRDEYE::runtime::rtobject::tryHandle(ctx, fn, args, result)) return result;
	if (THIRDEYE::runtime::eye::tryHandle(ctx, fn, args, result))      return result;
	if (THIRDEYE::runtime::graphics::tryHandle(ctx, fn, args, result)) return result;
	if (THIRDEYE::runtime::sound::tryHandle(ctx, fn, args, result))    return result;

	THIRDEYE::runtime::rt() << "  [stub -> 0]" << std::endl;
	// Always log the first hit on each unimplemented runtime function (max a few
	// dozen lines per run) -- this is how we find what the SOP needs next.
	static std::set<std::string> stubbed;
	if (stubbed.insert(fn).second)
		std::cout << "[stub] " << fn << " (first call, " << args.size()
		          << " args)" << std::endl;
	return 0;
}

// Register every SOP code object in the resource as a class. Registering all of
// them first lets parent links (the class hierarchy) resolve at dispatch time.
void registerClasses(RESOURCES::Resource &resource, VM::ObjectSystem &objects) {
	for (const std::string &name : resource.getCodeResourceNames()) {
		VM::SopClass cls;
		cls.name = name;
		cls.number = static_cast<uint16_t>(resource.getResourceNumber(name));
		cls.code = resource.getAsset(name);
		cls.header = VM::Interpreter(cls.code).header();
		auto isVarTag = [](const std::string &tag) { // "B:/W:/L:<name>" entries
			return tag.size() > 2 && tag[1] == ':' &&
			       (tag[0] == 'B' || tag[0] == 'W' || tag[0] == 'L');
		};
		for (const auto &exp : resource.getExports(name)) {
			if (exp.first.rfind("M:", 0) == 0) // "M:<n>" -> handler entry offset
				cls.handlers[std::stoi(exp.first.substr(2))] =
					static_cast<uint32_t>(std::stoul(exp.second));
			else if (isVarTag(exp.first)) // public static -> offset in this class
				cls.exportedVars[exp.first] =
					static_cast<uint16_t>(std::stoul(exp.second));
			else if (exp.first == "N:PARENT") {
				// The real superclass is the ".EXPT" N:PARENT resource number (a
				// clean class number, e.g. axe -> 1688 "weapons"), NOT the code
				// header's `parent` field (an unrelated encoded value daesop can't
				// resolve either). This is how a subclass inherits its base's
				// handlers/statics (e.g. items get "draw"/"usable hand" from
				// weapons/armor). Top-level objects have no N:PARENT and keep the
				// header value (which doesn't resolve -> treated as no parent).
				cls.header.parent = static_cast<uint32_t>(std::stoul(exp.second));
			}
		}
		for (const auto &imp : resource.getImports(name)) {
			if (imp.first.rfind("C:", 0) == 0) { // "C:<name>" -> runtime-fn number
				cls.imports[std::stoi(imp.second)] = imp.first.substr(2);
			} else if (isVarTag(imp.first)) {
				// extern variable: def is "<XR-offset>,<source-class>" (RTLINK.C)
				size_t comma = imp.second.find(',');
				if (comma != std::string::npos)
					cls.externs[std::stoul(imp.second.substr(0, comma))] = {
						imp.first, static_cast<uint32_t>(
							std::stoul(imp.second.substr(comma + 1)))};
			}
		}
		objects.addClass(std::move(cls));
	}
}

} // namespace

// Drive the SOP bytecode of each code object through the VM: run every exported
// message handler and report where each ends. Useful for small resources like
// SAMPLE.RES; for a full game use bootObject() instead.
void THIRDEYE::Engine::runResourceVM(RESOURCES::Resource &resource) {
	std::vector<std::string> codeNames = resource.getCodeResourceNames();
	if (codeNames.empty()) {
		std::cout << "\nNo SOP code objects in this resource file." << std::endl;
		return;
	}

	std::cout << "\nRunning SOP bytecode (" << codeNames.size()
	          << " code object(s))..." << std::endl;

	VM::ObjectSystem objects;
	VM::EventSystem events(objects);
	std::map<int32_t, int32_t> mem; // peekmem/pokemem cells
	TransferState xfer;             // char-gen party transfer file (unused here)
	events.setVerbose(mDebug || std::getenv("THIRDEYE_AUTOWALK") ||
	                  std::getenv("THIRDEYE_CLICK"));
	objects.setTrace(mDebug);
	// Gate the runtime-call trace: on with --debug, or when a debug env var wants it.
	gRtTrace = mDebug || std::getenv("THIRDEYE_AUTOWALK") ||
	           std::getenv("THIRDEYE_TESTOBJ") || std::getenv("THIRDEYE_CLICK") || std::getenv("THIRDEYE_AUTOKEY");
	objects.setRuntimeCall(
		[&](VM::Interpreter &vm, const std::string &fn,
		           const std::vector<VM::Value> &args) {
			// runResourceVM stays headless (gfx = nullptr) -- it's for small
			// resources like SAMPLE.RES, not the full game.
			return defaultRuntimeCall(objects, events, nullptr, resource, mem, xfer,
			                          vm, fn, args);
		});
	objects.setMaxSteps(2000000); // bring-up safety: stop runaway loops
	registerClasses(resource, objects);

	for (const std::string &name : codeNames) {
		uint16_t classNumber = 0;
		objects.findClassByName(name, classNumber);
		const VM::SopClass *cls = objects.classByNumber(classNumber);
		const VM::SopHeader &h = cls->header;

		std::cout << "\n=== code object \"" << name << "\" ==="
		          << "  (static=" << h.static_size << " import=" << h.import_resource
		          << " export=" << h.export_resource << " parent="
		          << (h.parent == 0xFFFFFFFFu ? std::string("none")
		                                      : std::to_string(h.parent))
		          << ")" << std::endl;

		int objIndex = objects.createInstance(classNumber);
		for (const auto &handler : cls->handlers) {
			std::cout << "  handler M:" << handler.first << " @ " << handler.second
			          << ": ";
			if (mDebug)
				std::cout << std::endl;
			try {
				VM::Value result = objects.send(objIndex, handler.first, {});
				std::cout << "END (returned " << result << ")" << std::endl;
			} catch (const VM::VmError &e) {
				std::cout << "stopped -- " << e.what() << std::endl;
			} catch (const Relaunch &r) {
				// AMAZE demo's exit sentinel (launch("xxx.exe")): no bootObject
				// wrapper here to chain sub-programs, so treat as terminal.
				std::cout << "END (launch \"" << r.program << "\")" << std::endl;
			} catch (const QuitRequested &q) {
				std::cout << "END (quit"
				          << (q.reason.empty() ? "" : ": " + q.reason) << ")"
				          << std::endl;
			}
		}
	}
}

// Boot a single object the way the original interpreter does (create instance +
// send MSG_CREATE). This runs the real boot path and stops at the first runtime
// function / opcode we haven't built yet -- our data-driven bring-up to-do list.
void THIRDEYE::Engine::bootObject(RESOURCES::Resource &resource,
                                  const std::string &objectName) {
	// Open a window so the SOP runtime's draw calls have somewhere to render.
	// If there's no display (headless/CI), fall back to running without graphics
	// (the runtime draw functions then no-op through to the stub).
	std::unique_ptr<GRAPHICS::Graphics> gfx;
	try {
		gfx = std::make_unique<GRAPHICS::Graphics>(mScale, mRenderer);
	} catch (const std::exception &e) {
		std::cout << "Graphics unavailable (" << e.what()
		          << "); booting headless." << std::endl;
	}

	// In-game sound mixer (OpenAL). Only useful with graphics; when headless we
	// still construct it so the sound runtime calls have a target, but they no-op
	// if the device failed to open. Lives for the whole game session.
	MIXER::Mixer mixer;

	VM::ObjectSystem objects;
	VM::EventSystem events(objects);
	events.setVerbose(mDebug || std::getenv("THIRDEYE_AUTOWALK") ||
	                  std::getenv("THIRDEYE_CLICK"));
	objects.setTrace(mDebug);
	// Gate the runtime-call trace ([drew]/[text]/CALL lines): on with --debug, or
	// when a debug env var wants it (AUTOWALK/TESTOBJ/CLICK/AUTOKEY). Without this
	// the in-game trace was silently off (it was only set in runResourceVM).
	gRtTrace = mDebug || std::getenv("THIRDEYE_AUTOWALK") ||
	           std::getenv("THIRDEYE_TESTOBJ") || std::getenv("THIRDEYE_CLICK") ||
	           std::getenv("THIRDEYE_AUTOKEY");
	gPerf = std::getenv("THIRDEYE_PERF") != nullptr;
	gLastPresent = std::chrono::steady_clock::now();

	// Seed the boot "mode" that start.MSG_CREATE reads via peekmem(1264) and
	// CASEs on (see the start disassembly). The 4-char modes are stored
	// little-endian:
	//   "INTR" -> title menu (title screen + "Begin a New Quest" / "Continue
	//             the Quest" / "Summon Heroes" buttons)
	//   "CINE" -> straight to the game (party comes from a savegame via
	//             resume_*). With a fresh EOB3 install this lands on the
	//             pre-shipped "Quick Start Party" save (Sir Mikeal et al. on
	//             level 3) -- not empty -- because Westwood/SSI ship
	//             SAVEGAME/ITEMS.TMP with the install for exactly this.
	//   "CHGN" -> run the chargen-transfer SOP first (= read CHARGEN/CREATE.SAV
	//             into live PCs, then enter the game). On a fresh install
	//             CREATE.SAV holds the dev sample party (Bob/Carol/Ted/Alice);
	//             if the user ran CHGEN.EXE to roll their own, it's theirs.
	constexpr int32_t MODE_INTR = 0x494e5452; // 'I''N''T''R' LE -> title menu
	constexpr int32_t MODE_CHGN = 0x4348474e; // 'C''H''G''N' LE -> char-gen + game
	constexpr int32_t MODE_CINE = 0x43494e45; // 'C''I''N''E' LE -> straight into the game
	                                          //                    (resume_level builds the party from the save)
	// Clean gate-drop for THIRDEYE_CONTINUE: when --skip-menu is set we auto-detect.
	// ITEMS.TMP present  -> CINE (continue from save: resume_level creates PCs).
	// ITEMS.TMP missing  -> CHGN (run chargen-transfer: copy CREATE.SAV's PCs).
	// --chargen overrides: force CHGN regardless of save state (start a new game).
	// Resolve against the loaded .RES's parent dir, not the process CWD --
	// otherwise launching thirdeye from any directory other than the game
	// dir picks CHGN even when a save exists (matches resume_level's lookup).
	auto saveExists = [&]() {
		std::error_code ec;
		auto root = resource.resourcePath().parent_path();
		return std::filesystem::exists(root / "SAVEGAME" / "ITEMS.TMP", ec) ||
		       std::filesystem::exists(root / "savegame" / "items.tmp", ec);
	};
	std::map<int32_t, int32_t> mem;
	// Cold boot = 0: cell 1264 is the DOS Inter-Application Communication Area
	// (0000:04F0), which holds zeros when no prior program poked it. start's
	// create handler dispatches on it, and its default path is the DOS boot
	// experience: pokemem(1264,'INTR') + launch("cine.exe") -- intro first,
	// then the title menu when cine chains back (start.dasm LBL_79). Seeding
	// a mode here is only for the shortcut flags.
	int32_t bootMode = 0;
	if (mChargen)        bootMode = MODE_CHGN;
	else if (mSkipMenu)  bootMode = saveExists() ? MODE_CINE : MODE_CHGN;
	else if (mSkipIntro) bootMode = MODE_INTR;
	mem.insert_or_assign(1264, bootMode);
	std::cout << "  [boot mode = "
	          << (bootMode == MODE_CINE ? "CINE (continue from save)"
	            : bootMode == MODE_CHGN ? "CHGN (chargen + game)"
	            : bootMode == MODE_INTR ? "INTR (title menu)"
	            : "cold boot (intro, then menu)")
	          << "]" << std::endl;
	TransferState xfer; // char-gen party transfer file (CHARGEN\CREATE.SAV)

	objects.setRuntimeCall(
		[&](VM::Interpreter &vm, const std::string &fn,
		           const std::vector<VM::Value> &args) {
			return defaultRuntimeCall(objects, events, gfx.get(), resource, mem, xfer,
			                          vm, fn, args, &mixer);
		});
	// With graphics, the host pump (in dispatch_event) presents + yields + handles
	// quit, so the SOP main loop is frame-paced and the whole game session runs
	// inside send(MSG_CREATE) until the user quits -- the instruction budget would
	// fight that, so drop it. Headless keeps the budget as a runaway guard.
	objects.setMaxSteps(gfx ? 0 : 2000000);
	auto _tr = std::chrono::steady_clock::now();
	registerClasses(resource, objects);
	if (std::getenv("THIRDEYE_TIMING")) {
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		              std::chrono::steady_clock::now() - _tr).count();
		std::cout << "[timing] registerClasses (" << resource.getCodeResourceNames().size()
		          << " classes): " << ms << " ms" << std::endl;
	}

	uint16_t classNumber = 0;
	if (!objects.findClassByName(objectName, classNumber)) {
		std::cout << "Boot object \"" << objectName
		          << "\" not found in this resource file." << std::endl;
		return;
	}

	std::cout << "\nBooting object \"" << objectName
	          << "\" (sending MSG_CREATE)..." << std::endl;
	// Program-chain loop: start.MSG_CREATE runs the whole session and normally
	// returns only at quit (Abandon, or window-close via QuitRequested). A menu
	// choice that hands off to a sub-program (Introduction->cine, Gather->chgen)
	// unwinds here as Relaunch; we play our internal stand-in for that program and
	// re-enter start, which re-reads the mode the bytecode left in cell 1264. This
	// mirrors the DOS program chain (each program ends by launching the next).
	// Side-effect-free read for the sparse mem map. operator[] would silently
	// default-insert if the key were missing (we seed cell 1264 above, but
	// the SOP can also poke arbitrary cells); .find() makes the absence
	// explicit and falls back to INTR (the safe boot mode).
	auto readMode = [&]() -> int32_t {
		auto it = mem.find(1264);
		return it != mem.end() ? it->second : MODE_INTR;
	};
	bool quit = false;
	while (!quit) {
		const int32_t mode = readMode();
		// Text-box clear style depends on the screen we're booting into: the title
		// menu (INTR) bakes its options into the backdrop bitmap, so its text boxes
		// flat-fill to erase them; the in-game HUD draws text over detailed panel
		// art, so its boxes restore the backdrop snapshot (true overlay, no blob).
		if (gfx)
			gfx->setTextRestoreBackground(mode != MODE_INTR);
		// Pin `start` to obj 0 every iteration. The SOP's self-destroy
		// convention (CANCEL on the picker, back-out from any sub-menu)
		// hardcodes `destroy_object(0)` -- if we let createInstance append
		// at the next free slot, the second relaunch's start lands at some
		// high index and the SOP's destroy_object(0) hits the OLD slot,
		// leaving the new start "alive" -> our self-destroy check returns
		// false -> we quit instead of relaunching. allocAt(0, ...) replaces
		// whatever stale tombstone is at slot 0 each time.
		int objIndex = objects.createInstanceAt(0, classNumber);
		std::string relaunch; // non-empty => a sub-program to run, then re-boot
		try {
			VM::Value result = objects.send(objIndex, MSG_CREATE, {});
			// AESOP self-destroy convention: when start cancels back to itself
			// (e.g. CANCEL on the Restore-Game picker, or back-out from any
			// sub-menu), the SOP calls destroy_object(0) on its own handler.
			// The original interpreter's top-level loop re-creates start at
			// that point with whichever mode the bytecode left in cell 1264.
			// If start is still alive, it returned normally (Abandon the
			// Quest -> clean exit).
			bool selfDestroyed = objects.objectLookup(objIndex) != objIndex;
			if (selfDestroyed) {
				// Re-read mode: the SOP may have poked cell 1264 between boot
				// and self-destroy (e.g. picker -> menu sets it back to INTR).
				const int32_t nextMode = readMode();
				// Simulate the original AESOP `launch()` exec-replace by
				// resetting per-process state: every live object + every live
				// subwindow + the event queue. The SOP authors didn't bother
				// releasing the ~80 sub-windows / handful of helper objects
				// each menu iteration creates because the original DOS runtime
				// reaped the whole process on launch(). Without these resets,
				// 3-4 cancel cycles exhaust the 256-handle window table and
				// mouse hit-testing dies. The class registry + the runtime
				// hooks (dynamic-statics, etc.) persist; only instance state
				// is wiped.
				std::cout << "  [start self-destroyed -- "
				          << objects.liveObjectCount() << " obj / "
				          << events.liveWindowCount() << " win leaked; "
				             "resetting + relaunching with mode "
				          << (nextMode == MODE_CINE ? "CINE"
				            : nextMode == MODE_CHGN ? "CHGN" : "INTR")
				          << "]" << std::endl;
				objects.resetInstances();
				events.reset();
				continue; // loop to re-create start in the cleared environment
			}
			std::cout << "Boot handler returned " << result << " -- quitting."
			          << std::endl;
			quit = true; // start returned normally (e.g. "Abandon the Quest")
		} catch (const QuitRequested &q) {
			std::cout << "\n" << (q.reason.empty() ? "Window closed" : q.reason)
			          << " -- quitting." << std::endl;
			quit = true;
		} catch (const Relaunch &r) {
			relaunch = r.program.empty() ? " " : r.program; // mark for handling
		} catch (const VM::VmError &e) {
			handleBootWall(e, gfx.get());
			quit = true;
		}
		// Run the sub-program OUTSIDE the catch above (a throw from within a catch
		// handler escapes its own try): it may itself QuitRequested (window closed
		// during the intro), which must be caught here. Then loop to re-boot start.
		// Returns false on cancel (Esc out of chargen) -- override mem[1264] so
		// the next start lands on the title menu rather than the chargen-transfer.
		if (!relaunch.empty()) {
			try {
				if (!runExternalProgram(relaunch, gfx.get(), resource)) {
					mem.insert_or_assign(1264, MODE_INTR);
					std::cout << "  [sub-program cancelled -- next start: INTR]"
					          << std::endl;
				}
			} catch (const QuitRequested &q) {
				std::cout << "\n" << (q.reason.empty() ? "Window closed" : q.reason)
				          << " -- quitting." << std::endl;
				quit = true;
			}
		}
	}

	if (gfx)
		gfx->saveScreenshot("/tmp/thirdeye_boot.bmp"); // last frame, for inspection

	// THIRDEYE_RECORD: emit the session's input as a replayable AUTOWALK line.
	// This is the common exit seam -- every quit path (menu quit, window close,
	// boot-wall) falls through here.
	if (gRecorder.enabled)
		gRecorder.dump();
}

// A menu choice handed off to an external DOS sub-program via launch(). We can't
// run those binaries; instead we play thirdeye's own equivalent. The chain
// (re-boot start on the poked mode) is what makes the selection do the right
// thing; here we drive the rich behaviour for the programs we can.
bool THIRDEYE::Engine::runExternalProgram(const std::string &program,
                                          GRAPHICS::Graphics *gfx,
                                          RESOURCES::Resource &resource) {
	std::string p = program;
	std::transform(p.begin(), p.end(), p.begin(),
	               [](unsigned char c) { return std::tolower(c); });
	if (p.find("cine") != std::string::npos) {
		// "Introduction": the original launches CINE.EXE to play INTRO.GFF, then
		// chain-launches back to the title menu. Drive thirdeye's own GFF player.
		playCinematic(gfx, resource, "INTRO.GFF");
		return true;
	}
	if (p.find("chgen") != std::string::npos ||
	    p.find("chargen") != std::string::npos ||
	    p.find("charge") != std::string::npos) {
		// "Gather a New Party": CHGEN.EXE builds a party + writes CREATE.SAV,
		// then chains back with mode "CHGN" so start's xfer reads it.
		std::filesystem::path chargenDir = resolveChildCI(
		    resource.resourcePath().parent_path(), "CHARGEN");
		std::cout << "  [program chain: \"" << program
		          << "\" -> entering chargen screen (dir: " << chargenDir
		          << ", gfx=" << (gfx ? "yes" : "no") << ")]" << std::endl;
		if (gfx) {
			bool ok = THIRDEYE::chargen::runChargenScreen(*gfx, chargenDir);
			if (ok) {
				// Paint the game's main stone Backdrop over the chargen's last
				// frame before start.create's CHGN path begins drawing. The
				// SOP draws specific UI elements (portraits, compass, dialog
				// text) on top of whatever's already there and never issues
				// a full-screen clear, so a leftover chargen frame leaks
				// through the gaps. The CINE path doesn't see this because
				// the title menu had already drawn the stone Backdrop into
				// those areas. fillRect-to-black "fixes" the leak but leaves
				// the gaps reading as black voids (visible under the dialog
				// text panel); drawing the actual Backdrop matches CINE.
				try {
					auto &bmp = resource.getAsset("Backdrop");
					gfx->drawImage(bmp, 0, 0, 0, /*transparency=*/false,
					               /*mirror=*/0, /*cacheId=*/0);
				} catch (const std::exception &) {
					// Backdrop missing -- fall back to a black wipe rather
					// than leak the chargen frame.
					gfx->fillRect(0, 0, WIDTH - 1, HEIGHT - 1, 0);
				}
				// New party means a new game: wipe SAVEGAME/*.TMP so the CHGN
				// boot doesn't read stale kernel state (party position, level
				// progress) from a previous session. Otherwise the kernel
				// hydrates from the old ITEMS.TMP and the fresh party lands
				// on whatever level/cell the prior save was at.
				auto saveDir = resolveChildCI(
				    resource.resourcePath().parent_path(), "SAVEGAME");
				std::error_code ec;
				if (std::filesystem::is_directory(saveDir, ec)) {
					int wiped = 0;
					for (auto &entry : std::filesystem::directory_iterator(saveDir, ec)) {
						auto ext = entry.path().extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(),
						               [](unsigned char c){ return std::tolower(c); });
						if (ext == ".tmp") {
							std::filesystem::remove(entry.path(), ec);
							if (!ec) ++wiped;
						}
					}
					std::cout << "  [chargen success: wiped " << wiped
					          << " stale *.TMP from " << saveDir << "]"
					          << std::endl;
				}
			}
			return ok;
		}
		std::cout << "  [program chain: \"" << program
		          << "\" -> headless; using the existing CREATE.SAV]"
		          << std::endl;
		return true;
	}
	// DOS `launch()` (arun/src/EYE.C:1169) always terminates the current
	// process via exit(127); a loader outside AESOP execs the next program.
	// An unknown target name matches "exec failed" -- terminal, not a silent
	// re-boot. SAMPLE.RES's `launch("xxx.exe")` is a demo exit sentinel that
	// used to spin us here forever.
	std::cout << "  [program chain: \"" << program
	          << "\" -> unknown target; quitting session]"
	          << std::endl;
	throw QuitRequested{"unknown launch target \"" + program + "\""};
}

// Play a GFF cinematic (INTRO.GFF/FINALE.GFF/...) that lives beside the game's
// .RES, reusing thirdeye's existing GFF player. Returns when the cinematic ends,
// is skipped (ESC/Enter), or --skip-intro is set; closing the window throws
// QuitRequested to unwind the whole session. The boot then re-enters start, which
// draws the menu over the final frame.
void THIRDEYE::Engine::playCinematic(GRAPHICS::Graphics *gfx,
                                     RESOURCES::Resource &resource,
                                     const std::string &gffName) {
	if (mSkipIntro) {
		std::cout << "  [program chain: --skip-intro set; skipping " << gffName
		          << "]" << std::endl;
		return;
	}
	std::filesystem::path gffPath =
		resource.resourcePath().parent_path() / gffName;
	if (!gfx || !std::filesystem::exists(gffPath)) {
		std::cout << "  [program chain: " << gffName
		          << (gfx ? " not found" : " -- headless")
		          << "; skipping cinematic]" << std::endl;
		return;
	}

	std::cout << "  [program chain: playing " << gffName
	          << " (ESC/Enter to skip)]" << std::endl;
	// GFFI load / music decode / video setup can throw; a failure here must not
	// abort the boot (we're on the relaunch path), so skip the cinematic instead.
	// QuitRequested (below) is not a std::exception, so window-close still unwinds.
	try {
		MIXER::Mixer mixer;
		RESOURCES::GFFI video(std::move(gffPath));
		mixer.playMusic(video.getMusic());
		gfx->playVideo(video.getSequence());

		SDL_Event event;
		while (gfx->isVideoPlaying()) {
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_EVENT_QUIT) {
					gfx->stopVideo();
					mixer.stopMusic();
					throw QuitRequested{};
				}
				if (event.type == SDL_EVENT_KEY_DOWN &&
				    (event.key.key == SDLK_ESCAPE ||
				     event.key.key == SDLK_RETURN)) {
					gfx->stopVideo();
					mixer.stopMusic();
				}
			}
			gfx->update();
			mixer.update();
			uint32_t sleep = gfx->getSleep();
			SDL_Delay(sleep > 0 ? sleep : 16);
		}
	} catch (const std::exception &e) {
		gfx->stopVideo();
		std::cout << "  [program chain: failed to play " << gffName << " ("
		          << e.what() << "); skipping cinematic]" << std::endl;
	}
}

void THIRDEYE::Engine::handleBootWall(const VM::VmError &e,
                                      GRAPHICS::Graphics *gfx) {
	std::cout << "\nBoot stopped at the first unimplemented piece:\n  "
	          << e.what() << std::endl;
	// Bring-up wall: hold the partially-rendered frame on screen so it's
	// visible (the live host pump never got a chance to run the main loop).
	if (gfx) {
		gfx->update();
		std::cout << "\nRendered frame is up -- press ESC or close the window "
		             "to quit." << std::endl;
		bool done = false;
		SDL_Event event;
		while (!done) {
			while (SDL_PollEvent(&event))
				if (event.type == SDL_EVENT_QUIT ||
				    (event.type == SDL_EVENT_KEY_DOWN &&
				     event.key.key == SDLK_ESCAPE))
					done = true;
			gfx->update();
			SDL_Delay(16);
		}
	}
}

// Initialise and enter main loop.
void THIRDEYE::Engine::go() {
	gBootStart = std::chrono::steady_clock::now();
	std::filesystem::path resFile = resolveResourceFile();

	if (!std::filesystem::exists(resFile)) {
		throw std::runtime_error(resFile.string() + " does not exist!");
	}

	auto _t0 = std::chrono::steady_clock::now();
	RESOURCES::Resource resource(resFile);	// get our game resources ready
	if (std::getenv("THIRDEYE_TIMING")) {
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		              std::chrono::steady_clock::now() - _t0).count();
		std::cout << "[timing] resource load: " << ms << " ms" << std::endl;
	}

	// --chargen-test: render the chargen entry screen in isolation (no SOP, no
	// menu, no transfer). Used to verify the chargen UI / portrait decoder
	// without driving the title menu, since headless autowalk doesn't currently
	// reach the menu's mouse-region handler.
	if (mChargenTest) {
		GRAPHICS::Graphics gfx(mScale);
		std::filesystem::path chargenDir =
		    resolveChildCI(resFile.parent_path(), "CHARGEN");
		try {
			THIRDEYE::chargen::runChargenScreen(gfx, chargenDir);
		} catch (const THIRDEYE::runtime::QuitRequested &) {
			std::cout << "\nWindow closed -- quitting." << std::endl;
		}
		return;
	}

	// --vm (or the --skip-* flags, which only make sense here): boot the SOP
	// bytecode VM (the 'start' object). This is the real, data-driven game path:
	// start.MSG_CREATE shows the title menu (or, with --skip-menu, the game).
	if (mForceVM || mSkipMenu || mSkipIntro) {
		bootObject(resource, "start");
		return;
	}

	// The intro/menu flow below is EOB3-specific and needs the cinematic that
	// ships beside the game's .RES. If we were handed some other resource file
	// (e.g. SAMPLE.RES) or a real game install isn't present, just report what
	// we loaded and stop -- no need to spin up graphics/sound. The SOP VM is
	// not yet wired into the main loop.
	std::filesystem::path introPath = resFile.parent_path() / "INTRO.GFF";
	if (mGame == GAME_UNKN || !std::filesystem::exists(introPath)) {
		std::cout << "Loaded resource file: " << resFile << std::endl;
		resource.showResources();
		runResourceVM(resource);
		return;
	}

	// A real game install, no flags: the full game. The SOP boot plays the
	// intro via the program chain (CINE mode / auto-detect), then runs the
	// interactive title menu -- everything the old hand-coded intro/menu demo
	// loop that lived here faked, minus the dead ends (its faded-in title
	// image had no SOP behind it, so nothing was selectable). Museum code
	// removed; git history has it.
	bootObject(resource, "start");
}

