Thirdeye is from-scratch C++ reimplementation of SSI/Westwood's **AESOP** engine (AESOP/16 and AESOP/32), the bytecode VM that powers **Eye of the Beholder 3** (`EYE.RES`) and **Dungeon Hack** (`HACK.RES` + `OPEN.RES`). Written by John Miles in 1992.

**End goal:** play Eye of the Beholder 3 and Dungeon Hack natively from the original game data. You must own the original games — Thirdeye ships no game assets.

![Thirdeye](https://github.com/user-attachments/assets/77952869-864c-44c2-890d-029d7bf5c29d "Thirdeye")



Version: 0.90.0 (unreleased)  
License: GPL (see GPL3.txt for more information)  
Website:  http://www.mindwerks.net/projects/thirdeye/  

QUALITY-OF-LIFE ADDITIONS

Thirdeye runs the game's original bytecode unchanged, but the runtime around it
adds a few conveniences the DOS release never had. Saves stay DOS-compatible
on disk throughout.

* Automap: press M in-game for a top-down map of everything the party has seen
  (fog of war; secrets count as walls until found). Exploration persists per
  save slot. The original shipped without any map.
* Modern movement: WASD/QE alongside the original arrow keys and mouse, bound
  by physical key position so they land right on QWERTY/AZERTY/QWERTZ alike.
* Windowed play at any integer scale (--scale=N) instead of fullscreen VGA.
* Skip the wait: --skip-intro / --skip-menu / --load-save=N jump straight into
  the game, and the intro cinematic auto-plays only on first boot (still
  available from the menu any time).
* Launcher: picks the game folder, sets options, checks for updates -- and can
  download and install the game data for you.
* Music out of the box: a built-in OPL3 synth (authentic AdLib/SB16 timbres)
  -- no soundfont or patch setup required.
* Crash-safe saving: save files are staged and committed atomically as a set,
  so a crash or full disk mid-save can't corrupt an existing slot.

BUILDING

Thirdeye is built with CMake and a C++20 toolchain.

Dependencies
------------
Build tools:
* CMake >= 3.15
* A C++20 compiler (GCC 10+, Clang 12+, or MSVC 2019+)
* Ninja (recommended generator; any CMake generator works)
* Git (required: unit tests fetch GoogleTest via CMake FetchContent)

Libraries:
* SDL3          - windowing, input, audio output
* OpenAL        - digital sound mixer (openal-soft)
* WildMIDI      - XMIDI / MIDI playback

GoogleTest is NOT a system dependency: when unit tests are enabled (the
default) CMake downloads and builds it automatically via FetchContent, so an
internet connection is needed the first time you configure.

Installing dependencies
-----------------------
Linux (Debian/Ubuntu):

    sudo apt-get install -y cmake ninja-build build-essential \
        libsdl3-dev libopenal-dev libwildmidi-dev

macOS (Homebrew):

    brew install cmake ninja sdl3 openal-soft wildmidi

Windows (vcpkg):

    git clone https://github.com/microsoft/vcpkg.git
    .\vcpkg\bootstrap-vcpkg.bat
    .\vcpkg\vcpkg.exe install sdl3 openal-soft wildmidi --triplet x64-windows

Configure and build
-------------------
Linux / macOS:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Windows (from a Developer Command Prompt, using the vcpkg toolchain):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>\scripts\buildsystems\vcpkg.cmake
    cmake --build build

Useful CMake options (defaults in parentheses):

    -DUNIT_TESTS=ON|OFF      build the unit tests (ON)
    -DBUILD_DAESOP=ON|OFF    build the daesop RES tool / disassembler (ON)
    -DBUILD_ARC=ON|OFF       build the arc resource compiler (OFF)
    -DBUILD_LAUNCHER=ON|OFF  build the launcher front-end (OFF)
    -DDEBUG=ON|OFF           extra debug build options (OFF)

Tip: if you already have a configured build/ dir and want to flip an option,
re-run cmake on it (e.g. `cmake build -DUNIT_TESTS=OFF`) or delete
build/CMakeCache.txt.

RUNNING TESTS

Unit tests are built by default into the `runtests` executable (GoogleTest).

    cmake -S . -B build -G Ninja          # UNIT_TESTS is ON by default
    cmake --build build --target runtests
    ctest --test-dir build --output-on-failure   # or run the binary directly:

    # macOS (app bundle layout):
    ./build/thirdeye.app/Contents/MacOS/runtests
    # Linux / Windows:
    ./build/runtests

To configure without tests (also avoids the GoogleTest download):

    cmake -S . -B build -G Ninja -DUNIT_TESTS=OFF

CHANGELOG

0.90.0 (unreleased):
Dungeon Hack boots, renders and generates its own dungeons. MAZE.EXE -- the DOS
binary that builds a fresh dungeon for every new game -- is reimplemented
natively, so mazes, rooms, doors, keys, traps and monsters are generated
in-process.

* Dungeon Hack boots end to end (OPEN.RES -> `opening`, HACK.RES ->
  `phase-one`), following HACK.BAT's errorlevel chain. The game is auto-detected
  from the .RES file, so EOB3 and DH share one binary with no flags.
* DH rendering: the 3D view with real occlusion, the HUD, and the automap. DH
  composites each panel through an offscreen page; EOB3's flattened path is
  untouched (verified pixel-identical).
* MAZE.EXE ported natively, including its R250 PRNG -- every layout decision is
  a draw from that stream, so nothing else reproduces DH's dungeons. All five
  layout algorithms, plus the feature pass that places stairs, locks, traps,
  pits, teleporters, monsters and treasure from SETTINGS.DAT's FREQ_* values.
  Every lock gets a key no deeper in the maze than the lock, so the dungeon
  stays solvable. See docs/dungeon_hack_maze.md.
* DH file I/O: SETTINGS.DAT and PC.DAT round-trip byte-perfectly, so the
  Customization screen's settings and the chosen character persist. Seed 0 means
  "random", as in the original.
* Single-instance lock: two copies running against one game directory could
  corrupt a savegame. Set THIRDEYE_ALLOW_MULTI=1 to opt out.
* Fixed `roll_chance`, which always returned 0 -- it looked for its probability
  table in object statics, but both call sites pass a code-space address.
* Known limitation: DH gameplay still needs `THIRDEYE_BOOT=phase-two`.
  `phase-one` never returns the 0 that HACK.BAT needs to reach the game. EOB3 is
  unaffected.

0.89.0 (released 2026-07-13):
Save/load lands for real -- save at camp, quit, continue -- plus an automap,
a launcher that can install the game for you, and a live control channel that
lets a script (or an AI agent) play the game.

* Save/load: position, HP, inventory, memorized spells and consumed items all
  survive a save+load cycle. Save slots work from the camp menu; --load-save=N
  boots straight into a slot. Saves are staged and committed atomically, so a
  mid-save crash or an empty slot can't corrupt an existing save.
* Floor items now spawn: ITEMS.TMP's item stream is parsed with the correct
  native CDESC record framing. The old off-by-4 framing dropped the placement
  fields, so no on-the-ground loot had ever appeared; now it spawns, renders
  and can be picked up. Saves stay DOS-byte-compatible.
* Live control channel (macOS/Linux): THIRDEYE_CTL=<socket> exposes a line
  protocol to inject input and query game state, for scripted play and
  debugging. Zero cost when unset. See docs/control_channel.md.
* Automap (new): press M for a top-down map of what the party has seen (fog of
  war; secrets count as walls until found). Persisted per save slot; original
  EOB3 save files stay DOS-compatible.
* World items: niches, monster inventories, chests and containers now hold
  their contents; consumed items stay consumed across save+load.
* EOB2 party transfer (initial): TRANSFER.SAV staging + format validation.
* Launcher: can download and install the EOB3 game data (GOG or Internet
  Archive), with overwrite warnings; archive readers hardened.
* Rendering fixes: co-located doors + wall buttons both render (chain order
  now matches the original's init_level, guarded by a chain-invariant
  checker); VFX transparency uses the RLE skip token so spell-book text
  renders solid; transition dialogs no longer repaint the compass.
* Runtime: destroy_object releases its subwindows; --skip-intro only skips the
  boot auto-intro; the intro plays on first boot only.

0.88.0:
Thirdeye grows from "walks a populated dungeon" into a game you can actually fight,
loot and boot into from scratch -- and ships as a proper self-contained bundle on
Linux, macOS and Windows.

* Combat: monsters spot the party, close, attack; the party swings back with real
  weapons. The troll dies, the sword-wraith dies on contact, monsters drop loot and
  award XP, corpses decorate the tile, and monster sprites scale down over distance
  in the view. HUD autoattack button + autoattack timer wired to the runtime.
* Monster AI (port of MONSTER.C and friends): threat detection, path/facing update
  on the timer, engage/disengage; the previously-stubbed post/pass/pass_monster_events
  and companion CALLs implemented against John Miles' arun/runtime sources so the
  SOP-driven combat handlers all land.
* Item pickup and the hand cursor: clicking a dropped item in the dungeon view moves
  it into the cursor and inventory. The ethereal-miss/pickup issue traced to an
  uninitialised static the DOS loader had seeded (fixed in our loader, never the SOP).
* Runtime coverage: pixel_fade, do_dots, do_ice, getkey, BRK, and a batch of
  previously-stubbed CALLs implemented; unimplemented stubs mapped and named so the
  next round is a fill-in rather than a search.
* Cutscenes and level dressing: intro/level cutscene playback fixed; gravestones
  and other level impediments now load and render on the map.
* Chargen: initial chargen entry screen wired in, save-game read path works
  end-to-end, Cancel goes back to the title menu instead of shutting the engine.
  Reverse-engineered CHGEN.EXE / CHARCOPY.EXE (docs under eob3_research/).
* AESOP VM: zero-init autos on frame entry and permissive static OOB access to
  match DOS-AESOP semantics -- both load-bearing for combat and chargen.
* Rendering: fixed the wall-clip regression at the dungeon-view edge (bbox now
  caps at x=176 -- caught and locked down by the autowalk + BMP-diff harness);
  text-window wipes when needed; palette choice now driven by the SOP instead
  of guessed by us.
* Launcher (new): a Qt6 launcher app (thirdeye-launcher) picks the game folder,
  toggles skip-intro/skip-menu/nosound/debug, chooses render scale, checks GitHub
  for updates and self-launches the engine. Installs side-by-side with the engine
  on all three platforms.
* Self-contained music: WildMIDI's new Nuked-OPL3-fast synth is the default
  (@opl3 -- no patches or soundfont required, authentic SoundBlaster 16 / AdLib
  timbres). --wildmidi-cfg=<file> still accepts a .sf2/.cfg/.op2 override.
* Sound polish: OpenAL-soft device enumeration, follows the system default
  output when it changes, mouse cursor fixes, sound effects and music now
  play reliably on all three platforms.
* Packaging: proper distributables on every platform -- macOS .dmg (macdeployqt
  bundles Qt + engine deps), Windows NSIS installer (windeployqt + vcpkg-cached
  runtime DLLs), Linux .AppImage (linuxdeploy) and .deb. `cmake --build build
  --target dmg|nsis|appimage` for each.
* Platform upgrade: SDL2 -> SDL3 across the codebase; ccache + vcpkg cache in CI
  so pushes rebuild in seconds. GitHub Actions builds and runs unit tests on
  Linux, macOS and Windows every push.
* Regression harness: THIRDEYE_AUTOWALK / THIRDEYE_DUMP / THIRDEYE_RECORD to
  script/record/replay a session, plus BMP diffs to lock down clip regressions;
  ci-valgrind.sh replays a recorded walk under valgrind in Docker.
* Correctness sweep: Coverity rounds 5-9, ASan build, valgrind fixes, MSVC
  warnings cleaned up, security fixes across the RES loader / GFF reader / arc.
* daesop / arc: bytecode.def looked up next to the binary; arc_compat helper
  for arc; MSVC build works.


0.87.0:
Thirdeye grows from "plays the intro" into a SOP bytecode engine that boots Eye of
the Beholder III from the original game data, renders its interactive menu, and walks
a populated 3D dungeon -- all driven by the game's own AESOP bytecode.

* SOP virtual machine (new): a from-scratch port of the AESOP/32 bytecode dispatcher.
  All 88 opcodes (0x00-0x57) except BRK -- branches (BRT/BRF/BRA/CASE), arithmetic/
  logic/compare, constants, JSR/RTS procedures, RCRS/CALL runtime calls.
* Object & message system: class hierarchy resolved from each code object's N:PARENT,
  instances, SEND/PASS dispatch with THIS and parameter passing, per-instance static
  storage with base-class-first layout for inherited statics.
* Variable model: auto (local/param), static (object state), table (constant) and
  extern (cross-object) scalars + arrays; AIM/AIS array indexing; a unified tagged
  effective-address model (LECA/LEAA/LESA/LETA/LEXA across code/stack/static/extern).
* Cross-object link layer (port of RTLINK.C construct_thunk), resolved lazily + cached.
* Event system (port of EVENT.C): the FIFO event queue + notify list that are the
  engine's main loop -- notify/post_event/dispatch_event/drain/flush, app-vs-system
  event priority, destroy_object -> cancel_entity_requests.
* Windowing & region input (port of INTRFACE.C): assign_subwindow regions, mouse
  hit-testing, enter/leave/click region events, get_x1/y1/x2/y2.
* Event-driven host loop: SDL mouse/keyboard -> AESOP events, a coalesced ~30 Hz
  SYS_TIMER heartbeat, frame-paced presentation, yields when idle (~6% CPU).
* Runtime-function library wired to thirdeye's subsystems: set_palette, draw_bitmap
  (with the GIL2VFX X/Y mirror flag), refresh_window, color/light_fade,
  set_mouse_pointer, fill_rectangle, load_resource; text_window/text_style/text_color/
  text_xy/print/sprint (printf-style %d/%s with word-wrap + clipping); peekmem/pokemem;
  absv/minv/maxv; launch; create_program/create_object/destroy_object; step_X/Y/FDIR
  movement geometry; change_level.
* AESOP/16 "1.10" VFX shape-table bitmap decoder and "2." VFX font decoder -- EYE.RES
  art and fonts render natively (the formats daesop's -cob couldn't bridge).
* Boot: drives EOB3's `start` object through its peekmem(1264) "mode" state machine and
  renders the title menu ("Choose Your Destiny") entirely from SOP bytecode -- real art/
  palette/text, hover highlight, mouse + keyboard. Menu choices act via the AESOP program
  chain (Continue -> in-game; Abandon -> quit; Introduction -> cinematic; Gather a New
  Party -> char-gen), with INTRO.GFF cinematic playback.
* Char-gen party transfer: reverse-engineered CREATE.SAV (and the EOB1 14-byte ITEM.DAT
  format), so "Gather a New Party"/--skip-menu enters the game with the real default
  party (Bob/Carol/Ted/Alice) -- portraits, names, HP, ability scores and starting gear.
* Playable dungeon: the 3D view renders across all 14 levels (four tilesets derived from
  each map's area name -- Mausoleum/Forest/Ruins/Marble), with correct stone palettes and
  view-window clipping. Movement works end to end -- WASD/QE and arrow keys plus the
  on-screen compass arrows walk/turn/strafe with real maze collision; ESC/C open the camp
  menu. The whole level (doors, levers, stairs, decorations, monsters -- 231 in LVL01)
  loads from LVLnn.TMP and draws in the view.
* In-game HUD: dungeon view, four character panels (portrait + name + HP), the compass
  with a rotating N/E/S/W facing indicator, the movement compass-arrows, and CAMP.
  Clicking a portrait opens that character's equipment screen (paper-doll + real gear)
  and the character-stats screen.
* Keyboard bindings use physical SDL scancodes, so movement lands on the same keys under
  QWERTY/AZERTY/QWERTZ.
* daesop: fixed conversion of localized (Spanish) EOB3 -- issue #18. The old-bitmap RLE
  decoder read each span's X position as a single byte and tested the end-of-line flag as
  "== 0x80"; but X is a 16-bit little-endian value whose bit 15 is that flag, so any span
  starting at x >= 256 (e.g. the translated "Reward" bitmap 189, whose reflowed text
  begins at x=283) desynced the decoder ("rle_width=-21"). Now reads X as u16 and masks
  bit 15 as the flag. daesop also now finds bytecode.def beside its own binary; added
  daesop unit tests.
* Build & platform: ported to C++20, dropped the Boost dependency (std::ifstream/
  filesystem), builds with CMake + Ninja on Linux, macOS and Windows (SDL3, OpenAL,
  WildMIDI). GoogleTest + CLI11 are auto-fetched via FetchContent. GitHub Actions CI
  (replacing Travis) builds and runs the VM/resource/daesop unit tests on all three
  platforms; MSVC build + warning cleanups. Sound now enumerates devices and works on
  Windows as well as Linux/macOS. CLI: positional <game-data|.RES> plus --vm/--skip-menu/
  --skip-intro/--debug/--scale.

0.86.0:
* First official release of Thirdeye.
* Thirdeye can play back the intro sequence, with music and display the title menu.
* Found deleted scene in intro and added it back in.
* Picked up where Mirek Luza left off with daesop (0.85.0) in 2007.
* Picked up arc from John Miles circa 1993.
* Cleaned both daesop and arc to compile against GNU GCC and ported them to Linux.
* All code is now under GPLv3 license


0.85.0:
The version 0.850 improves the AESOP disassembler. The local variables and
parameters use now symbolic names. Also whenever the bytecode uses a direct
number which could possibly refer an existing resource, the corresponding
comment is added into the disassembly (of course in many cases this will be
a wrong guess - the number can be used for different purposes - but I still
think it will increase the readability of the disassembler). Some minor fixes
in the disassembly were made.


0.80.0:

The version 0.800 adds the command for patching of the converted EYE.RES from
the "Eye of Beholder 3" so that it does not crash when loading/saving
(there is a problem that the original code depends on the shape of 16 bit
pointers, minor fix is needed to make it work in AESOP/32 - the fix is done
in the code resource "menu" in the message handler "show"). This should make
the "Eye of Beholder 3" playable in the AESOP/32 (but more testing is needed).
I also added a command which makes patching of the EOB 3 and the conversion of
bitmaps/fonts to the AESOP/32 in one step (instead of using DAESOP three times).
But remember that another command is still needed to replace the resource 3
(see later).


0.75.0:

The version 0.750 adds support for converting "EOB 3 like" fonts. This means
that that all text is now shown inside the game, further increasing playability.
Beware that there are still some problems (e.g. I had crashes when wanting to
save game). I must investigate them.


0.70.0:

The version 0.700 adds support for converting "EOB 3 like" bitmaps. This means
that the "Eye of Beholder 3" is already partially usable in AESOP/32 (not
really playable - fonts need to be converted). Also a possibility to create
TBL files (for the "Dungeon Hack" engine) was added.


0.63.0:

The version 0.660 adds the command line options /r and /rh. This enable to
"replace" resources in an existing RES file (so it is possible to change
e.g. code/images/music/sound...). The replacement does not remove an old
resource physically but rather adds a new resource to the end of the file
and changes reference pointing to the old resource so that it points to the
new resource.


0.63.0:

The version 0.630 adds a usefull command line option /ir. It enables to show
more information about resources, their types and for string resources their
values.


0.60.0:

The version 0.600 adds a lot of new things into the disassembler introduced in
DAESOP 0.500. It concerns mainly variables. For most of variables (with
exception of local "auto" variables) symbolic names are used. When possible
(imported/exported variables), the real names are used. When it is not possible
(private static variables, "table" variables), simple symbolic names are made.
In future versions of DAESOP this will be done also for local variables.
The tables showing import/export resources were reworked and they now show
properly all available items. The problem of not disassembling procedures
(instructions JSR/RTS) was fixed. Various minor things were fixed/improved.


HISTORY

0.51  minor bug fixes (just making the disassembled code nicer)  
0.50  fourth release (including disassembler)  
0.40  third release: added more dumps, resolving names in export tables  
0.36  internal revision: added info about special/import/export/code resources  
0.35  internal revision (major rewriting, starting to show individual resource information)  
0.31  fixed syntax help  
0.30  second release including resource extraction (061017)  
0.25  internal version  
0.20  first release (061014)  
0.1x  initial versions (development)  
