#include "windowspath.hpp"

#if defined(_WIN32) || defined(__WINDOWS__)

#include <cstring>

#include <windows.h>
#include <shlobj.h>
#include <Shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

/**
 * FIXME: Someone with Windows system should check this and correct if necessary
 */

/**
 * \namespace Files
 */
namespace Files
{

WindowsPath::WindowsPath(const std::string& application_name)
    : mName(application_name)
{
}

std::filesystem::path WindowsPath::getUserPath() const
{
    std::filesystem::path userPath(".");

    TCHAR path[MAX_PATH];
    memset(path, 0, sizeof(path));

    if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PERSONAL | CSIDL_FLAG_CREATE, NULL, 0, path)))
    {
        PathAppend(path, TEXT("My Games"));
        userPath = std::filesystem::path(path);
    }

    return userPath / mName;
}

std::filesystem::path WindowsPath::getGlobalPath() const
{
    std::filesystem::path globalPath(".");

    TCHAR path[MAX_PATH];
    memset(path, 0, sizeof(path));

    if(SUCCEEDED(SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES | CSIDL_FLAG_CREATE, NULL, 0, path)))
    {
        globalPath = std::filesystem::path(path);
    }

    return globalPath / mName;
}

std::filesystem::path WindowsPath::getLocalPath() const
{
    return std::filesystem::path("./");
}

std::filesystem::path WindowsPath::getGlobalDataPath() const
{
    return getGlobalPath();
}

} /* namespace Files */

#endif /* defined(_WIN32) || defined(__WINDOWS__) */
