#include "macospath.hpp"

#if defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__)

#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <filesystem>

/**
 * FIXME: Someone with MacOS system should check this and correct if necessary
 */

/**
 * \namespace Files
 */
namespace Files
{

MacOsPath::MacOsPath(const std::string& application_name)
    : mName(application_name)
{
}

std::filesystem::path MacOsPath::getUserPath() const
{
    std::filesystem::path userPath(".");

    const char* theDir = getenv("HOME");
    if (theDir == NULL)
    {
        struct passwd* pwd = getpwuid(getuid());
        if (pwd != NULL)
        {
            theDir = pwd->pw_dir;
        }
    }
    if (theDir != NULL)
    {
        userPath = std::filesystem::path(theDir) / "Library/Preferences/";
    }

    return userPath / mName;
}

std::filesystem::path MacOsPath::getGlobalPath() const
{
    std::filesystem::path globalPath("/Library/Preferences/");
    return globalPath / mName;
}

std::filesystem::path MacOsPath::getLocalPath() const
{
    return std::filesystem::path("./");
}

std::filesystem::path MacOsPath::getGlobalDataPath() const
{
    std::filesystem::path globalDataPath("/Library/Application Support/");
    return globalDataPath / mName;
}

} /* namespace Files */

#endif /* defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__) */
