#ifndef COMPONENTS_FILES_WILDMIDICFG_HPP
#define COMPONENTS_FILES_WILDMIDICFG_HPP

#include <string>

/**
 * Single source of truth for locating what WildMidi_Init should open. Shared
 * by the engine (sound.cpp, at play-time) and the launcher (Music panel
 * validation + where the OPL-3 setup writes the .sf2) so the two can never
 * drift.
 *
 * The returned path may be an .sf2 (rendered via TinySoundFont) or a legacy
 * WildMIDI cfg — WildMidi_Init since 0.5.0 dispatches on the file itself, so
 * callers pass the string through unchanged.
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

/// Where the launcher's OPL-3 setup writes the soundfont:
/// thirdeyeAppDataDir() + "/OPL-3_FM_128M.sf2". Empty if no home.
/// Existence-agnostic (naming convention only) — check with fs::exists.
std::string appDataOpl3Sf2();

/// Legacy WildMIDI config path (thirdeyeAppDataDir() + "/patches/wildmidi.cfg").
/// Still recognized in the search order for users with existing configs
/// (freepats, timidity, older Thirdeye installs). New setups use the .sf2.
std::string appDataWildmidiCfg();

/// Locate the music path WildMidi_Init should open. Search order:
///   1. `override` — returned as-is when non-empty
///   2. THIRDEYE_WILDMIDI_CFG env var — returned as-is when set
///   3. appDataOpl3Sf2() — only if the file exists (preferred: what the
///      launcher's OPL-3 setup writes)
///   4. appDataWildmidiCfg() — only if the file exists (legacy)
///   5. platform system locations (freepats / distro WildMIDI) — only if
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
