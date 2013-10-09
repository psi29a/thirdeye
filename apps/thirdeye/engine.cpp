#include "engine.hpp"
#include "resources/res.hpp"
#include "resources/gffi.hpp"
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
	RESOURCES::Resource resource(mGameData);	// get our game resources ready
	RESOURCES::GFFI gffi(mGameData.remove_leaf() /= "INTRO.GFF"); // get our intro cinematic

	/*
	 Settings::Manager settings;
	 std::string settingspath;

	 settingspath = loadSettings (settings);
	 */

	std::vector<uint8_t> &snd = resource.getAsset("BIRD4");
	std::vector<uint8_t> &xmidi = resource.getAsset("CUE1");


	std::vector<uint8_t> &font = resource.getAsset("8x8 font");
	//std::vector<uint8_t> &font2 = resource.getAsset("6x8 font");
	//std::vector<uint8_t> &font3 = resource.getAsset("Ornate font");

	std::vector<uint8_t> &bmp = resource.getAsset("Backdrop");
	std::vector<uint8_t> &icons = resource.getAsset("Icons");
	std::vector<uint8_t> &marble = resource.getAsset("Marble walls");
	std::vector<uint8_t> &basePalette = resource.getAsset("Fixed palette");
	std::vector<uint8_t> &subPalette = resource.getAsset("Marble palette");
	std::string text = resource.getTableEntry("Marble palette", 1);

	gfx.loadPalette(basePalette);
	gfx.drawImage(bmp, 0, 0, false);

	gfx.loadPalette(basePalette, subPalette, text);
	gfx.drawImage(marble, 18, 0, 0, true);
	gfx.drawImage(marble, 0, 0, 0, true);
	gfx.drawImage(marble, 1, 24, 8, true);
	gfx.drawImage(marble, 2, 48, 20, true);
	gfx.drawImage(marble, 3, 64, 28, true);

	gfx.drawImage(icons, 1, 25, 120, true);
	gfx.drawText(font,"Welcome to Thirdeye!", 8, 181);

	gfx.loadMouse(icons, 0);


	Uint32 	clock = 0;
	Uint32 	currentSecond = 0;
	Uint32 	wait = 0;
	bool 	update = false;

	std::map<uint8_t, tuple<uint8_t, uint8_t, std::vector<uint8_t> > > cutscene = gffi.getSequence();


	gfx.loadPalette(cutscene[0].get<2>(), false);

	gfx.panDirection(0, cutscene[16].get<2>(), cutscene[17].get<2>(), cutscene[18].get<2>(), cutscene[19].get<2>());
	gfx.update();
	SDL_Delay(100);
	gfx.update();
	SDL_Delay(100);
	gfx.update();
	//return;
	//gfx.playAnimation(cutscene[11].get<2>());
	SDL_Delay(100);
	gfx.update();
	SDL_Delay(100);
	gfx.update();
	//gfx.playAnimation(cutscene[13].get<2>());
	SDL_Delay(100);
	gfx.update();
	SDL_Delay(100);
	gfx.update();
	//gfx.playAnimation(cutscene[14].get<2>());
	SDL_Delay(100);
	gfx.update();
	SDL_Delay(100);
	gfx.update();
	//gfx.playAnimation(cutscene[15].get<2>());
	SDL_Delay(100);
	while (true){
	  gfx.update();
	  SDL_Delay(0.1);
	}
	return;


	// Start the main rendering loop
	SDL_Event event;
	bool done = false;
	while (!done)  // Enter main loop.
	{
		clock = SDL_GetTicks();
		if (clock/1000 > currentSecond){
			currentSecond = clock/1000;
			update = true;
		}

		if (wait > 0 and update)
			wait--;

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

		if (cutscene.size() > 0 and wait == 0){
			uint8_t index = cutscene.begin()->first;
			std::cout << "Playing cutscene: " << (int) index << std::endl;
			tuple<uint8_t, uint8_t, std::vector<uint8_t> > scene = cutscene.begin()->second;
			switch (scene.get<0>()){
			case SETT_PAL:
				gfx.loadPalette(scene.get<2>(), false);
				break;
			case PANB_LEFT:
				gfx.drawImage(scene.get<2>(), 0, 0, 3, true);
				cutscene.erase(index); // just a test
				index = cutscene.begin()->first;
				scene = cutscene.begin()->second;
				gfx.drawImage(scene.get<2>(), 0, 0, 3, true);
				break;
			case DISP_BMP:
				gfx.drawImage(scene.get<2>(), 0, 0, 0, false);
				break;
			case PANF_LEFT:
				gfx.drawImage(scene.get<2>(), 0, 0, 0, true);
				break;
			case DISP_BMA:
				gfx.playAnimation(scene.get<2>());
				break;
			case FADE_IN:
				gfx.drawImage(scene.get<2>(), 0, 0, 0, false);
				gfx.fadeIn();
				break;
			case FADE_LEFT:
				gfx.drawImage(scene.get<2>(), 0, 0, 0, true);
				break;
			default:
				std::cerr << "Case not yet implemented." << std::endl;
				throw;
			}
			wait = scene.get<1>();
			cutscene.erase(index);
		}

		gfx.update();		// update our screen
		mixer.update();		// update our sounds

		std::cout << "Clock " << clock/1000 << std::endl;

		//printf("Waiting 60...\n");
		update = false;
		SDL_Delay(60*2);      // Pause briefly before moving on to the next cycle.
	}

	// Save user settings
	//settings.saveUser(settingspath);

	std::cout << "Quitting peacefully." << std::endl;
}

