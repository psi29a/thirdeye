#ifndef COMPONENTS_FILES_WILDMIDICFG_HPP
#define COMPONENTS_FILES_WILDMIDICFG_HPP

#include <string>

/**
 * Single source of truth for locating wildmidi.cfg. Shared by the engine
 * (sound.cpp, at play-time) and the launcher (Music panel validation +
 * where the OPL-3 setup flow writes patches) so the two can never drift.
 */
namespace Files
{

/// Per-user app-data dir for Thirdeye, no trailing separator. Matches
/// SDL_GetPrefPath("Mindwerks", "Thirdeye") and Qt's AppDataLocation:
///   macOS:   $HOME/Library/Application Support/Mindwerks/Thirdeye
///   Linux:   $XDG_DATA_HOME|$HOME/.local/share + /Mindwerks/Thirdeye
///   Windows: %APPDATA%\Mindwerks\Thirdeye
/// Empty if the environment gives us no home.
std::string thirdeyeAppDataDir();

/// Where the launcher's OPL-3 setup writes the WildMIDI config:
/// thirdeyeAppDataDir() + "/patches/wildmidi.cfg". Empty if no home.
std::string appDataWildmidiCfg();

/// Locate wildmidi.cfg. Search order:
///   1. `override` — returned as-is when non-empty
///   2. THIRDEYE_WILDMIDI_CFG env var — returned as-is when set
///   3. appDataWildmidiCfg() — only if the file exists
///   4. platform system locations (freepats / distro WildMIDI) — only if
///      the file exists
/// Empty string if nothing is set up.
///
/// 1 and 2 are deliberately NOT existence-checked: both express explicit
/// user intent (--wildmidi-cfg flag, exported env var), and a typo there
/// should fail loudly in WildMidi_Init — not silently fall through to a
/// different soundfont the user never chose. Callers that prefer fallback
/// over failure (the launcher's status row) pre-check before passing.
std::string findWildmidiCfg(const std::string& override_ = {});

} // namespace Files

#endif // COMPONENTS_FILES_WILDMIDICFG_HPP
