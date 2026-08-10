#ifndef THIRDEYE_RUNTIME_INTERNAL_HPP
#define THIRDEYE_RUNTIME_INTERNAL_HPP

// Shared scaffolding for apps/thirdeye/runtime/. Each *.cpp file in this dir
// mirrors one of ../eob3_research/runtime/*.C and exports one `tryHandle`
// entry; engine.cpp's `defaultRuntimeCall` chains them.
//
// What's where:
//   rtcode.cpp    -- math/random/peekmem (RTCODE.C)
//   event.cpp     -- event queue + dispatch_event host pump (EVENT.C)
//   rtobject.cpp  -- create_program/create_object/destroy_object (RTOBJECT.C)
//   eye.cpp       -- EOB3-specific glue: transfer, step, change_level (EYE.C)
//   graphics.cpp  -- windowing, draw, text (GRAPHICS.C / INTRFACE.C)
//
// Globals (`gXxx`) are owned by engine.cpp; we declare them extern here so the
// category files can share the trace/perf/clip/compass state that crosses cuts.

#include "../vm/vm.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace RESOURCES { class Resource; }
namespace GRAPHICS { class Graphics; }
namespace MIXER { class Mixer; }
namespace VM { class ObjectSystem; class EventSystem; }
namespace THIRDEYE::savegame { struct TransferState; }

namespace THIRDEYE::runtime {

// Thrown by pumpHost to unwind the SOP main loop on quit/ESC. Caught in
// bootObject (engine.cpp). `reason` is empty for an actual window-close/ESC;
// non-window terminal conditions (e.g. an unknown launch() target) set it so
// the catch site doesn't misreport them as "Window closed".
struct QuitRequested { std::string reason; };

// Thrown by `launch` to model AESOP's program chain. In the original, launch()
// exec-replaces the process with a sub-program (cine.exe/chgen.exe); when that
// finishes it chain-launches "aesop eye start" again, which re-reads the mode
// the bytecode pokemem'd into cell 1264 and routes on it. We can't exec, so
// launch() unwinds the VM back to bootObject, which runs our internal
// equivalent of the named program and then re-enters start.MSG_CREATE -- same
// effect.
struct Relaunch {
    std::string program;              // launch()'s program name ("CINE.EXE", ...)
    std::vector<std::string> extras;  // subsequent string args, e.g. GFF filename
                                      // ("INTRO.GFF" for the title-menu intro,
                                      // "FINALE.GFF" for the quit farewell, ...)
};

// Per-call references handed to every category. These come from the engine's
// call site; the host TU owns the storage. Reference members make copy/move
// assignment implicitly deleted -- the explicit deletions below silence
// MSVC C5027 and document intent. Aggregate init was the old call style;
// we keep an explicit constructor so callers can still write Context{...}.
struct Context {
	VM::ObjectSystem &objects;
	VM::EventSystem &events;
	GRAPHICS::Graphics *gfx;     // null in headless mode
	RESOURCES::Resource &res;
	std::map<int32_t, int32_t> &mem;
	savegame::TransferState &xfer;
	VM::Interpreter &vm;
	MIXER::Mixer *mixer;         // null in headless mode / non-game paths

	Context(VM::ObjectSystem &objects_, VM::EventSystem &events_,
	        GRAPHICS::Graphics *gfx_, RESOURCES::Resource &res_,
	        std::map<int32_t, int32_t> &mem_, savegame::TransferState &xfer_,
	        VM::Interpreter &vm_, MIXER::Mixer *mixer_ = nullptr)
	    : objects(objects_), events(events_), gfx(gfx_), res(res_),
	      mem(mem_), xfer(xfer_), vm(vm_), mixer(mixer_) {}

