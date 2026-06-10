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

// Runtime-function library. Most functions are still stubs (log + return 0),
// but where we can do the real thing, we do. Each call is logged with its
// arguments, resolving address arguments to inline strings when possible.
// `gfx` is null when running headless (no display); the graphics functions then
// fall through to the stub.
VM::Value defaultRuntimeCall(VM::ObjectSystem &objects, VM::EventSystem &events,
                             GRAPHICS::Graphics *gfx, RESOURCES::Resource &res,
                             VM::Interpreter &vm, const std::string &fn,
                             const std::vector<VM::Value> &args) {
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
	if (fn == "dispatch_event") { events.dispatchEvent(); return 0; }
	if (fn == "drain_event_queue") { events.drainEventQueue(); return 0; }
	if (fn == "peek_event") return events.peekEvent() ? 1 : 0;
	if (fn == "flush_event_queue" && args.size() >= 3) {
		events.flushEventQueue(args[0], args[1], args[2]);
		return 0;
	}
	if (fn == "flush_input_events") { events.flushInputEvents(); return 0; }

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

	// launch(mode, program, arg1, arg2): spawn a secondary program. In AESOP
	// this is how EOB3 hands off to its sub-programs (e.g. CHARGEN).
	if (fn == "launch") {
		std::string program;
		for (VM::Value a : args) {
			std::string s = vm.readCodeString(static_cast<uint32_t>(a));
			if (!s.empty()) { program = s; break; }
		}
		if (program.empty()) {
			std::cout << "  [launch: no program name]" << std::endl;
			return 0;
		}
		if (std::filesystem::exists(program)) {
			std::string path = std::filesystem::absolute(program).string();
			std::cout << "  [launch: running \"" << path << "\"]" << std::endl;
			return std::system(path.c_str());
		}
		std::cout << "  [launch: \"" << program << "\" not found, nothing to run]"
		          << std::endl;
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
	events.setVerbose(mDebug);
	objects.setTrace(mDebug);
	objects.setRuntimeCall(
		[&objects, &events, &resource](VM::Interpreter &vm, const std::string &fn,
		           const std::vector<VM::Value> &args) {
			// runResourceVM stays headless (gfx = nullptr) -- it's for small
			// resources like SAMPLE.RES, not the full game.
			return defaultRuntimeCall(objects, events, nullptr, resource, vm, fn,
			                          args);
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
	objects.setRuntimeCall(
		[&objects, &events, &gfx, &resource](VM::Interpreter &vm,
		           const std::string &fn, const std::vector<VM::Value> &args) {
			return defaultRuntimeCall(objects, events, gfx.get(), resource, vm, fn,
			                          args);
		});
	objects.setMaxSteps(2000000); // bring-up safety: stop runaway loops
	registerClasses(resource, objects);

	uint16_t classNumber = 0;
	if (!objects.findClassByName(objectName, classNumber)) {
		std::cout << "Boot object \"" << objectName
		          << "\" not found in this resource file." << std::endl;
		return;
	}

	std::cout << "\nBooting object \"" << objectName
	          << "\" (sending MSG_CREATE)..." << std::endl;
	int objIndex = objects.createInstance(classNumber);
	try {
		VM::Value result = objects.send(objIndex, MSG_CREATE, {});
		std::cout << "Boot handler returned " << result << "." << std::endl;
	} catch (const VM::VmError &e) {
		std::cout << "\nBoot stopped at the first unimplemented piece:\n  "
		          << e.what() << std::endl;
	}

	// The SOP main loop spins on dispatch_event() with no input to break it, so
	// the budget trips with the menu already drawn. Hold the rendered frame on
	// screen until the user quits, so the result is actually visible. (Temporary:
	// the real fix is to drive the engine off the event queue -- feed SDL input +
	// a timer heartbeat into dispatch_event so the loop is event-driven.)
	if (gfx) {
		std::cout << "\nRendered frame is up -- press ESC or close the window to quit."
		          << std::endl;
		bool done = false;
		SDL_Event event;
		while (!done) {
			while (SDL_PollEvent(&event)) {
				if (event.type == SDL_QUIT ||
				    (event.type == SDL_KEYDOWN &&
				     event.key.keysym.sym == SDLK_ESCAPE))
					done = true;
			}
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

	// --vm: boot the SOP bytecode VM (the 'start' object) instead of the intro.
	// This is the data-driven bring-up path.
	if (mForceVM) {
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

