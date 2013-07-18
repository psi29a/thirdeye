#include "engine.hpp"
#include "resource.hpp"
#include "xmidi/xmidi.hpp"

#include <components/files/configurationmanager.hpp>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <boost/algorithm/string.hpp>
using boost::algorithm::to_lower;

#include <components/games/eob2.hpp> // temp

extern  "C"{
#include <alsa/asoundlib.h>
#include <wildmidi_lib.h>
}

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Define this to the location of the wildmidi config file */
#define WILDMIDI_CFG "/etc/wildmidi/wildmidi.cfg"


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
	std::vector<uint8_t> xmidi = resource.getAsset("CUE1");
	std::cout << "xmi: " << xmidi.size() << " " << &xmidi[0];
	std::cout << std::endl;

	bool mt32 = true;
	DataSource *xmids = new BufferDataSource (reinterpret_cast<char*> (&xmidi[0]), xmidi.size());
	XMIDI	*xmi = new XMIDI(xmids, mt32?XMIDI_CONVERT_MT32_TO_GS:XMIDI_CONVERT_NOCONVERSION);

	std::vector<uint8_t> midi(sizeof(uint8_t)*4096);
	DataSource *xout = new BufferDataSource (reinterpret_cast<char*> (&midi[0]), midi.size());

	//FILE	*fileout = fopen ("/tmp/test.midi", "wb");
	//DataSource *destFile = new FileDataSource (fileout);

	xmi->retrieve(0,xout);
	//fclose(fileout);

	std::cout << "midi: " << midi.size()
			<< " " << &midi[0]
			<< " " << xout->getPos()
			<< std::endl;
	midi.resize(xout->getPos());

	std::cout << "midi: " << midi.size()
			<< " " << &midi[0]
			<< std::endl;

	alsa_first_time = 1;
	rate = 32072;

	struct _WM_Info * wm_info = NULL;
	static char *config_file = NULL;
	unsigned long int mixer_options = 0;
	static char spinner[] ="|/-\\";
	static int spinpoint = 0;
	unsigned char master_volume = 100;
	char * output_buffer = NULL;
	void *midi_ptr =  NULL;
	unsigned long int count_diff;
	int output_result = 0;
	unsigned long int apr_mins = 0;
	unsigned long int apr_secs = 0;
	unsigned long int perc_play = 0;
	unsigned long int pro_mins = 0;
	unsigned long int pro_secs = 0;

	if (open_alsa_output() == -1){
		std::cerr << "Can't open ALSA!" <<std::endl;
		return;
	}

	if (!config_file) {
		config_file = (char*) malloc(sizeof(WILDMIDI_CFG)+1);
		strncpy (config_file, WILDMIDI_CFG, sizeof(WILDMIDI_CFG));
		config_file[sizeof(WILDMIDI_CFG)] = '\0';
	}

	if (WildMidi_Init (config_file, rate, mixer_options) == -1) {
		std::cerr << "Error: Could not inititilize WildMIDI." << std::endl;
		return;
	}
	WildMidi_MasterVolume(master_volume);

	output_buffer = (char*) malloc(16384);

	if (output_buffer == NULL) {
		std::cerr << "Not enough ram, exiting" << std::endl;
		WildMidi_Shutdown();
		return;
	}

	midi_ptr = WildMidi_OpenBuffer(&midi[0], midi.size());
	wm_info = WildMidi_GetInfo(midi_ptr);
	printf ("Playing test midi: %ld\n", wm_info->approx_total_samples);

    apr_mins = wm_info->approx_total_samples / (rate * 60);
    apr_secs = (wm_info->approx_total_samples % (rate * 60)) / rate;

	while (true){
		count_diff = wm_info->approx_total_samples - wm_info->current_sample;

		if (count_diff == 0)
			break;

		if (count_diff < 4096) {
			output_result = WildMidi_GetOutput (midi_ptr, output_buffer, (count_diff * 4));
		} else {
			output_result = WildMidi_GetOutput (midi_ptr, output_buffer, 4096);
		}

        if (output_result <= 0)
            break;

		wm_info = WildMidi_GetInfo(midi_ptr);
        perc_play =  (wm_info->current_sample * 100) / wm_info->approx_total_samples;
        pro_mins = wm_info->current_sample / (rate * 60);
        pro_secs = (wm_info->current_sample % (rate * 60)) / rate;

		if (output_result > 0)
            send_output (output_buffer, output_result);
		fprintf(stderr, "        [Approx %2lum %2lus Total] [%3i] [%2lum %2lus Processed] [%2lu%%] %c  \r",
			apr_mins, apr_secs, master_volume,
			pro_mins, pro_secs, perc_play, spinner[spinpoint++%4]);
	}
	std::cout << std::endl;

	WildMidi_Close(midi_ptr);
	WildMidi_Shutdown();
	return;

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
					break;
				case SDLK_a:
					break;
				case SDLK_s:
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

