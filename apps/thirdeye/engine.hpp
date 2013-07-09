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

namespace THIRDEYE {
// Main engine class, that brings together all the components of Thirdeye
class Engine //: private Ogre::FrameListener
{
	bool mNewGame;
	bool mUseSound;
	SDL_Window *window = NULL;
	SDL_Renderer *renderer = NULL;
	SDL_Surface *screen = NULL;
	SDL_Texture *texture = NULL;

	// not implemented
	Engine(const Engine&);
	Engine& operator=(const Engine&);

	/// Load RES files
	void loadRES();

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

	/// Show version and environment
	void displayEnvironment();

private:
	Files::ConfigurationManager& mCfgMgr;
};
}

#endif /* ENGINE_HPP_ */
