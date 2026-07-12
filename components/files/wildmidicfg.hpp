#ifndef COMPONENTS_FILES_WILDMIDICFG_HPP
#define COMPONENTS_FILES_WILDMIDICFG_HPP

#include <string>

namespace Files
{

/// Resolve what WildMidi_Init should open. Search order:
///   1. `override` — returned as-is when non-empty (from --wildmidi-cfg)
///   2. THIRDEYE_WILDMIDI_CFG env var — returned as-is when set
///   3. "@opl3" — WildMIDI's built-in Nuked-OPL3-fast synth (no data files)
///
/// 1 and 2 express explicit user intent; a typo there should fail loudly in
/// WildMidi_Init rather than silently fall through to OPL3.
std::string findWildmidiCfg(const std::string& override_ = {});

} // namespace Files

#endif // COMPONENTS_FILES_WILDMIDICFG_HPP
