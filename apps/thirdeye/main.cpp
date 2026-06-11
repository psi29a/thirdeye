#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>

#include "engine.hpp"

#include <components/files/configurationmanager.hpp>

#include "config.hpp"

namespace
{

static std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

static bool toBool(const std::string& value)
{
    const auto normalized = toLower(value);
    return normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on";
}

} // namespace

int main(int argc, char** argv) {
    try {
        Files::ConfigurationManager cfgMgr;

        CLI::App app{"Thirdeye - a reimplementation of the AESOP engine "
                     "(Eye of the Beholder 3 / Dungeon Hack)"};
        app.set_version_flag("--version", std::string{THIRDEYE_VERSION});

        // Option values (with their defaults). These feed the VariablesMap below
        // so that config files (thirdeye.cfg) can still override them, preserving
        // the original precedence: defaults < command line < config file.
        std::string gameData = "/opt/eob3";
        std::string game     = "eob3";
        std::string start    = "LEVEL4";
        uint16_t    scale     = 1;
        bool        renderer  = false;
        bool        forceVM   = false;
        bool        debug     = false;
        bool        nosound   = false;
        bool        newGame   = false;
        bool        fsStrict  = false;

        // Positional arg OR --game-data: a game-data directory or a .RES file.
        app.add_option("game-data,--game-data", gameData,
                       "Path to a game-data directory or a .RES file");
        app.add_option("--game", game, "Which game to play (eob3, hack)");
        app.add_option("--start", start, "Starting location");
        app.add_option("--scale", scale, "Resolution scale");
        app.add_flag("--renderer", renderer, "Enable hardware renderer");
        app.add_flag("--vm", forceVM,
                     "Boot the SOP bytecode VM (run the 'start' object) instead of the intro");
        app.add_flag("--debug", debug, "Enable debug mode");
        app.add_flag("--nosound", nosound, "Disable all sounds");
        app.add_flag("--new-game", newGame, "Activate new game mechanics");
        app.add_flag("--fs-strict", fsStrict, "Use strict file system handling");

        CLI11_PARSE(app, argc, argv); // handles --help/--version (exits cleanly)

        // Bridge into the config system: stuff the parsed values into the map,
        // then let config files override (matching prior behaviour).
        Files::ConfigurationManager::VariablesMap variables = {
            {"game-data", gameData},
            {"game",      game},
            {"start",     start},
            {"scale",     std::to_string(scale)},
            {"renderer",  renderer ? "true" : "false"},
            {"vm",        forceVM  ? "true" : "false"},
            {"debug",     debug    ? "true" : "false"},
            {"nosound",   nosound  ? "true" : "false"},
            {"new-game",  newGame  ? "true" : "false"},
            {"fs-strict", fsStrict ? "true" : "false"},
        };
        cfgMgr.readConfiguration(variables);

        THIRDEYE::Engine engine(cfgMgr);
        engine.setGameData(variables["game-data"]);
        engine.setGame(variables["game"]);
        engine.setForceVM(toBool(variables["vm"]));
        engine.setDebugMode(toBool(variables["debug"]));
        engine.setSoundUsage(toBool(variables["nosound"]));
        engine.setScale(static_cast<uint16_t>(std::stoi(variables["scale"])));

        engine.go();
    } catch (std::exception &e) {
        std::cout << "\nERROR: " << e.what() << std::endl;
        return (1);
    }

    return (0);
}
