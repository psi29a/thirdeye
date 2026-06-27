#ifndef THIRDEYE_CHARGEN_CHARGEN_SCREEN_HPP
#define THIRDEYE_CHARGEN_CHARGEN_SCREEN_HPP

// In-engine stand-in for CHGEN.EXE, the original DOS program that built a
// new party and wrote CHARGEN\CREATE.SAV. The bytecode hands control to us
// via `launch("chgen")` from the title menu's "Gather a New Party" option;
// the engine catches the relaunch and calls runChargenScreen() before
// re-entering `start` with mode CHGN (which then reads CREATE.SAV).
//
// Today this is a minimal entry screen: it loads the chargen palette +
// portrait sheet so the right pixels show up on screen, draws the four
// party slots and instructions, and falls through to the existing
// CREATE.SAV transfer when the user presses Play. The full per-slot flow
// (race -> class -> alignment -> stats -> portrait -> name, matching the
// dosbox_screenshots reference) is the next slice.

#include <filesystem>

namespace GRAPHICS { class Graphics; }

namespace THIRDEYE::chargen {

// Run the chargen entry screen. `chargenDir` is the path containing
// CHARPICS.BMP / PALETTE.COL / FONT6.FNT (alongside the game's .RES).
// Returns true if the user accepted (Play -> go into the game with the
// rolled CREATE.SAV), false if cancelled (Esc -> go back to title menu).
// Throws QuitRequested if the user closes the window. If the chargen
// assets are missing it logs a note and returns true (caller proceeds
// with whatever CREATE.SAV is already on disk).
bool runChargenScreen(GRAPHICS::Graphics &gfx,
                      const std::filesystem::path &chargenDir);

} // namespace THIRDEYE::chargen

#endif // THIRDEYE_CHARGEN_CHARGEN_SCREEN_HPP
