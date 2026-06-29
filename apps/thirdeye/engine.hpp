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

#include <filesystem>
#include <SDL3/SDL.h>

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

namespace RESOURCES { class Resource; }
namespace GRAPHICS { class Graphics; }
namespace VM { class VmError; }

namespace THIRDEYE {
// Main engine class, that brings together all the components of Thirdeye
class Engine
{
	bool mNewGame;
	bool mUseSound;
	bool mDebug;
	bool mForceVM = false;
	bool mSkipMenu = false;  // boot straight into the game (skip the title menu)
	bool mSkipIntro = false; // skip the intro cinematic
	bool mChargen   = false; // --chargen: force the chargen path even if a save exists
	bool mChargenTest = false; // --chargen-test: run chargen entry screen in isolation
	bool mRenderer;
	uint8_t mGame;
	uint16_t mScale;
	std::filesystem::path mGameData;

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
	void setForceVM(bool forceVM);
	void setSkipMenu(bool skipMenu);
	void setSkipIntro(bool skipIntro);
	void setChargen(bool chargen);
	void setChargenTest(bool chargenTest);
	void setDebugMode(bool debug);
	void setSoundUsage(bool nosound);
	void setRenderer(bool renderer);
	void setScale(uint16_t);
private:
	/// Resolve the game-data path (which may be a directory or a direct .RES
	/// file) into the actual resource file to open, auto-detecting the game
	/// from the filename when a file is given.
	std::filesystem::path resolveResourceFile();

	/// Drive each SOP code object's message handlers through the bytecode VM,
	/// reporting where each one ends (END, or the first unimplemented opcode).
	/// With debug mode on, prints a per-instruction trace.
	void runResourceVM(RESOURCES::Resource &resource);

	/// Boot a single object the way the original interpreter does: create an
	/// instance of `objectName` and send it MSG_CREATE (0). This is the data-
	/// driven bring-up path -- it runs the real boot handler and stops at the
	/// first runtime function / opcode we haven't implemented.
	void bootObject(RESOURCES::Resource &resource, const std::string &objectName);

	/// Play thirdeye's internal stand-in for a launched DOS sub-program (the
	/// intro cinematic, character generation, ...). Called when a menu choice
	/// hands off via launch(); after it returns, bootObject re-boots `start` on
	/// the mode the bytecode poked into cell 1264 (AESOP's program chain).
	/// Returns true normally; false if the sub-program cancelled (e.g. user
	/// hit Esc out of the chargen entry screen) -- caller then resets
	/// mem[1264] to MODE_INTR so the next start lands on the title menu.
	bool runExternalProgram(const std::string &program, GRAPHICS::Graphics *gfx,
	                        RESOURCES::Resource &resource);

	/// Play a GFF cinematic (e.g. INTRO.GFF) that sits beside the game's .RES,
	/// reusing thirdeye's GFF player. Honors --skip-intro; ESC/Enter skips;
	/// closing the window throws QuitRequested to end the session.
	void playCinematic(GRAPHICS::Graphics *gfx, RESOURCES::Resource &resource,
	                   const std::string &gffName);

	/// Hold a partially-rendered frame on screen after the VM hits an
	/// unimplemented runtime function/opcode (a bring-up wall), until the user
	/// closes the window or presses ESC.
	void handleBootWall(const VM::VmError &e, GRAPHICS::Graphics *gfx);

	Files::ConfigurationManager& mCfgMgr;
	std::string mResource;
};
}

#endif /* ENGINE_HPP_ */
