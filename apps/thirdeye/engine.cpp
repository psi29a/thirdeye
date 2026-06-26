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
#include "chargen/chargen_screen.hpp"
#include "runtime/internal.hpp"

#include <components/files/configurationmanager.hpp>

#include <algorithm>
#include <cctype>
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
		std::string cl = candidate;
		for (auto &c : cl) c = static_cast<char>(std::tolower(
		    static_cast<unsigned char>(c)));
		if (cl == lower) return entry.path();
	}
	return exact;
}

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
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			throw QuitRequested{};
		case SDL_KEYDOWN: {
			// The SOP code expects DOS/BIOS key codes: printable keys are ASCII
			// (SDL keysyms already are, incl. ESC=0x1b/Enter=0x0d), but the arrow
			// keys are (scancode << 8) with a zero ASCII byte. SDL's own arrow
			// keysyms (0x4000_00xx) match nothing, so translate them. ESC is *not*
			// special-cased to quit: the bytecode handles it (in-game it opens the
			// camp menu via notify msg 272; in the title menu it's "Abandon the
			// Quest"). Forced quit is the window close button (SDL_QUIT above).
			//
			// WASD/QE/C use the PHYSICAL key position (SDL_SCANCODE_*), so they work
			// regardless of layout (QWERTY/AZERTY/QWERTZ): W/S = fwd/back, A/D = strafe,
			// Q/E = turn, C = camp. They post the same DOS codes the arrow keys do (the
			// kernel notifies move forward/back/strafe/turn on those). (No I->inventory:
			// it defaulted to Bob with no indication of whose pack it opened.)
			int32_t key = 0;
			switch (ev.key.keysym.scancode) {
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
				SDL_Keycode k = ev.key.keysym.sym;
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
			if (key != 0)
				events.postEvent(0, VM::SYS_KEYDOWN, key);
			break;
		}
		case SDL_MOUSEMOTION: {
			int lx, ly;
			gfx.mouseToLogical(ev.motion.x, ev.motion.y, lx, ly);
			events.mouseMove(lx, ly);
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			// Keep the mouse position current, then raise the button edge.
			int lx, ly;
			gfx.mouseToLogical(ev.button.x, ev.button.y, lx, ly);
			events.mouseMove(lx, ly);
			uint32_t b = SDL_GetMouseState(nullptr, nullptr);
			events.mouseButton(b & SDL_BUTTON(SDL_BUTTON_LEFT),
			                   b & SDL_BUTTON(SDL_BUTTON_RIGHT));
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

	// Debug: THIRDEYE_AUTOWALK posts scripted SYS_KEYDOWNs (the same the arrow
	// keys/WASD/QE post) every ~40 pumps, so a headless run can drive the engine
	// for repro/regression testing. Value is one OR MORE comma-separated hex
	// scan codes: "4800" = repeat forward; "4900,4800,4800" = turn-right once
	// then walk forward x2 then hold (last code repeats). Useful sequences:
	//   4800=forward 5000=back 4b00=strafe-L 4d00=strafe-R 4700=turn-L 4900=turn-R
	//   5000,0d,0d = menu Down + Enter + Enter (advances title menu + save picker)
	//   0d = Enter   1b = Esc
	if (const char *aw = std::getenv("THIRDEYE_AUTOWALK")) {
		// Cache the parsed sequence in statics: AUTOWALK is a process-launch
		// env var (set once before exec), so re-parsing every event pump
		// (~30 Hz) is wasted work. Callers wanting a different script
		// restart the process; we deliberately don't honor setenv at runtime.
		static std::vector<int32_t> codes;
		static int tick = 0;
		if (codes.empty()) {
			std::string s = aw;
			for (size_t p = 0; p < s.size(); ) {
				size_t e = s.find(',', p);
				int32_t c = static_cast<int32_t>(std::strtol(
					s.substr(p, e == std::string::npos ? e : e - p).c_str(), nullptr, 16));
				if (c != 0) codes.push_back(c);
				if (e == std::string::npos) break;
				p = e + 1;
			}
			if (codes.empty()) codes.push_back(0x4800);
		}
		if (++tick % 40 == 0) {
			size_t idx = static_cast<size_t>(tick / 40 - 1);
			if (idx >= codes.size()) idx = codes.size() - 1;
			events.postEvent(0, VM::SYS_KEYDOWN, codes[idx]);
		}
	}
	// Debug: THIRDEYE_AUTOKEY=<decimal SDL scancode> pushes a synthetic SDL_KEYDOWN
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
			ke.type = SDL_KEYDOWN;
			ke.key.keysym.scancode = static_cast<SDL_Scancode>(std::atoi(ak));
			ke.key.keysym.sym = SDLK_UNKNOWN;
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
                             const std::vector<VM::Value> &args) {
	THIRDEYE::runtime::Context ctx{objects, events, gfx, res, mem, xfer, vm};
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

	THIRDEYE::runtime::rt() << "  [stub -> 0]" << std::endl;
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
	events.setVerbose(mDebug || std::getenv("THIRDEYE_AUTOWALK"));
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

	VM::ObjectSystem objects;
	VM::EventSystem events(objects);
	events.setVerbose(mDebug || std::getenv("THIRDEYE_AUTOWALK"));
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
	int32_t bootMode = MODE_INTR;
	if (mChargen)       bootMode = MODE_CHGN;
	else if (mSkipMenu) bootMode = saveExists() ? MODE_CINE : MODE_CHGN;
	mem.insert_or_assign(1264, bootMode);
	std::cout << "  [boot mode = "
	          << (bootMode == MODE_CINE ? "CINE (continue from save)"
	            : bootMode == MODE_CHGN ? "CHGN (chargen + game)"
	            : "INTR (title menu)")
	          << "]" << std::endl;
	TransferState xfer; // char-gen party transfer file (CHARGEN\CREATE.SAV)

	objects.setRuntimeCall(
		[&](VM::Interpreter &vm, const std::string &fn,
		           const std::vector<VM::Value> &args) {
			return defaultRuntimeCall(objects, events, gfx.get(), resource, mem, xfer,
			                          vm, fn, args);
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
		} catch (const QuitRequested &) {
			std::cout << "\nWindow closed -- quitting." << std::endl;
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
			} catch (const QuitRequested &) {
				std::cout << "\nWindow closed -- quitting." << std::endl;
				quit = true;
			}
		}
	}

	if (gfx)
		gfx->saveScreenshot("/tmp/thirdeye_boot.bmp"); // last frame, for inspection
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
	std::cout << "  [program chain: \"" << program
	          << "\" -> no thirdeye equivalent yet; re-booting start]"
	          << std::endl;
	return true;
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
		RESOURCES::GFFI video(gffPath);
		mixer.playMusic(video.getMusic());
		gfx->playVideo(video.getSequence());

		SDL_Event event;
		while (gfx->isVideoPlaying()) {
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_QUIT) {
					gfx->stopVideo();
					mixer.stopMusic();
					throw QuitRequested{};
				}
				if (event.type == SDL_KEYDOWN &&
				    (event.key.keysym.sym == SDLK_ESCAPE ||
				     event.key.keysym.sym == SDLK_RETURN)) {
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
				if (event.type == SDL_QUIT ||
				    (event.type == SDL_KEYDOWN &&
				     event.key.keysym.sym == SDLK_ESCAPE))
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

	MIXER::Mixer mixer;		// setup our sound mixer
	GRAPHICS::Graphics gfx(mScale); // setup our graphics

	/*
	 Settings::Manager settings;
	 std::string settingspath;

	 settingspath = loadSettings (settings);
	 */

	std::vector<uint8_t> &snd = resource.getAsset("BIRD4");
	std::vector<uint8_t> &xmidi = resource.getAsset("CUE1");

	//std::vector<uint8_t> &font = resource.getAsset("8x8 font");
	//std::vector<uint8_t> &font2 = resource.getAsset("6x8 font");
	//std::vector<uint8_t> &font3 = resource.getAsset("Ornate font");

	//std::vector<uint8_t> &bmp = resource.getAsset("Backdrop");
	//std::vector<uint8_t> &bmp = resource.getAsset("Menu shapes");
	std::vector<uint8_t> &icons = resource.getAsset("Icons");
	//std::vector<uint8_t> &marble = resource.getAsset("Marble walls");
	//std::vector<uint8_t> &basePalette = resource.getAsset("Fixed palette");
	std::vector<uint8_t> &basePalette = resource.getAsset("Title palette");
	//std::vector<uint8_t> &subPalette = resource.getAsset("Marble palette");
	std::string text = resource.getTableEntry("Marble palette", 1);

	gfx.loadPalette(basePalette);
	gfx.loadMouse(icons, 0);

	//gfx.drawImage(bmp, 0, 0, 0, true);


	 /*
	 gfx.loadPalette(basePalette, subPalette, text);
	 gfx.drawImage(marble, 18, 0, 0, true);
	 gfx.drawImage(marble, 0, 0, 0, true);
	 gfx.drawImage(marble, 1, 24, 8, true);
	 gfx.drawImage(marble, 2, 48, 20, true);
	 gfx.drawImage(marble, 3, 64, 28, true);

	 gfx.drawImage(icons, 1, 25, 120, true);
	 gfx.drawText(font,"Welcome to Thirdeye!", 8, 181);
	 */

	uint32_t clock = 0;	//  wall clock in ms resolution
	uint32_t currentSecond = 0;	// our wall clock with 1s resolution
	uint32_t fps = 0;	// number of fps (iterations of main loop)

	// get our intro cinematic, set state and play
	RESOURCES::GFFI introVideo(introPath);
	mixer.playMusic(introVideo.getMusic());
	gfx.playVideo(introVideo.getSequence());
	uint8_t state = STATE_INTRO;

	// Start the main rendering loop
	SDL_Event event;
	bool done = false;
	bool isSecond = false;
	while (!done)  // Enter main loop.
	{
		// get our clock
		clock = SDL_GetTicks();
		fps++;
		if (clock / 1000 > currentSecond) {
			currentSecond = clock / 1000;
			//update = true;
			isSecond = true;
		}

		// what state are we in
		switch (state) {
			case STATE_INTRO: // change state to menu when finished playing intro
				if ( !gfx.isVideoPlaying() ){
					state = STATE_MENU;
					std::vector<uint8_t> &menuBMP = resource.getAsset("Menu shapes");
					std::vector<uint8_t> &menuPAL = resource.getAsset("Title palette");
					gfx.loadPalette(menuPAL);
					gfx.zoomIntoImage(menuBMP);
				}

		}

		// poll our inputs
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			// this is the window x being clicked.
			case SDL_QUIT:
				done = true;
				break;
				// process the mouse data by passing it to ngl class
			case SDL_MOUSEMOTION:
				//ngl.mouseMoveEvent(event.motion);
				//std::cout << "Mouse moved @ " <<  event.motion.x << " " << event.motion.y << std::endl;
				break;
			case SDL_MOUSEBUTTONDOWN:
				break;
			case SDL_MOUSEBUTTONUP:
				//std::cout << "Mouse clicked @ " << event.button.x << " " << event.button.y << std::endl;
				break;
				//case SDL_MOUSEWHEEL : ngl.wheelEvent(event.wheel);
				// if the window is re-sized pass it to the ngl class to change gl viewport
				// note this is slow as the context is re-create by SDL each time
			case SDL_WINDOWEVENT:
				//int w, h;
				// get the new window size
				//SDL_GetWindowSize(window, &w, &h);
				//ngl.resize(w,h);
				break;

				// now we look for a keydown event
			case SDL_KEYDOWN: {
				switch (event.key.keysym.sym) {
				// if it's the escape key quit
				case SDLK_ESCAPE:
					if ( state == STATE_INTRO ){
						gfx.stopVideo();
						mixer.stopMusic();
					} else
						done = true;
					break;
				case SDLK_w:
					mixer.playSound(snd);
					break;
				case SDLK_a:
					break;
				case SDLK_s:
					mixer.playMusic(xmidi);
					break;
				case SDLK_d:
					break;
				case SDLK_f:
					//SDL_SetWindowFullscreen(window, SDL_TRUE);
					break;
				case SDLK_g:
					//SDL_SetWindowFullscreen(window, SDL_FALSE);
					break;
				default:
					break;
				} // end of key process
			}
				break; // end of keydown

			default:
				break;
			} // end of event switch
		} // end pool loop


		gfx.update();		// update our screen
		mixer.update();		// update our sounds

		if (isSecond) {
			std::cout << "Wall Clock: " << std::dec << currentSecond << " average " << fps << "fps"
					//<< " sleeping for: " << (int) gfx.getSleep() << "ms "
					<< "\r" << std::flush;
			isSecond = false;
			fps = 0;
		}

		uint32_t sleep = gfx.getSleep();
		if ( sleep > 0 )
			SDL_Delay(gfx.getSleep());
		else {
			SDL_Delay(fps/5);
		}
	}

	// Save user settings
	//settings.saveUser(settingspath);

	std::cout << "Quitting peacefully." << std::endl;
}