int
THIRDEYE::Engine::send_output (char * output_data, int output_size) {
	int err;
	snd_pcm_uframes_t frames;
	while (output_size > 0) {
		frames = snd_pcm_bytes_to_frames(pcm, output_size);
        if ((err = snd_pcm_writei(pcm, output_data, frames)) < 0) {\
			if (snd_pcm_state(pcm) == SND_PCM_STATE_XRUN) {
				if ((err = snd_pcm_prepare(pcm)) < 0)
					printf("snd_pcm_prepare() failed.\r\n");
				alsa_first_time = 1;
				continue;
			}
			return err;
		}

		output_size -= snd_pcm_frames_to_bytes(pcm, err);
		output_data += snd_pcm_frames_to_bytes(pcm, err);
		if (alsa_first_time) {
			alsa_first_time = 0;
			snd_pcm_start(pcm);
		}
	}
	return 0;
}

void
THIRDEYE::Engine::close_output ( void ) {
	snd_pcm_close (pcm);
}

int
THIRDEYE::Engine::open_alsa_output(void) {
	snd_pcm_hw_params_t     *hw;
	snd_pcm_sw_params_t     *sw;
	int err;
	unsigned int alsa_buffer_time;
	unsigned int alsa_period_time;

	if (!pcmname) {
	    pcmname = (char*) malloc (8);
        strcpy(pcmname,"default\0");
	}

	if ((err = snd_pcm_open (&pcm, pcmname, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		printf("Error: audio open error: %s\r\n", snd_strerror (-err));
		return -1;
	}

	snd_pcm_hw_params_alloca (&hw);

	if ((err = snd_pcm_hw_params_any(pcm, hw)) < 0) {
		printf("ERROR: No configuration available for playback: %s\r\n", snd_strerror(-err));

		return -1;
	}

	if ((err = snd_pcm_hw_params_set_access(pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
		printf("Cannot set mmap'ed mode: %s.\r\n", snd_strerror(-err));
		return -1;
	}

	if (snd_pcm_hw_params_set_format (pcm, hw, SND_PCM_FORMAT_S16_LE) < 0) {
		printf("ALSA does not support 16bit signed audio for your soundcard\r\n");
		close_output();
		return -1;
	}

	if (snd_pcm_hw_params_set_channels (pcm, hw, 2) < 0) {
		printf("ALSA does not support stereo for your soundcard\r\n");
		close_output();
		return -1;
	}

	if (snd_pcm_hw_params_set_rate_near(pcm, hw, &rate, 0) < 0) {
		printf("ALSA does not support %iHz for your soundcard\r\n",rate);
		close_output();
		return -1;
	}

	alsa_buffer_time = 500000;
	alsa_period_time = 50000;

	if ((err = snd_pcm_hw_params_set_buffer_time_near(pcm, hw, &alsa_buffer_time, 0)) < 0)
	{
		printf("Set buffer time failed: %s.\r\n", snd_strerror(-err));
		return -1;
	}

	if ((err = snd_pcm_hw_params_set_period_time_near(pcm, hw, &alsa_period_time, 0)) < 0)
	{
		printf("Set period time failed: %s.\r\n", snd_strerror(-err));
		return -1;
	}

	if (snd_pcm_hw_params(pcm, hw) < 0)
	{
		printf("Unable to install hw params\r\n");
		return -1;
	}

	snd_pcm_sw_params_alloca(&sw);
	snd_pcm_sw_params_current(pcm, sw);
	if (snd_pcm_sw_params(pcm, sw) < 0)
	{
		printf("Unable to install sw params\r\n");
		return -1;
	}

	//send_output = write_alsa_output;
	//close_output = close_alsa_output;
	if (pcmname != NULL) {
	    free (pcmname);
	}
	return 0;
}

