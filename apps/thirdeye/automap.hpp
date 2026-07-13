#ifndef THIRDEYE_AUTOMAP_HPP
#define THIRDEYE_AUTOMAP_HPP

// Automap overlay: press M in-game to view a top-down grid of the current
// dungeon level, fogged to what the party has actually seen. Not a pause --
// the SOP keeps ticking underneath. See docs/roadmap.md Phase 5.
//
// Data lives per-level (0..14) as a 32x32 bit-per-cell 'visited' map plus a
// matching 'notable' bitmap for cells that held an interactable or monster
// when we saw them. Persistence: MAPS.TMP alongside ITEMS.TMP, cloned to
// MAPS_nn.BIN on save_game (see runtime/eye.cpp save hooks).

#include <cstdint>
#include <filesystem>

namespace RESOURCES { class Resource; }
namespace GRAPHICS  { class Graphics; }
namespace VM        { class ObjectSystem; }

namespace THIRDEYE::automap {

// Per-pump refresh: reads kernel/dungeon statics, marks the party's field-of-
// view into the current level's visited bitset, and flags cells whose lvlobj
// planes hold interactables/monsters as 'notable'. Safe to call every tick;
// no-ops when the SOP hasn't instantiated dungeon+kernel yet.
void tick(VM::ObjectSystem &objects);

// Toggle the overlay. When open, render() draws each pump before present.
void toggle();
void close();
bool isOpen();

// Draw the overlay onto the presented frame. Suspends the Graphics backdrop
// tracking so the SOP's text-restore snapshot isn't corrupted.
void render(GRAPHICS::Graphics &gfx, RESOURCES::Resource &res);

// Serialize / deserialize the per-level bitsets. Slot 0 is the live MAPS.TMP.
bool saveTo(const std::filesystem::path &file);
bool loadFrom(const std::filesystem::path &file);
// Clear all state (new-game start).
void reset();

} // namespace THIRDEYE::automap

#endif // THIRDEYE_AUTOMAP_HPP
