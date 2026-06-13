#include "engine.hpp"
#include "resources/res.hpp"
#include "resources/gffi.hpp"
#include "sound/sound.hpp"
#include "graphics/graphics.hpp"
#include "vm/vm.hpp"
#include "vm/objects.hpp"
#include "vm/events.hpp"

#include <components/files/configurationmanager.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

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

// first_color[region]: where each palette region starts in the 256-colour DAC
// (GRAPHICS.C). PAL_FIXED=0x00, PAL_WALLS=0xB0, PAL_M1=0xC0, PAL_M2=0xE0, PAL_OUT=0xB0.
constexpr uint16_t kFirstColor[5] = {0x00, 0xB0, 0xC0, 0xE0, 0xB0};

// Thrown by the host pump to unwind out of the SOP main loop when the user
// closes the window / presses ESC. Caught in bootObject for a clean exit.
struct QuitRequested {};

// Thrown by launch() to model AESOP's program chain. In the original, launch()
// exec-replaces the process with a sub-program (cine.exe/chgen.exe); when that
// finishes it chain-launches "aesop eye start" again, which re-reads the mode
// the bytecode just pokemem'd into cell 1264 and routes on it. We can't exec, so
// launch() unwinds the VM back to bootObject, which runs our internal equivalent
// of the named program and then re-enters start.MSG_CREATE -- same effect.
struct Relaunch {
	std::string program;
};

// State for the char-gen party transfer (the `xfer` object's "convert created
// party"/"transfer" handlers). open_transfer_file buffers CHARGEN\CREATE.SAV (the
// party CHGEN.EXE wrote); player_attrib/item_attrib read fields back out of it.
//
// CREATE.SAV layout (reverse-engineered from the default party Bob/Carol/Ted/
// Alice): a short header, then four fixed 345-byte PC records starting at 0x16.
// player_attrib(pc, attr, size) reads `size` little-endian bytes at file offset
// kPcBase + pc*kPcStride + attr (attr is the byte offset within the record's
// attribute area, which begins just past the 11-byte name -- so e.g. attr 2 is
// the first ability score). The bytecode just copies these bytes into the PC
// object's statics, so returning the right bytes reconstructs the real party.
struct TransferState {
	std::vector<uint8_t> data; // CREATE.SAV contents (empty => not open)
	// player_attrib(pc, attr, size) reads `size` LE bytes at file offset
	// kPcBase + pc*kPcStride + attr. The record starts at 0x16 and `attr` is a
	// direct byte offset into it biased by +2 (attr 2 = the first byte = name[0]):
	// the name-copy loop reads attr 2..12 -> name[0..10] ("Bob\0..."), confirming
	// the bias. So kPcBase = 0x16 - 2 = 20.
	static constexpr int kPcBase = 0x16 - 2;
	static constexpr int kPcStride = 345;

	// Read `size` (1/2/4) little-endian bytes for player `pc`, attribute `attr`.
	int32_t playerAttrib(int pc, int attr, int size) const {
		size_t off = static_cast<size_t>(kPcBase + pc * kPcStride + attr);
		uint32_t v = 0;
		for (int i = 0; i < size; ++i) {
			if (off + i >= data.size()) break;
			v |= static_cast<uint32_t>(data[off + i]) << (8 * i);
		}
		return static_cast<int32_t>(v);
	}

	// --- inventory / starting gear (CREATE.SAV item array) ---
	// A PC's inventory is a row of 26 word slots at record offset 219 holding item
	// ids (0 = empty). The party's items live in an EOB1-format item array near the
	// end of CREATE.SAV: 14-byte records (unid,id,bits,pic,type,subpos,pos,next,
	// prev,level,value), the char-gen's ids running from 434 (= ITEM.DAT's item
	// count) at file 0x894. item_attrib(pc, slot, attr) resolves slot->id->record:
	// attr 1 = type (+4, matched against the xfer's table123 type->object map),
	// attr 0 = bits/flags (+2, -> itmflags), attr 2 = value (+13, signed -> bonus).
	static constexpr int kInvRecOff = 219;     // PC inventory slots (words)
	static constexpr int kInvSlots = 26;
	static constexpr int kItemArrayBase = 0x894; // party item records in CREATE.SAV
	static constexpr int kItemIdBase = 434;       // first party item id (ITEM.DAT count)
	static constexpr int kItemRecSize = 14;

