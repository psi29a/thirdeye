#include "wildmidicfg.hpp"

#include <cstdlib>
#include <filesystem>

namespace
{

std::string envOrEmpty(const char* name)
{
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string();
}

} // namespace

namespace Files
{

std::string thirdeyeAppDataDir()
{
#if defined(_WIN32)
    std::string base = envOrEmpty("APPDATA");
    if (base.empty())
        return {};
    return base + "\\Mindwerks\\Thirdeye";
#elif defined(__APPLE__)
    std::string home = envOrEmpty("HOME");
    if (home.empty())
        return {};
    return home + "/Library/Application Support/Mindwerks/Thirdeye";
#else
    std::string base = envOrEmpty("XDG_DATA_HOME");
    if (base.empty())
    {
        std::string home = envOrEmpty("HOME");
        if (home.empty())
            return {};
        base = home + "/.local/share";
    }
    return base + "/Mindwerks/Thirdeye";
#endif
}

std::string appDataWildmidiCfg()
{
    std::string dir = thirdeyeAppDataDir();
    if (dir.empty())
        return {};
#if defined(_WIN32)
    return dir + "\\patches\\wildmidi.cfg";
#else
    return dir + "/patches/wildmidi.cfg";
#endif
}

std::string findWildmidiCfg(const std::string& override_)
{
    namespace fs = std::filesystem;
    std::error_code ec;

    if (!override_.empty())
        return override_;

    if (std::string env = envOrEmpty("THIRDEYE_WILDMIDI_CFG"); !env.empty())
        return env;

    if (std::string appdata = appDataWildmidiCfg();
        !appdata.empty() && fs::exists(appdata, ec))
        return appdata;

    static const char* kSystemDefaults[] = {
#if defined(__APPLE__)
        "/opt/homebrew/etc/wildmidi/wildmidi.cfg",
        "/usr/local/etc/wildmidi/wildmidi.cfg",
#elif defined(_WIN32)
        // Windows has no standard system location — app-data via the
        // launcher is the only path.
#else
        "/etc/wildmidi/wildmidi.cfg",
        "/usr/share/wildmidi/wildmidi.cfg",
#endif
        nullptr // keeps the array non-empty on Windows (MSVC C2466)
    };
    for (const char* p : kSystemDefaults)
    {
        if (p && fs::exists(p, ec))
            return p;
    }
    return {};
}

} // namespace Files
