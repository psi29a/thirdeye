Thirdeye: A reimplementation of the AESOP engine

Thirdeye is an attempt at recreating the AESOP engine for the popular
role-playing games Eye of the Beholder 3 and Dungeon Hack. You need to own and
install the original games for Thirdeye to work.

Version: 0.86.0  
License: GPL (see GPL3.txt for more information)  
Website:  http://www.mindwerks.net/projects/thirdeye/  

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
* SDL2          - windowing, input, audio output
* OpenAL        - digital sound mixer (openal-soft)
* WildMIDI      - XMIDI / MIDI playback

GoogleTest is NOT a system dependency: when unit tests are enabled (the
default) CMake downloads and builds it automatically via FetchContent, so an
internet connection is needed the first time you configure.

Installing dependencies
-----------------------
Linux (Debian/Ubuntu):
    sudo apt-get install -y cmake ninja-build build-essential \
        libsdl2-dev libopenal-dev libwildmidi-dev

macOS (Homebrew):
    brew install cmake ninja sdl2 openal-soft wildmidi

Windows (vcpkg):
    git clone https://github.com/microsoft/vcpkg.git
    .\vcpkg\bootstrap-vcpkg.bat
    .\vcpkg\vcpkg.exe install sdl2 openal-soft wildmidi --triplet x64-windows

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

INSTALLATION

Windows:
TODO

Linux:
Run either in place with generic tar balls or install distribution specific packages.

OSX:
TODO

CHANGELOG

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
