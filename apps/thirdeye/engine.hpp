/*
 * engine.hpp
 *
 *  Created on: Mar 31, 2013
 *      Author: bcurtis
 */

#ifndef ENGINE_HPP_
#define ENGINE_HPP_

#include <components/files/configurationmanager.hpp>

#include <map>
#include <iostream>

#include <boost/filesystem.hpp>
#include <SDL2/SDL.h>

#include <alsa/asoundlib.h>

#define GAME_UNKN	0
#define GAME_EOB3	1
#define GAME_HACK	2

namespace THIRDEYE {
// Main engine class, that brings together all the components of Thirdeye
class Engine
{
	bool mNewGame;
	bool mUseSound;
	bool mDebug;
	uint8_t mGame;
	boost::filesystem::path mGameData;

	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Surface *screen;
	SDL_Texture *texture;


	void *buffer;
	int bps;
	int alsa_first_time;
	snd_pcm_t  *pcm;
	unsigned int rate;
	char  *pcmname;

	// not implemented
	Engine(const Engine&);
	Engine& operator=(const Engine&);

	/// Load RES files
	void loadRES();

	/// Load settings from various files, returns the path to the user settings file
	//std::string loadSettings (Settings::Manager & settings);

	/// Prepare engine for game play
	//void prepareEngine (Settings::Manager & settings);

	int send_output (char * output_data, int output_size);
	void close_output ( void );
	int open_alsa_output(void);

public:
	Engine(Files::ConfigurationManager& configurationManager);
	virtual ~Engine();

	/// Initialise and enter main loop.
	void go();

	/// Write screenshot to file.
	void screenshot();

	/// Show version and environment
	void displayEnvironment();

	void setGame(std::string game);
	void setGameData(std::string gameData);
	void setDebugMode(bool debug);
	void setSoundUsage(bool nosound);

private:
	Files::ConfigurationManager& mCfgMgr;
};
}

#endif /* ENGINE_HPP_ */
