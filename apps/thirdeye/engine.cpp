#include "engine.hpp"
#include "resource.hpp"
#include "sound/sound.hpp"

#include <components/files/configurationmanager.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <boost/algorithm/string.hpp>
using boost::algorithm::to_lower;

#include <components/games/eob2.hpp> // temp


THIRDEYE::Engine::Engine(Files::ConfigurationManager& configurationManager) :
		mNewGame(false),
		mUseSound(true),
		mDebug(false),
		mGame(GAME_UNKN),
		window(NULL),
		renderer(NULL),
		screen(NULL),
		texture(NULL),
		mCfgMgr(configurationManager)
{
	std::cout << "Initializing Thirdeye... ";

	std::srand(std::time(NULL));

	Uint32 flags = SDL_INIT_VIDEO;
	if (SDL_WasInit(flags) == 0) {
		//kindly ask SDL not to trash our OGL context
		//might this be related to http://bugzilla.libsdl.org/show_bug.cgi?id=748 ?
		//SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");

		if (SDL_Init(flags) != 0) {
			throw std::runtime_error(
					"Could not initialize SDL! " + std::string(SDL_GetError()));
		}
	}
	std::cout << "done!" << std::endl;
}

THIRDEYE::Engine::~Engine() {
	// Done! Close the window, clean-up and exit the program.
	SDL_Quit();
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
	mGameData = gameData;
}
void THIRDEYE::Engine::setDebugMode(bool debug){
	mDebug = debug;
}
void THIRDEYE::Engine::setSoundUsage(bool nosound){
	mUseSound = !nosound;
}


// Initialise and enter main loop.
void THIRDEYE::Engine::go() {
	displayEnvironment();
	RESOURCE::Resource resource(mGameData);

	/*
	 Settings::Manager settings;
	 std::string settingspath;

	 settingspath = loadSettings (settings);
	 */

	// Play some good 'ol tunes
	//MWBase::Environment::get().getSoundManager()->playPlaylist(std::string("Explore"));

	// temp return
	std::vector<uint8_t> snd = resource.getAsset("WEASEL");
	std::ofstream os ("/tmp/WEASEL.SND", std::ios::binary);
	os.write((const char*) &snd[0], snd.size());
	os.close();

	std::vector<uint8_t> xmidi = resource.getAsset("CUE1");
	std::cout << "xmi: " << xmidi.size() << " " << &xmidi[0];
	std::cout << std::endl;

	MIXER::Mixer mixer;
	//return;
	//mixer.playMusic(xmidi);
	// Create a window.
	window = SDL_CreateWindow("Thirdeye", SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED, 320, 200, 0    //SDL_WINDOW_SHOWN
			);

	// Create the renderer driver to be used in window
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

	//SDL_GetRendererInfo(renderer, &displayRendererInfo);

	// Create our game screen that will be blitted to before renderering
	screen = SDL_CreateRGBSurface(0, 320, 200, 32, 0, 0, 0, 0);

	// set magenta as our transparent colour
	SDL_SetColorKey(screen, SDL_TRUE, SDL_MapRGB(screen->format, 255, 0, 255));

	// temp code to display CPS files
	SDL_Surface** images;
	images = new SDL_Surface*[256];
	Games::gameInit(images);
	SDL_BlitSurface(images[0], NULL, screen, NULL);
	std::cout << "End of Data..." << std::endl;

	// Start the main rendering loop
	SDL_Event event;
	Uint8 done = 0;
	while (!done)  // Enter main loop.
	{
		while (SDL_PollEvent(&event)) {
			switch (event.type) {
			// this is the window x being clicked.
			case SDL_QUIT:
				done = true;
				break;
				// process the mouse data by passing it to ngl class
				//case SDL_MOUSEMOTION : ngl.mouseMoveEvent(event.motion); break;
				//case SDL_MOUSEBUTTONDOWN : ngl.mousePressEvent(event.button); break;
				//case SDL_MOUSEBUTTONUP : ngl.mouseReleaseEvent(event.button); break;
				//case SDL_MOUSEWHEEL : ngl.wheelEvent(event.wheel);
				// if the window is re-sized pass it to the ngl class to change gl viewport
				// note this is slow as the context is re-create by SDL each time
			case SDL_WINDOWEVENT:
				int w, h;
				// get the new window size
				SDL_GetWindowSize(window, &w, &h);
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
					SDL_SetWindowFullscreen(window, SDL_TRUE);
					break;
				case SDLK_g:
					SDL_SetWindowFullscreen(window, SDL_FALSE);
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

		// generate texture from our screen surface
		texture = SDL_CreateTextureFromSurface(renderer, screen);

		// Clear the entire screen to our selected color.
		SDL_RenderClear(renderer);

		// blit texture to display
		SDL_RenderCopy(renderer, texture, NULL, NULL);

		// Up until now everything was drawn behind the scenes.
		// This will show the new, red contents of the window.
		SDL_RenderPresent(renderer);

		printf("Waiting 60...\n");
		SDL_Delay(60);      // Pause briefly before moving on to the next cycle.
	}

	// Save user settings
	//settings.saveUser(settingspath);

	std::cout << "Quitting peacefully." << std::endl;
}

void THIRDEYE::Engine::displayEnvironment() {
	std::cout << "Initializing SDL... ";

	SDL_SysWMinfo info;
	SDL_Window* window = SDL_CreateWindow("", 0, 0, 0, 0, SDL_WINDOW_HIDDEN);
	SDL_VERSION(&info.version); // initialize info structure with SDL version info

	if (SDL_GetWindowWMInfo(window, &info)) { // the call returns true on success
		// success
		std::cout << "done!" << std::endl << "  Version:	"
				<< (int) info.version.major << "." << (int) info.version.minor
				<< "." << (int) info.version.patch << std::endl
				<< "  Environment:	";
		switch (info.subsystem) {
		case SDL_SYSWM_UNKNOWN:
			std::cout << "an unknown system!";
			break;
		case SDL_SYSWM_WINDOWS:
			std::cout << "Microsoft Windows(TM)";
			break;
		case SDL_SYSWM_X11:
			std::cout << "X Window System";
			break;
		case SDL_SYSWM_DIRECTFB:
			std::cout << "DirectFB";
			break;
		case SDL_SYSWM_COCOA:
			std::cout << "Apple OS X";
			break;
		case SDL_SYSWM_UIKIT:
			std::cout << "UIKit";
			break;
		}
		std::cout << std::endl;
	} else {
		// call failed
		std::cout << std::endl << "Couldn't get window information: "
				<< SDL_GetError();
	}
}