	Context(const Context &) = default;
	Context(Context &&) = default;
	Context &operator=(const Context &) = delete;
	Context &operator=(Context &&) = delete;
};

// --- Engine-owned globals (defined in engine.cpp) -------------------------

// Runtime-call trace gate. Set from --debug before the VM runs; per-call
// logging routes through rt() so it's a no-op when off.
extern bool gRtTrace;

// True when the loaded resource is Dungeon Hack (HACK.RES / OPEN.RES). DH
// shares the AESOP runtime with EOB3 but differs in a few table constants --
// currently the palette-region bases (see set_palette in runtime/graphics.cpp).
extern bool gDungeonHack;

// Set when the bytecode redraws the compass facing indicator (draw_bitmap to
// page 104, resource 187, on a turn); consumed by the next compass-page
// refresh to re-snapshot. See Graphics::snapshotCompass/restoreCompass.
extern bool gCompassDirty;

// THIRDEYE_PERF=1: per-present timing instrument. Counts draw_bitmap calls
// since the last present, sums their wall-clock cost, and logs the gap from
// the previous present.
extern bool gPerf;
extern int gDrawCount;
extern long gDrawNanos;
extern std::chrono::steady_clock::time_point gLastPresent;

// (No globals here: per-cell view narrowing is held in the events.windowRect
// table -- set_x1/x2/y1/y2 mutate the pane's edges via setWindowEdge, and
// draw_bitmap reads them back via windowRect. Mirrors GIL2VFX's panes[wnd].)

// Set at go(); for timing prints.
extern std::chrono::steady_clock::time_point gBootStart;
extern bool gFirstPresentLogged;

// Gated trace stream (no-op unless gRtTrace).
std::ostream &rt();

// Resolve a tagged Static/Extern address (as produced by LESA/LEXA for "pass
// this array to the runtime") to a bounds-checked byte pointer into the owning
// object's statics. Returns nullptr for non-static addresses or OOB ranges.
// Mirrors the original's flat far pointers into SOP instance memory.
uint8_t *staticBytePtr(Context &ctx, VM::Value addr, uint32_t size);

// The host seam (see CLAUDE.md). Called by event.cpp's dispatch_event /
// peek_event. Pumps SDL input into AESOP events, presents the frame, yields
// the CPU when the queue is idle. Throws QuitRequested on window-close/ESC.
// objects + res are needed by the automap overlay -- tick() reads party pose
// and lvlmap; render() reads FONT8 -- so we plumb them through the seam
// rather than smuggling globals.
void pumpHost(GRAPHICS::Graphics &gfx, VM::EventSystem &events,
              VM::ObjectSystem &objects, RESOURCES::Resource &res);

// True while any UI screen is up instead of the 3D adventure view: the
// kernel's W:current_screen@265 (spell book, character sheet, ...) or the
// camp object's active/outtake/selecting flags (camp menu, cutscene outtake
// boxes, level-transition decision dialogs). Mirrors the gate the SOP's own
// auto-attack handler uses. Drives host-side suppression of adventure-only
// chrome (compass restamp/draw, WASDQE movement translation, automap M key).
bool uiScreenActive(VM::ObjectSystem &objects);

// printf-style substitution shared by print()/sprint(): fill %d/%u/%i, %c,
// %s and %% in `fmt` from `args[start..]`. Per GRAPHICS.C vsprint, %s args
// are string-resource numbers ("S:"-tagged), %a args are byte pointers;
// tagged VM addresses also read as strings for %s.
std::string formatSop(const std::string &fmt, const std::vector<VM::Value> &args,
                      size_t start, Context &ctx);

// --- Category dispatch entries -------------------------------------------
//
// Each returns true iff `fn` matches a name this category owns (whether the
// handler succeeded or no-op'd); on true, `result` is the return value.

namespace rtcode   { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace event    { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace rtobject { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace eye      { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace graphics { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace sound    { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result); }
namespace dh       { bool tryHandle(Context&, const std::string &fn,
                                    const std::vector<VM::Value>&,
                                    VM::Value &result);
                     // Called once per DH boot (from engine.cpp when
                     // HACK.RES is being loaded). Writes structurally
                     // valid empty savegame/LEVELS.DAT / FEA*.DAT /
                     // ITEMS.DAT if they don't exist, so phase-two
                     // consumes zero-content dungeons instead of
                     // tripping on missing files. Idempotent.
                     void ensureSavegameFiles(
                         const std::filesystem::path &dhRoot);

