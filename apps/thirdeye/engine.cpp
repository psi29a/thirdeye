#include "engine.hpp"
#include "resources/res.hpp"
#include "resources/gffi.hpp"
#include "sound/sound.hpp"
#include "graphics/graphics.hpp"
#include "vm/vm.hpp"

#include <components/files/configurationmanager.hpp>

#include <algorithm>
#include <cctype>

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

// Drive the SOP bytecode of each code object through the VM. This is the
// bring-up path: it runs every exported message handler from its entry offset
// and reports the outcome (END with a return value, or the first opcode we
// can't execute yet -- typically a CALL/SEND into the not-yet-built runtime).
void THIRDEYE::Engine::runResourceVM(RESOURCES::Resource &resource) {
	std::vector<std::string> codeNames = resource.getCodeResourceNames();
	if (codeNames.empty()) {
		std::cout << "\nNo SOP code objects in this resource file." << std::endl;
		return;
	}

	std::cout << "\nRunning SOP bytecode (" << codeNames.size()
	          << " code object(s))..." << std::endl;

	for (const std::string &name : codeNames) {
		std::vector<uint8_t> code = resource.getAsset(name);
		std::map<std::string, std::string> exports = resource.getExports(name);

		VM::Interpreter info(code);
		const VM::SopHeader &h = info.header();
		std::cout << "\n=== code object \"" << name << "\" ==="
		          << "  (static=" << h.static_size << " import=" << h.import_resource
		          << " export=" << h.export_resource << " parent="
		          << (h.parent == 0xFFFFFFFFu ? std::string("none")
		                                      : std::to_string(h.parent))
		          << ")" << std::endl;

		// Each "M:<n>" export maps a message number to a handler entry offset.
		for (const auto &entry : exports) {
			if (entry.first.rfind("M:", 0) != 0)
				continue; // skip non-handler entries (e.g. "N:OBJECT")

			uint32_t offset = static_cast<uint32_t>(std::stoul(entry.second));
			std::cout << "  handler " << entry.first << " @ " << offset << ": ";

			VM::Interpreter vm(code); // fresh stack/frame per handler
			vm.setTrace(mDebug);
			if (mDebug)
				std::cout << std::endl;
			try {
				VM::Value result = vm.execute(offset);
				std::cout << "END (returned " << result << ")" << std::endl;
			} catch (const VM::VmError &e) {
				std::cout << "stopped -- " << e.what() << std::endl;
			}
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