	int32_t itemAttrib(int pc, int slot, int attr) const {
		size_t slotOff = static_cast<size_t>(kPcBase + 2 + pc * kPcStride +
		                                     kInvRecOff + slot * 2);
		if (slot < 0 || slot >= kInvSlots || slotOff + 1 >= data.size())
			return attr == 1 ? -1 : 0;
		int id = data[slotOff] | (data[slotOff + 1] << 8);
		if (id < kItemIdBase) // 0 = empty slot (party items all start at 434)
			return attr == 1 ? -1 : 0;
		size_t rec = static_cast<size_t>(kItemArrayBase +
		                                 (id - kItemIdBase) * kItemRecSize);
		auto rb = [&](int off) -> int {
			return rec + off < data.size() ? data[rec + off] : 0;
		};
		switch (attr) {
		case 0: return rb(2);                                 // bits  -> itmflags
		case 1: return rb(4);                                 // type  (table123 key)
		case 2: return static_cast<int8_t>(rb(13));           // value -> bonus
		default: return 0;
		}
	}
};

// The host seam (see the design note in CLAUDE.md). The kernel's main loop is a
// busy-wait -- `while (!quit) dispatch_event();` -- so each time the bytecode
// polls dispatch_event/peek_event we do the work DOS did with interrupts +
// vblank: pump SDL input into AESOP events, present the frame, and yield the CPU
// when the queue is idle. That turns the 100% spin into an event-driven,
// frame-paced loop and makes the window render live (instead of only after the
// loop ends). Throws QuitRequested on window-close / ESC.
void pumpHost(GRAPHICS::Graphics &gfx, VM::EventSystem &events) {
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
			SDL_Keycode k = ev.key.keysym.sym;
			int32_t key;
			switch (k) {
			case SDLK_UP:    key = 0x4800; break;
			case SDLK_DOWN:  key = 0x5000; break;
			case SDLK_LEFT:  key = 0x4b00; break;
			case SDLK_RIGHT: key = 0x4d00; break;
			default:
				key = (k > 0 && k < 0x80) ? k : 0; // ASCII (ESC=0x1b, Enter=0x0d, ...)
				break;
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

	gfx.update();                 // present whatever the bytecode has drawn
	if (!events.peekEvent())
		SDL_Delay(10);            // idle: yield (~100 Hz) instead of spinning
}

// Runtime-function library. Most functions are still stubs (log + return 0),
// but where we can do the real thing, we do. Each call is logged with its
// arguments, resolving address arguments to inline strings when possible.
// `gfx` is null when running headless (no display); the graphics functions then
// printf-style substitution shared by print()/sprint(): fill %d/%u/%i, %s and
// %% in `fmt` from `args[start..]`. %s args are addresses (read via the VM).
std::string formatSop(const std::string &fmt, const std::vector<VM::Value> &args,
                      size_t start, VM::Interpreter &vm) {
	std::string out;
	size_t ai = start;
	for (size_t i = 0; i < fmt.size(); ++i) {
		if (fmt[i] != '%' || i + 1 >= fmt.size()) { out += fmt[i]; continue; }
		char conv = fmt[++i];
		if (conv == '%') { out += '%'; continue; }
		VM::Value a = ai < args.size() ? args[ai++] : 0;
		if (conv == 'd' || conv == 'u' || conv == 'i')
			out += std::to_string(a);
		else if (conv == 's')
			out += vm.readString(a);
		else { out += '%'; out += conv; }
	}
	return out;
}

// fall through to the stub.
VM::Value defaultRuntimeCall(VM::ObjectSystem &objects, VM::EventSystem &events,
                             GRAPHICS::Graphics *gfx, RESOURCES::Resource &res,
                             std::map<int32_t, int32_t> &mem, TransferState &xfer,
                             VM::Interpreter &vm, const std::string &fn,
                             const std::vector<VM::Value> &args) {
	// peekmem/pokemem model the original's raw memory cells. The boot object's
	// MSG_CREATE reads peekmem(1264) -- a 4-char "mode" -- and CASEs on it to
	// decide what to do (INTR -> title menu, CINE -> straight to game, ...). We
	// back them with a real map so that the boot state machine works and we can
	// seed the mode (see bootObject). Quiet: called in tight spots.
	if (fn == "peekmem" && args.size() >= 1) {
		auto it = mem.find(args[0]);
		return it == mem.end() ? 0 : it->second;
	}
	if (fn == "pokemem" && args.size() >= 2) {
		mem[args[0]] = args[1];
		return 0;
	}
	// absv: absolute value (RTCODE.C). The menu colours each option
	// 136 + absv(option - selected), so a stub (->0) flattens the highlight.
	if (fn == "absv" && args.size() >= 1)
		return args[0] < 0 ? -args[0] : args[0];
	// minv/maxv: min/max of two values (RTCODE.C math helpers). Stubbed -> 0 they
	// flatten clamped values (HP/AC/colour computations in the HUD).
	if (fn == "minv" && args.size() >= 2)
		return args[0] < args[1] ? args[0] : args[1];
	if (fn == "maxv" && args.size() >= 2)
		return args[0] > args[1] ? args[0] : args[1];
	// --- event system (EVENT.C) -- real, and intentionally quiet: the kernel's
	// main loop calls these in a tight spin, so logging each would bury the
	// trace. dispatchEvent itself logs the messages it delivers when verbose.
	if (fn == "notify" && args.size() >= 4) {
		events.notify(args[0], static_cast<uint32_t>(args[1]), args[2], args[3]);
		return 0;
	}
	if (fn == "cancel" && args.size() >= 4) {
		events.cancel(args[0], static_cast<uint32_t>(args[1]), args[2], args[3]);
		return 0;
	}
	if (fn == "post_event" && args.size() >= 3) {
		events.postEvent(args[0], args[1], args[2]);
		return 0;
	}
	if (fn == "send_event" && args.size() >= 3) {
		events.sendEvent(args[0], args[1], args[2]);
		return 0;
	}
	if (fn == "dispatch_event") {
		if (gfx) pumpHost(*gfx, events); // present + input + yield (host seam)
		events.dispatchEvent();
		return 0;
	}
	if (fn == "drain_event_queue") { events.drainEventQueue(); return 0; }
	if (fn == "peek_event") {
		if (gfx) pumpHost(*gfx, events);
		return events.peekEvent() ? 1 : 0;
	}
	if (fn == "flush_event_queue" && args.size() >= 3) {
		events.flushEventQueue(args[0], args[1], args[2]);
		return 0;
	}
	if (fn == "flush_input_events") { events.flushInputEvents(); return 0; }

	// --- windowing (GRAPHICS.C assign_subwindow/release_window) ---
	// Backed by the event layer's window table so region (click/hover) events
	// can hit-test against these rectangles. assign_subwindow(owner, parent, x1,
	// y1, x2, y2) returns the handle the SOP code passes to notify(); we ignore
	// `parent` since subwindow coords are absolute (matches GIL2VFX).
	if (fn == "assign_subwindow" && args.size() >= 6)
		return events.assignWindow(args[0], args[2], args[3], args[4], args[5]);
	if (fn == "assign_window" && args.size() >= 5)
		return events.assignWindow(args[0], args[1], args[2], args[3], args[4]);
	if (fn == "release_window" && args.size() >= 1) {
		events.releaseWindow(args[0]);
		return 0;
	}
	// get_x1/get_y1/get_x2/get_y2: a window's rectangle edges.
	if ((fn == "get_x1" || fn == "get_y1" || fn == "get_x2" || fn == "get_y2") &&
	    args.size() >= 1) {
		int32_t x1, y1, x2, y2;
		if (!events.windowRect(args[0], x1, y1, x2, y2))
			return 0;
		if (fn == "get_x1") return x1;
		if (fn == "get_y1") return y1;
		if (fn == "get_x2") return x2;
		return y2; // get_y2
	}

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

	// --- object management (RTOBJECT.C) -- real, backed by the ObjectSystem ---
	if (fn == "create_program" && args.size() >= 2) {
		int index = static_cast<int>(args[0]);
		int rtn = objects.createProgram(index,
		                                static_cast<uint16_t>(args[1]));
		std::cout << "  [-> obj " << rtn << "]" << std::endl;
		return rtn;
	}
	if (fn == "create_object" && args.size() >= 1) {
		int rtn = objects.createObject(static_cast<uint16_t>(args[0]));
		std::cout << "  [-> obj " << rtn << "]" << std::endl;
		return rtn;
	}
	if (fn == "destroy_object" && args.size() >= 1) {
		// RTOBJECT.C destroy_object: MSG_DESTROY, then cancel the object's
		// outstanding notify requests (release_owned_windows is still a stub).
		VM::Value rtn = objects.destroyObject(static_cast<int>(args[0]));
		events.cancelEntityRequests(static_cast<int>(args[0]));
		std::cout << "  [destroyed]" << std::endl;
		return rtn;
	}

	// launch(mode, program, arg1, arg2): hand off to a secondary program. In
	// AESOP this exec-replaces the process (CHARGEN, the CINE intro player, ...);
	// the sub-program chain-launches back into "start", which re-reads cell 1264
	// (the bytecode pokemem'd the next mode just before launching). We model that
	// by unwinding to bootObject, which plays our internal equivalent and re-boots
	// start -- so a menu choice's poke+launch routes to the right next mode
	// instead of falling through to the following CASE branch.
	if (fn == "launch") {
		std::string program;
		for (VM::Value a : args) {
			std::string s = vm.readCodeString(static_cast<uint32_t>(a));
			if (!s.empty()) { program = s; break; }
		}
		throw Relaunch{program};
	}

	// --- char-gen party transfer (EYE.C transfer-file API) ---
	// open_transfer_file(name): buffer the CHGEN.EXE output (CHARGEN\CREATE.SAV)
	// so player_attrib/item_attrib can read the created party out of it. The DOS
	// path uses a backslash and is relative to the game dir; map it to the real
	// sibling file beside the .RES.
	if (fn == "open_transfer_file" && args.size() >= 1) {
		std::string name = vm.readCodeString(static_cast<uint32_t>(args[0]));
		// Map the DOS path (e.g. "CHARGEN\CREATE.SAV") to a real sibling of the
		// .RES: split on '\\' and rebuild with the host separator.
		std::filesystem::path full = res.resourcePath().parent_path();
		std::string part;
		for (size_t i = 0; i <= name.size(); ++i) {
			if (i == name.size() || name[i] == '\\') {
				if (!part.empty()) full /= part;
				part.clear();
			} else part.push_back(name[i]);
		}
		xfer.data.clear();
		std::ifstream in(full, std::ios::binary);
		if (in) {
			xfer.data.assign(std::istreambuf_iterator<char>(in),
			                 std::istreambuf_iterator<char>());
		}
		std::cout << "  [open_transfer_file \"" << name << "\" -> " << full.string()
		          << " (" << xfer.data.size() << " bytes)]" << std::endl;
		return xfer.data.empty() ? -1 : 0;
	}
	if (fn == "close_transfer_file") {
		xfer.data.clear();
		std::cout << "  [close_transfer_file]" << std::endl;
		return 0;
	}
	// player_attrib(pc, attr, size): read `size` bytes for player `pc`'s attribute
	// `attr` from the buffered CREATE.SAV (see TransferState). The xfer bytecode
	// copies these straight into the PC object's statics, rebuilding the party.
	if (fn == "player_attrib" && args.size() >= 3) {
		int32_t v = xfer.playerAttrib(static_cast<int>(args[0]),
		                              static_cast<int>(args[1]),
		                              static_cast<int>(args[2]));
		return v;
	}
	// item_attrib(pc, slot, attr): a created PC's inventory slot -> item field
	// (see TransferState::itemAttrib). attr 1 = type (the transfer maps it to an
	// EOB3 item object via table123), 0 = flags, 2 = bonus.
	if (fn == "item_attrib" && args.size() >= 3)
		return xfer.itemAttrib(static_cast<int>(args[0]), static_cast<int>(args[1]),
		                       static_cast<int>(args[2]));
	// read_initial_items / arrow_count / write_initial_tempfiles: the rest of the
	// transfer. Reading items + writing the game's initial save files is still to
	// do (the savegame format); stubbed so "convert created party" runs through.
	if (fn == "read_initial_items" || fn == "arrow_count")
		return 0;
	if (fn == "write_initial_tempfiles") {
		std::cout << "  [write_initial_tempfiles: stub -- party not persisted yet]"
		          << std::endl;
		return 0;
	}
	// resume_level(level): the real one reconstructs the dungeon level + party from
	// the savegame (.TMP). Until that format is implemented, do the one piece the
	// HUD needs to show the party: register the live PC objects in the kernel's
	// `player[]` array (the list "draw players" walks). Each PC's PC_num is its
	// slot; player[] is the kernel's public static word array (6 slots).
	if (fn == "resume_level") {
		constexpr uint16_t kKernelClass = 1382, kPcClass = 1369;
		constexpr uint16_t kEntitiesClass = 1370;
		constexpr uint32_t kPlayerOff = 229, kPlayerSlots = 6, kPcNumOff = 0;
		// The dungeon "entities" manager is a singleton the draw/move code addresses
		// at the fixed object index 15 (e.g. SXW place@1370 to obj 15). The real
		// resume_level rebuilds it from the level state; create an empty one so the
		// party-draw's extern writes land somewhere live instead of crashing.
		constexpr int kEntitiesIndex = 15;
		if (objects.firstObjectOfClass(kEntitiesClass) < 0)
			objects.createProgram(kEntitiesIndex, kEntitiesClass);
		int kernel = objects.firstObjectOfClass(kKernelClass);
		if (kernel >= 0) {
			auto setSlot = [&](uint32_t slot, int16_t val) {
				if (slot >= kPlayerSlots) return;
				if (uint8_t *p = objects.classStaticPtr(kernel, kKernelClass,
				                                        kPlayerOff + slot * 2, 2)) {
					p[0] = val & 0xFF;
					p[1] = (val >> 8) & 0xFF;
				}
			};
			for (uint32_t s = 0; s < kPlayerSlots; ++s)
				setSlot(s, -1); // empty all slots first
			int placed = 0;
			for (int pc : objects.objectsOfClass(kPcClass)) {
				uint8_t *num = objects.classStaticPtr(pc, kPcClass, kPcNumOff, 1);
				int slot = num ? *num : -1;
				if (slot >= 0 && static_cast<uint32_t>(slot) < kPlayerSlots) {
					setSlot(static_cast<uint32_t>(slot), static_cast<int16_t>(pc));
					++placed;
				}
			}
			std::cout << "  [resume_level: registered " << placed
			          << " party members in player[] (stand-in for savegame load)]"
			          << std::endl;
		}
		return 0;
	}

	// --- graphics (GRAPHICS.C / INTRFACE.C) wired to thirdeye's Graphics ---
	// A first, deliberately-crude pass: we ignore the AESOP page/window/clip and
	// fade model and just blit onto one screen surface, presenting on refresh.
	// Enough to see the menu; faithful windowing comes with the GIL2VFX port.
	if (gfx) {
		auto fetch = [&](VM::Value n) -> std::vector<uint8_t> & {
			return res.getAsset(static_cast<uint16_t>(n));
		};
		// set_palette(region, resource): load a palette resource into the region.
		if (fn == "set_palette" && args.size() >= 2) {
			uint16_t region = static_cast<uint16_t>(args[0]);
			uint16_t first = region < 5 ? kFirstColor[region] : 0;
			try {
				gfx->setPaletteRange(fetch(args[1]), first);
				std::cout << "  [palette region " << region << "]" << std::endl;
			} catch (const std::exception &e) {
				std::cout << "  [palette failed: " << e.what() << "]" << std::endl;
			}
			return 0;
		}
		// draw_bitmap(page, table, number, x, y, scale, flip, fade_table, fade_level)
		if (fn == "draw_bitmap" && args.size() >= 5) {
			uint16_t table = static_cast<uint16_t>(args[1]);
			uint16_t number = static_cast<uint16_t>(args[2]);
			int x = static_cast<int>(args[3]), y = static_cast<int>(args[4]);
			try {
				gfx->drawImage(fetch(table), number, x, y, true);
				std::cout << "  [drew " << table << ":" << number << " @ " << x
				          << "," << y << "]" << std::endl;
			} catch (const std::exception &e) {
				std::cout << "  [draw failed: " << e.what() << "]" << std::endl;
			}
			return 0;
		}
		// refresh_window / color_fade / light_fade: present the screen.
		if (fn == "refresh_window" || fn == "color_fade" || fn == "light_fade") {
			gfx->update();
			std::cout << "  [present]" << std::endl;
			return 0;
		}
		// set_mouse_pointer(table, number, hot_X, hot_Y, ...)
		if (fn == "set_mouse_pointer" && args.size() >= 2) {
			try {
				gfx->loadMouse(fetch(args[0]), static_cast<uint16_t>(args[1]));
				std::cout << "  [cursor]" << std::endl;
			} catch (const std::exception &e) {
				std::cout << "  [cursor failed: " << e.what() << "]" << std::endl;
			}
			return 0;
		}

		// --- text output (GRAPHICS.C): numbered text windows + print ---
		// text_window(wndnum, wnd) -- bind the text window to a graphics window;
		// we record its horizontal extent so centered/right text can be placed.
		if (fn == "text_window" && args.size() >= 2) {
			int32_t x0, y0, x1, y1;
			if (events.windowRect(args[1], x0, y0, x1, y1))
				gfx->setTextWindow(static_cast<int>(args[0]), x0, y0, x1, y1);
			return 0;
		}
		// text_style(wndnum, font, justify) -- font + justification (0/1/2).
		if (fn == "text_style" && args.size() >= 2) {
			try {
				gfx->setTextFont(static_cast<int>(args[0]),
				                 static_cast<int>(args[1]), fetch(args[1]));
			} catch (const std::exception &) {}
			if (args.size() >= 3)
				gfx->setTextJustify(static_cast<int>(args[0]),
				                    static_cast<int>(args[2]));
			return 0;
		}
		// text_color(wndnum, current, new) -- the remap target colour.
		if (fn == "text_color" && args.size() >= 3) {
			gfx->setTextColor(static_cast<int>(args[0]),
			                  static_cast<uint8_t>(args[2]));
			return 0;
		}
		// text_xy(wndnum, htab, vtab) -- move the text cursor.
		if (fn == "text_xy" && args.size() >= 3) {
			gfx->setTextXY(static_cast<int>(args[0]), static_cast<int>(args[1]),
			               static_cast<int>(args[2]));
			return 0;
		}
		// sprint(wndnum, format_addr, args...) -- printf-style print to a text
		// window. The format string lives at a tagged address (e.g. a PC's "name"
		// in static space, or an inline code string like "%d of %d"); %d/%s are
		// filled from the trailing args. Used for character names + HP readouts.
		if (fn == "sprint" && args.size() >= 2) {
			std::string out = formatSop(vm.readString(args[1]), args, 2, vm);
			gfx->printText(static_cast<int>(args[0]), out);
			std::cout << "  [sprint \"" << out << "\"]" << std::endl;
			return 0;
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
				std::string text = formatSop(fmt, args, 2, vm);
				gfx->printText(static_cast<int>(args[0]), text);
				std::cout << "  [text \"" << text << "\"]" << std::endl;
			} catch (const std::exception &) {}
			return 0;
		}
	}

	std::cout << "  [stub -> 0]" << std::endl;
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
	events.setVerbose(mDebug);
	objects.setTrace(mDebug);
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
	events.setVerbose(mDebug);
	objects.setTrace(mDebug);

	// Seed the boot "mode" that start.MSG_CREATE reads via peekmem(1264) and
	// CASEs on (see the start disassembly). The 4-char modes are stored
	// little-endian: "INTR" -> title menu, "CINE" -> straight to the game (but the
	// party comes from a savegame via resume_*, still stubbed -> empty party),
	// "CHGN" -> run the char-gen party transfer first, then enter the game.
	// --skip-menu picks CHGN so it enters with the real default party (Bob/Carol/
	// Ted/Alice from CHARGEN\CREATE.SAV) instead of an empty one; otherwise we show
	// the menu. (Loading the actual "Quick Start Party" savegame is the resume_*
	// work -- see the Phase 3 save/load note in CLAUDE.md.)
	constexpr int32_t MODE_INTR = 0x494e5452; // 'I''N''T''R' LE -> title menu
	constexpr int32_t MODE_CHGN = 0x4348474e; // 'C''H''G''N' LE -> char-gen + game
	std::map<int32_t, int32_t> mem;
	mem[1264] = mSkipMenu ? MODE_CHGN : MODE_INTR;
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
	registerClasses(resource, objects);

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
	bool quit = false;
	while (!quit) {
		// Text-box clear style depends on the screen we're booting into: the title
		// menu (INTR) bakes its options into the backdrop bitmap, so its text boxes
		// flat-fill to erase them; the in-game HUD draws text over detailed panel
		// art, so its boxes restore the backdrop snapshot (true overlay, no blob).
		if (gfx)
			gfx->setTextRestoreBackground(mem[1264] != MODE_INTR);
		int objIndex = objects.createInstance(classNumber);
		std::string relaunch; // non-empty => a sub-program to run, then re-boot
		try {
			VM::Value result = objects.send(objIndex, MSG_CREATE, {});
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
		if (!relaunch.empty()) {
			try {
				runExternalProgram(relaunch, gfx.get(), resource);
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
void THIRDEYE::Engine::runExternalProgram(const std::string &program,
                                          GRAPHICS::Graphics *gfx,
                                          RESOURCES::Resource &resource) {
	std::string p = program;
	std::transform(p.begin(), p.end(), p.begin(),
	               [](unsigned char c) { return std::tolower(c); });
	if (p.find("cine") != std::string::npos) {
		// "Introduction": the original launches CINE.EXE to play INTRO.GFF, then
		// chain-launches back to the title menu. Drive thirdeye's own GFF player.
		playCinematic(gfx, resource, "INTRO.GFF");
	} else if (p.find("chgen") != std::string::npos ||
	           p.find("chargen") != std::string::npos ||
	           p.find("charge") != std::string::npos) {
		// "Gather a New Party": CHGEN.EXE builds a party and writes CHARGEN\CREATE.SAV,
		// then chains back with mode "CHGN" so start's xfer object reads it and enters
		// the game. We have no char-gen UI yet, so we reuse the existing CREATE.SAV
		// (the default party Bob/Carol/Ted/Alice) -- the transfer (player_attrib etc.)
		// then runs for real. TODO: an actual character-creation UI to author it.
		std::cout << "  [program chain: \"" << program
		          << "\" -> no char-gen UI yet; using the existing CHARGEN\\CREATE.SAV "
		             "(default party) and entering the game]"
		          << std::endl;
	} else {
		std::cout << "  [program chain: \"" << program
		          << "\" -> no thirdeye equivalent yet; re-booting start]"
		          << std::endl;
	}
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
	std::filesystem::path resFile = resolveResourceFile();

	if (!std::filesystem::exists(resFile)) {
		throw std::runtime_error(resFile.string() + " does not exist!");
	}

	RESOURCES::Resource resource(resFile);	// get our game resources ready

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

