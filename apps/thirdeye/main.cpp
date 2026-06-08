#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <cstdlib>
#include <sstream>

#include "engine.hpp"

#include <components/files/configurationmanager.hpp>

#if defined(_WIN32) && !defined(_CONSOLE)
#include <Windows.h>
#endif

#include "config.hpp"

/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 *
 * Results are directly written to \ref Engine class.
 *
 * \retval true - Everything goes OK
 * \retval false - Error
 */
namespace
{

typedef Files::ConfigurationManager::VariablesMap VariablesMap;

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

static VariablesMap getDefaultOptions()
{
    return {
        {"help", "false"},
        {"version", "false"},
        {"game", "eob3"},
        {"game-data", "/opt/eob3"},
        {"start", "LEVEL4"},
        {"scale", "1"},
        {"renderer", "false"},
        {"debug", "false"},
        {"nosound", "false"},
        {"new-game", "false"},
        {"fs-strict", "false"}
    };
}

static bool startsWith(const std::string& str, const std::string& prefix)
{
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

static std::string stripLeadingDashes(const std::string& arg)
{
    if (startsWith(arg, "--"))
        return arg.substr(2);
    if (startsWith(arg, "-"))
        return arg.substr(1);
    return arg;
}

} // namespace

bool parseOptions(int argc, char** argv, THIRDEYE::Engine& engine,
        Files::ConfigurationManager& cfgMgr) {
    VariablesMap variables = getDefaultOptions();

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h")
        {
            variables["help"] = "true";
            continue;
        }

        if (arg == "--version")
        {
            variables["version"] = "true";
            continue;
        }

        if (startsWith(arg, "--"))
        {
            auto pos = arg.find('=');
            std::string key;
            std::string value;
            if (pos != std::string::npos)
            {
                key = stripLeadingDashes(arg.substr(0, pos));
                value = arg.substr(pos + 1);
            }
            else
            {
                key = stripLeadingDashes(arg);
                if (i + 1 < argc && !startsWith(argv[i + 1], "-"))
                {
                    value = argv[++i];
                }
                else
                {
                    value = "true";
                }
            }

            if (!key.empty())
            {
                variables[key] = value;
            }
            continue;
        }
    }

    cfgMgr.readConfiguration(variables);

    bool run = true;

    if (toBool(variables["help"])) {
        std::cout << "Syntax: thirdeye <options>\nAllowed options" << std::endl;
        std::cout << "  --help                 print help message" << std::endl;
        std::cout << "  --version              print version information and quit" << std::endl;
        std::cout << "  --game <name>          set which game we want to play" << std::endl;
        std::cout << "  --game-data <path>     set game data directory" << std::endl;
        std::cout << "  --start <location>     set starting location" << std::endl;
        std::cout << "  --scale <n>            set resolution scale" << std::endl;
        std::cout << "  --renderer             enable hardware renderer" << std::endl;
        std::cout << "  --debug                enable debug mode" << std::endl;
        std::cout << "  --nosound              disable all sounds" << std::endl;
        std::cout << "  --new-game             activate new game mechanics" << std::endl;
        std::cout << "  --fs-strict            use strict file system handling" << std::endl;
        run = false;
    }

    if (toBool(variables["version"])) {
        std::cout << "Thirdeye version " << THIRDEYE_VERSION << std::endl;
        run = false;
    }

    if (!run)
        return false;

    engine.setGameData(variables["game-data"]);
    engine.setGame(variables["game"]);
    engine.setDebugMode(toBool(variables["debug"]));
    engine.setSoundUsage(toBool(variables["nosound"]));
    engine.setScale(static_cast<uint16_t>(std::stoi(variables["scale"])));

    return true;
}

int main(int argc, char**argv) {
	try {

		Files::ConfigurationManager cfgMgr;
		THIRDEYE::Engine engine(cfgMgr);

		if (parseOptions(argc, argv, engine, cfgMgr)) {
			engine.go();

		}
	} catch (std::exception &e) {
		std::cout << "\nERROR: " << e.what() << std::endl;
		return (1);
	}

	return (0);
}

#if defined(_WIN32) && !defined(_CONSOLE)

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    return main(__argc, __argv);
}

#endif

