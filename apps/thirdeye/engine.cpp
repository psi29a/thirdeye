#include "engine.hpp"
#include "resource.hpp"
#include "sound/sound.hpp"
#include "graphics/graphics.hpp"

#include <components/files/configurationmanager.hpp>

#include <SDL2/SDL.h>

#include <boost/algorithm/string.hpp>
using boost::algorithm::to_lower;

THIRDEYE::Engine::Engine(Files::ConfigurationManager& configurationManager) :
		mNewGame(false),
		mUseSound(true),
		mDebug(false),
		mGame(GAME_UNKN),
		mCfgMgr(configurationManager),
		mScale(1)
{
	std::cout << "Initializing Thirdeye... ";

	std::srand(std::time(NULL));

	std::cout << "done!" << std::endl;
}

THIRDEYE::Engine::~Engine() {

}

// Setup engine via parameters
void THIRDEYE::Engine::setGame(std::string game){
	boost::algorithm::to_lower(game);

	if (game == "eob3"){
		mGame = GAME_EOB3;
		mGameData /= "EYE.RES";
	} else if (game == "hack") {
		mGame = GAME_HACK;
		mGameData /= "HACK.RES";
	} else
		mGame = GAME_UNKN;

}

void THIRDEYE::Engine::setGameData(std::string gameData){
	mGameData = boost::filesystem::path(gameData);
}
void THIRDEYE::Engine::setDebugMode(bool debug){
	mDebug = debug;
}
void THIRDEYE::Engine::setSoundUsage(bool nosound){
	mUseSound = !nosound;
}
void THIRDEYE::Engine::setScale(uint16_t scale){
	mScale = scale;
}


// Initialise and enter main loop.
void THIRDEYE::Engine::go() {
	MIXER::Mixer mixer;		// setup our sound mixer
	GRAPHICS::Graphics gfx(mScale); // setup our graphics
	RESOURCE::Resource resource(mGameData);	// get our game resources ready

	/*
	 Settings::Manager settings;
	 std::string settingspath;

	 settingspath = loadSettings (settings);
	 */

	std::vector<uint8_t> snd = resource.getAsset("BIRD4");
	std::vector<uint8_t> xmidi = resource.getAsset("CUE1");

	std::vector<uint8_t> font = resource.getAsset("8x8 font");
	std::vector<uint8_t> font2 = resource.getAsset("6x8 font");
	std::vector<uint8_t> font3 = resource.getAsset("Ornate font");

	std::vector<uint8_t> bmp = resource.getAsset("Backdrop");
	std::vector<uint8_t> icons = resource.getAsset("Icons");
	std::vector<uint8_t> marble = resource.getAsset("Marble walls");
	std::vector<uint8_t> basePalette = resource.getAsset("Fixed palette");
	std::vector<uint8_t> subPalette = resource.getAsset("Marble palette");

	std::string text = resource.getTableEntry("Marble palette", 1);
	gfx.loadPalette(basePalette, subPalette, text);
	gfx.drawImage(bmp, 0, 0, 0);

	gfx.drawImage(marble, 18, 0, 0, true);
	gfx.drawImage(marble, 0, 0, 0, true);
	gfx.drawImage(marble, 1, 24, 8, true);
	gfx.drawImage(marble, 2, 48, 20, true);
	gfx.drawImage(marble, 3, 64, 28, true);


	gfx.drawImage(icons, 1, 25, 120, true);
	gfx.drawText(font,"Welcome to Thirdeye!", 8, 181);
	gfx.loadMouse(icons, 0);


	//return;

	// Start the main rendering loop
	SDL_Event event;
	bool done = false;
	while (!done)  // Enter main loop.
	{
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			// this is the window x being clicked.
			case SDL_QUIT:
				done = true;
				break;
				// process the mouse data by passing it to ngl class
				case SDL_MOUSEMOTION:
					//ngl.mouseMoveEvent(event.motion);
					std::cout << "Mouse moved @ " <<  event.motion.x << " " << event.motion.y << std::endl;
					break;
				case SDL_MOUSEBUTTONDOWN: break;
				case SDL_MOUSEBUTTONUP:
					std::cout << "Mouse clicked @ " << event.button.x << " " << event.button.y << std::endl;
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

		//printf("Waiting 60...\n");
		SDL_Delay(60);      // Pause briefly before moving on to the next cycle.
	}

	// Save user settings
	//settings.saveUser(settingspath);

	std::cout << "Quitting peacefully." << std::endl;
}

