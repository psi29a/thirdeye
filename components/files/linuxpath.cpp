#include "linuxpath.hpp"

#if defined(__linux__) || defined(__FreeBSD__)

#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <unistd.h>
#include <boost/filesystem/fstream.hpp>

/**
 * \namespace Files
 */
namespace Files
{

LinuxPath::LinuxPath(const std::string& application_name)
    : mName(application_name)
{
}

boost::filesystem::path LinuxPath::getUserPath() const
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
        userPath = boost::filesystem::path(theDir);
    }

    return (userPath / ".config" / mName);
}

boost::filesystem::path LinuxPath::getGlobalPath() const
{
    boost::filesystem::path globalPath("/etc/");
    return (globalPath / mName);
}

boost::filesystem::path LinuxPath::getLocalPath() const
{
    return (boost::filesystem::path("./"));
}

boost::filesystem::path LinuxPath::getGlobalDataPath() const
{
    boost::filesystem::path globalDataPath("/usr/share/games/");
    return (globalDataPath / mName);
}

} /* namespace Files */

#endif /* defined(__linux__) || defined(__FreeBSD__) */
