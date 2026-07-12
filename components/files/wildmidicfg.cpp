#include "wildmidicfg.hpp"

#include <cstdlib>

namespace Files
{

std::string findWildmidiCfg(const std::string& override_)
{
    if (!override_.empty())
        return override_;
    if (const char* env = std::getenv("THIRDEYE_WILDMIDI_CFG"); env && *env)
        return env;
    return "@opl3";
}

} // namespace Files
