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

// Types of games
#define GAME_UNKN	0
#define GAME_EOB3	1
#define GAME_HACK	2

// Game states
#define STATE_INTRO		0
#define STATE_MENU		1
#define	STATE_CONTINUE	2
#define STATE_NEW_PARTY	3
#define STATE_SUMMON	4
#define STATE_ABANDON	5

namespace THIRDEYE {
// Main engine class, that brings together all the components of Thirdeye
class Engine
{
	bool mNewGame;
	bool mUseSound;
	bool mDebug;
	bool mRenderer;
	uint8_t mGame;
	uint16_t mScale;
	boost::filesystem::path mGameData;

	// not implemented
	Engine(const Engine&);
	Engine& operator=(const Engine&);

	/// Load settings from various files, returns the path to the user settings file
	//std::string loadSettings (Settings::Manager & settings);

	/// Prepare engine for game play
	//void prepareEngine (Settings::Manager & settings);

public:
	Engine(Files::ConfigurationManager& configurationManager);
	virtual ~Engine();

	/// Initialise and enter main loop.
	void go();

	/// Write screenshot to file.
	void screenshot();

	void setGame(std::string game);
	void setGameData(std::string gameData);
	void setDebugMode(bool debug);
	void setSoundUsage(bool nosound);
	void setRenderer(bool renderer);
	void setScale(uint16_t);
private:
	Files::ConfigurationManager& mCfgMgr;
	std::string mResource;
};
}

#endif /* ENGINE_HPP_ */
