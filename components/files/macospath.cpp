#include "macospath.hpp"

#if defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__)

#include <cstdlib>
#include <pwd.h>
#include <unistd.h>
#include <boost/filesystem/fstream.hpp>

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

boost::filesystem::path MacOsPath::getUserPath() const
{
    boost::filesystem::path userPath(".");

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
        userPath = boost::filesystem::path(theDir) / "Library/Preferences/";
    }

    return userPath / mName;
}

boost::filesystem::path MacOsPath::getGlobalPath() const
{
    boost::filesystem::path globalPath("/Library/Preferences/");
    return globalPath / mName;
}

boost::filesystem::path MacOsPath::getLocalPath() const
{
    return boost::filesystem::path("./");
}

boost::filesystem::path MacOsPath::getGlobalDataPath() const
{
    boost::filesystem::path globalDataPath("/Library/Application Support/");
    return globalDataPath / mName;
}

}


} /* namespace Files */

#endif /* defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__) */
