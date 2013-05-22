#include <SDL2/SDL.h> // temp

#include <iostream>

#include <components/files/configurationmanager.hpp>

#include <components/games/eob2.hpp> // temp

#if defined(_WIN32) && !defined(_CONSOLE)
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream_buffer.hpp>

// For OutputDebugString
#include <Windows.h>
// makes __argc and __argv available on windows
#include <cstdlib>

#endif

#include "config.hpp"

#include <boost/version.hpp>
/**
 * Workaround for problems with whitespaces in paths in older versions of Boost library
 */
#if (BOOST_VERSION <= 104600)
namespace boost

template<>
inline boost::filesystem::path lexical_cast<boost::filesystem::path, std::string>(const std::string& arg)
{
    return boost::filesystem::path(arg);
}

} /* namespace boost */
#endif /* (BOOST_VERSION <= 104600) */


/**
 * \brief Parses application command line and calls \ref Cfg::ConfigurationManager
 * to parse configuration files.
 *
 * Results are directly written to \ref Engine class.
 *
 * \retval true - Everything goes OK
 * \retval false - Error
 */
bool parseOptions (int argc, char** argv, bool engine, Files::ConfigurationManager& cfgMgr)
{
    // Create a local alias for brevity
    namespace bpo = boost::program_options;
    typedef std::vector<std::string> StringsVector;

    bpo::options_description desc("Syntax: openadd <options>\nAllowed options");

    desc.add_options()
        ("help", "print help message")
        ("version", "print version information and quit")
        ("game", bpo::value<std::string>()->default_value("eob2"),
            "set which game we want to play")

        ("game-data", bpo::value<std::string>()->default_value("/opt/eob2"),
            "set game data directory")

        ("start", bpo::value<std::string>()->default_value("LEVEL4"),
            "set starting location")

        ("debug", bpo::value<bool>()->implicit_value(true)
            ->default_value(false), "debug mode")

        ("nosound", bpo::value<bool>()->implicit_value(true)
            ->default_value(false), "disable all sounds")

        ("new-game", bpo::value<bool>()->implicit_value(true)
            ->default_value(false), "activate char gen/new game mechanics")

        ("fs-strict", bpo::value<bool>()->implicit_value(true)
            ->default_value(false), "strict file system handling (no case folding)")
        ;

    bpo::parsed_options valid_opts = bpo::command_line_parser(argc, argv)
        .options(desc).allow_unregistered().run();

    bpo::variables_map variables;

    // Runtime options override settings from all configs
    bpo::store(valid_opts, variables);
    bpo::notify(variables);

    cfgMgr.readConfiguration(variables, desc);

    bool run = true;

    if (variables.count ("help"))
    {
        std::cout << desc << std::endl;
        run = false;
    }

    if (variables.count ("version"))
    {
        std::cout << "OpenADD version " << OPENADD_VERSION << std::endl;
        run = false;
    }

    if (!run)
        return false;

    /*
    // startup-settings
	engine.setGame(variables["game"].as<std::string>());
	engine.setGameData(variables["game-data"].as<bool>());

	// other settings
	engine.setDebugMode(variables["debug"].as<bool>());
	engine.setSoundUsage(!variables["nosound"].as<bool>());
	*/

    return true;
}

int main(int argc, char**argv)
{
    try
    {

        Files::ConfigurationManager cfgMgr;
        //OMW::Engine engine(cfgMgr);

    	bool engine = true;

        if (parseOptions(argc, argv, engine, cfgMgr))
        {



            //engine.go();

            SDL_Init(SDL_INIT_VIDEO);
            SDL_Window* displayWindow;
            SDL_Renderer* displayRenderer;
            SDL_RendererInfo displayRendererInfo;
            SDL_Surface *screen;

            // Create the window where we will draw.
            displayWindow = SDL_CreateWindow("SDL_RenderClear",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            320, 200,
                            SDL_WINDOW_SHOWN);

            // We must call SDL_CreateRenderer in order for draw calls to affect this window.
            displayRenderer = SDL_CreateRenderer(displayWindow, -1, 0);
            SDL_GetRendererInfo(displayRenderer, &displayRendererInfo);

        	screen = SDL_CreateRGBSurface(0, 320, 200, 32, 0, 0, 0, 0);
        	SDL_SetColorKey( screen, SDL_TRUE, SDL_MapRGB(screen->format, 255, 0, 255) ); // set magenta as our transparent colour

        	//Blit to rendering surface that is to be turned into a texture
        	SDL_Rect rcSrc = { 0, 0, 320, 200 };
        	SDL_Rect rcDst = { 0, 0, 320, 200 };

        	// create a surfaces
        	SDL_Surface *images[256];
        	Games::gameInit(images);
        	SDL_BlitSurface(images[0], NULL, screen, NULL);

        	SDL_Texture *tex;
        	tex = SDL_CreateTextureFromSurface(displayRenderer, screen);

            // Clear the entire screen to our selected color.
            SDL_RenderClear(displayRenderer);

            // blit texture to display
            SDL_RenderCopy(displayRenderer, tex, NULL, NULL);

            // Up until now everything was drawn behind the scenes.
            // This will show the new, red contents of the window.
            SDL_RenderPresent(displayRenderer);

            SDL_Delay(5000);
            SDL_Quit();

            //printf("cpsByte %x\n", playfldImage[555]);

        	std::cout << "End of Data..." << std::endl;
        }
    }
    catch (std::exception &e)
    {
        std::cout << "\nERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

// Platform specific for Windows when there is no console built into the executable.
// Windows will call the WinMain function instead of main in this case, the normal
// main function is then called with the __argc and __argv parameters.
// In addition if it is a debug build it will redirect cout to the debug console in Visual Studio
#if defined(_WIN32) && !defined(_CONSOLE)

#if defined(_DEBUG)
class DebugOutput : public boost::iostreams::sink
{
public:
    std::streamsize write(const char *str, std::streamsize size)
    {
        // Make a copy for null termination
        std::string tmp (str, size);
        // Write string to Visual Studio Debug output
        OutputDebugString (tmp.c_str ());
        return size;
    }
};
#else
class Logger : public boost::iostreams::sink
{
public:
    Logger(std::ofstream &stream)
        : out(stream)
    {
    }

    std::streamsize write(const char *str, std::streamsize size)
    {
        out.write (str, size);
        out.flush();
        return size;
    }

private:
    std::ofstream &out;
};
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    std::streambuf* old_rdbuf = std::cout.rdbuf ();

    int ret = 0;
#if defined(_DEBUG)
    // Redirect cout to VS debug output when running in debug mode
    {
        boost::iostreams::stream_buffer<DebugOutput> sb;
        sb.open(DebugOutput());
#else
    // Redirect cout to openadd.log
    std::ofstream logfile ("openadd.log");
    {
        boost::iostreams::stream_buffer<Logger> sb;
        sb.open (Logger (logfile));
#endif
        std::cout.rdbuf (&sb);

        ret = main (__argc, __argv);

        std::cout.rdbuf(old_rdbuf);
    }
    return ret;
}

#endif

