#include "linuxpath.hpp"

#if defined(__linux__) || defined(__FreeBSD__)

#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <unistd.h>
#include <filesystem>

/**
 * \namespace Files
 */
namespace Files
{

LinuxPath::LinuxPath(const std::string& application_name)
    : mName(application_name)
{
}

std::filesystem::path LinuxPath::getUserPath() const
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
        userPath = std::filesystem::path(theDir);
    }

    return (userPath / ".config" / mName);
}

std::filesystem::path LinuxPath::getGlobalPath() const
{
    std::filesystem::path globalPath("/etc/");
    return (globalPath / mName);
}

std::filesystem::path LinuxPath::getLocalPath() const
{
    return (std::filesystem::path("./"));
}

std::filesystem::path LinuxPath::getGlobalDataPath() const
{
    std::filesystem::path globalDataPath("/usr/share/games/");
    return (globalDataPath / mName);
}

} /* namespace Files */

#endif /* defined(__linux__) || defined(__FreeBSD__) */