                     // MAZE.EXE's PRNG: R250 (lagged-Fibonacci XOR, lags
                     // 250/103), ported from segment 1766. Exposed only so
                     // the unit tests can assert the lag invariant -- the
                     // dungeon generator is the sole production caller.
                     // Implementation + provenance in dh_maze.cpp.
                     class R250 {
                     public:
                         explicit R250(uint32_t seed);
                         uint16_t next();           // 1766:006e
                         int range(int lo, int hi);  // 1766:00c1
                         int roll(int n, int sides, int bonus);  // 1766:00dd
                     private:
                         uint16_t mState[250] = {};
                         int mIndex = 0;
                     };

                     // Per-level results of a generateDungeon() run --
                     // MAZE's 16-byte level descriptor, named.
                     struct LevelInfo {
                         uint8_t zone = 1;  // 0..4 -- which layout algorithm
                         bool water = false;
                         int entryRow = 0, entryCol = 0;  // party arrival cell
                         int fdir = 0;                    // 0=N 1=E 2=S 3=W
                         int stairRow = 0, stairCol = 0;  // the down-staircase
                         // The cell in front of the staircase, and the facing
                         // there: what the next level's stairs-up record aims
                         // back at.
                         int stairFrontRow = 0, stairFrontCol = 0;
                         int stairFdir = 0;
                         bool stairFdirValid = false;
                         int regionCount = 0;
                     };

                     // 1325:121d's 9-byte feature record, and 1325:11af's
                     // 5-byte item record, before the file writers permute
                     // them. `type` indexes MAZE's own name tables (feature
                     // 1..30, item 1..12) -- see dh_research/MAZE/FEATURES.md.
                     struct FeatureRecord {
                         uint8_t y, x, level, mask, type;
                         uint16_t p2, p3;   // type-specific; 0xFFFF = unset
                     };
                     struct ItemRecord {
                         uint8_t y, x, level, type, aux;
                     };
                     struct MagicZone {
                         bool present = false;
                         uint8_t x = 0, y = 0, w = 0, h = 0, kind = 0;
                     };

                     struct DungeonOut {
                         // The seed actually used. Differs from the one passed
                         // in only when that was 0, which means "roll one".
                         uint32_t seedUsed = 0;
                         std::vector<LevelInfo> info;
                         std::vector<std::vector<FeatureRecord>> features;
                         std::vector<MagicZone> zones;    // one per level
                         std::vector<ItemRecord> items;   // whole dungeon
                     };

                     // Generate `levels` consecutive 0x400-byte tile chunks
                     // into `chunks`, the way MAZE.EXE does: zones decided up
                     // front from `seed`, each level generated from
                     // `seed + level + 1`, then the feature-placement tail.
                     // `settings` is SETTINGS.DAT's 12-byte struct (the bytes
                     // after the u32 seed). Chunks come out in the on-disk
                     // encoding (0xFF = floor, 0x00/0x01 = wall).
                     void generateDungeon(uint8_t *chunks, int levels,
                                          uint32_t seed,
                                          const uint8_t *settings,
                                          DungeonOut &out);

                     // Serialise what generateDungeon produced, in MAZE's
                     // on-disk layouts.
                     std::vector<uint8_t> packFeatureFile(const DungeonOut &d,
                                                          int level);
                     std::vector<uint8_t> packItemFile(const DungeonOut &d); }

} // namespace THIRDEYE::runtime

#endif // THIRDEYE_RUNTIME_INTERNAL_HPP
