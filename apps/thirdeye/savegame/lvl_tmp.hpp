#ifndef THIRDEYE_SAVEGAME_LVL_TMP_HPP
#define THIRDEYE_SAVEGAME_LVL_TMP_HPP

#include <cstdint>
#include <filesystem>

namespace RESOURCES { class Resource; }
namespace VM { class ObjectSystem; class EventSystem; }

namespace THIRDEYE::savegame {

// Populate the dungeon's lvlobj from a saved LVLnn.TMP: create each level object
// (door / lever / stairs / decoration / monster) as its SOP class and link it
// into the cell grid, so the dungeon's "draw objects"/"impedance" see them.
// This is the native restore_level_objects, reconstructed from the file format
// (see docs/eob3_savegame_format.md §3): a stream of variable-length records,
// each
//   +0 type/flag, +1 u16 id, +3 u16 CLASS (stored directly!), +11 x, +12 y,
//   +14 decflags.
// The class being in the record means no type->class table is needed. Must run
// AFTER the dungeon's "init level" (which clears lvlobj), so it's called on the
// first frame.
//
// Creature palette loading is driven by the SOP "enter level" cascade (see
// THIRDEYE::savegame::loadAreaInstances + runtime set_palette CALLs), so this
// helper no longer needs to collect monster classes for the caller.
//
// Destroy-before-restore mirrors RTOBJECT.C destroy_object: MSG_DESTROY plus
// cancel_entity_requests -- hence the EventSystem parameter (skipping the
// cancel leaked every replaced object's notify requests until the 768-slot
// list overflowed and late-loading monsters lost their AI notifies).
//
// Returns the number of objects placed.
int loadLevelObjects(int level, VM::ObjectSystem &objects,
                     VM::EventSystem &events, RESOURCES::Resource &res);

// Serialize object slots [first..last] in RTOBJECT.C save_range's SF_BIN
// format: a 0x1A byte, then for EVERY slot in the range a CDESC
// {u16 slot, u32 class, u16 size} followed by `size` bytes of raw instance
// statics (dead slot = class 0xFFFFFFFF, size 0). Byte-compatible with the
// shipped SAVEGAME/*_00.BIN files (verified against LVL01_00.BIN /
// ITEMS_00.BIN). ITEMS.TMP = [0..999], LVLnn.TMP = [1000..1999].
// Returns false on I/O failure.
bool saveRange(VM::ObjectSystem &objects, const std::filesystem::path &path,
               int first, int last);

} // namespace THIRDEYE::savegame

#endif // THIRDEYE_SAVEGAME_LVL_TMP_HPP
