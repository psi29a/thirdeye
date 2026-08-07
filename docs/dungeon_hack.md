# Dungeon Hack — extraction notes

## Summary
This document records the steps taken to obtain and extract the Dungeon Hack (USA) distribution from the Internet Archive item "DungeonHackUSA", and the final contents obtained after extracting ARJ archives found inside the ISO.

## What was downloaded
- Source: https://archive.org/download/DungeonHackUSA
- File downloaded: "Dungeon Hack (USA).zip" (saved locally as /tmp/dungeonhack.zip)
- The ZIP contained two files of interest:
  - "Dungeon Hack (USA).cue"
  - "Dungeon Hack (USA).bin"

## Steps performed
1. Downloaded the ZIP from Archive.org and extracted the .cue and .bin into the repository workspace under extracted/DungeonHackUSA/.
   - CUE contents: referenced the BIN (MODE1/2352, INDEX 01 00:00:00).
   - BIN SHA-1: 510dfc5038bb17671be5722b9e676de4044d604b

2. Copied the ZIP, the .cue and the .bin to ~/Downloads for convenience:
   - ~/Downloads/dungeonhack.zip
   - ~/Downloads/Dungeon Hack (USA).cue
   - ~/Downloads/Dungeon Hack (USA).bin

3. Extracted the MODE1 payload from the BIN (each 2352-byte sector contains 2048 bytes of user data starting at offset 16). The 2048-byte payloads were concatenated to form an ISO at:
   - ~/Downloads/DungeonHackUSA.iso
   - ISO SHA-1: 56b2ff66f4326f789653616ea38116eb146ef6f4
   - Sectors written: 9226

4. Extracted the ISO contents into:
   - ~/Downloads/HACK_ISO
   Method used: 7z x DungeonHackUSA.iso -o~/Downloads/HACK_ISO
   (script tries 7z first, falls back to mounting the ISO on macOS if 7z isn't available)

5. Located ARJ archives inside the ISO and extracted them into:
   - ~/Downloads/HACK
   ARJ files discovered in ISO extraction:
   - DATA1.ARJ
   - DEMO1.ARJ
   - DEMO2.ARJ

## Tools used
- curl (download)
- unzip (to inspect ZIP contents and extract the CUE/BIN)
- Python (to split 2352-byte sectors and write 2048-byte payloads into an ISO)
- 7z (p7zip) when available for ISO and ARJ extraction; fallback: hdiutil (macOS) + rsync, or unar/arj if available
- xxd / hexdump for previews

## Files created on disk (key paths)
- /tmp/dungeonhack.zip (downloaded ZIP)
- extracted/DungeonHackUSA/Dungeon Hack (USA).cue
- extracted/DungeonHackUSA/Dungeon Hack (USA).bin
- ~/Downloads/dungeonhack.zip
- ~/Downloads/Dungeon Hack (USA).cue
- ~/Downloads/Dungeon Hack (USA).bin
- ~/Downloads/DungeonHackUSA.iso
- ~/Downloads/HACK_ISO/  (ISO contents)
- ~/Downloads/HACK/      (ARJ-extracted final files)

## ISO top-level files (from ISO extraction)
1. DEARJ.EXE
2. DATA1.ARJ
3. DEMO1.ARJ
4. INSTALL.EXE
5. DEMO2.ARJ
6. DATA1.NFO
7. DATA2.NFO
8. DATA3.NFO

## ARJ archives found (absolute paths)
1. /Users/bret.curtis/Downloads/HACK_ISO/DATA1.ARJ
2. /Users/bret.curtis/Downloads/HACK_ISO/DEMO1.ARJ
3. /Users/bret.curtis/Downloads/HACK_ISO/DEMO2.ARJ

## Contents of the final extraction from the ARJ files
(These files were extracted into ~/Downloads/HACK — paths are relative to that directory.)

--- extracted files (first 200 entries) ---
DRV/A32PASDG.DLL
DRV/A32ARXM.DLL
DRV/A32ADLIB.DLL
DRV/A32PASFM.DLL
DRV/A32TANDY.DLL
DRV/A32MT32.DLL
DRV/A32ARDG.DLL
DRV/A32PASOP.DLL
DRV/A32SP1FM.DLL
DRV/A32SBDG.DLL
DRV/VFXSCAN.DLL
DRV/A32SBPDG.DLL
DRV/A32SBFM.DLL
DRV/A32SPKR.DLL
DRV/A32SP2FM.DLL
DRV/A32ALGDG.DLL
DRV/A32ALGFM.DLL
DRV/VESA480.DLL
INIT
HACK.BAT
OAK/README.DOC
OAK/67VESA.COM
OAK/OAK.ZIP
OAK/37VESA.COM
STDPATCH.AD
AESOP.EXE
DOS4GW.EXE
MAZE.EXE
PARADISE/VESA.EXE
PARADISE/READ.ME
SBLASTER.COM
LEVELS.DAT
ART/SKY-5.PCX
ART/TITLE.LBM
ART/SKY-4.PCX
ART/OUTSIDE.LBM
ART/SKY-6.PCX
ART/SKY-7.PCX
ART/SKY-3.PCX
ART/SKY-2.PCX
ART/SKY-1.PCX
ART/DEMO0003.LBM
ART/DEMO0002.LBM
ART/DEMO0000.LBM
ART/DEMO0001.LBM
ART/DEMO0005.LBM
ART/DEMO0004.LBM
ART/DEMO0006.LBM
G.BAT
CODE.1
README.BAT
EVEREX/VESA.COM
EVEREX/VESAOFF.COM
EVEREX/README.DOC
EVEREX/VESA.DOC
EVEREX/EVRXVESA.COM
EVEREX/VESAON.COM
EVEREX/EVRXVESA.OUT
CHARS
DEMOGNBG.EXE
GRAPH.INI
C&T/SETVESA.EXE
C&T/V452.BAT
C&T/VESA452.ASM
C&T/VESA452.INC
C&T/VESA451.COM
C&T/VESA452.COM
C&T/VTEST.EXE
C&T/README.TXT
C&T/VESA.INC
RES4
RES3
T
RES2
STB/READ.ME
STB/STB-VESA.COM
RES5
VIDEO7/VESALIST.EXE
VIDEO7/V7VESA.COM
SAMPLE.AD
SIGMA/READ.ME
SIGMA/SIGVESA.COM
ADLIB.ADV
AUDIO/STDPATCH.AD
AUDIO/S_CLOCK.XMI
AUDIO/ST1_GR1.XMI
AUDIO/ST1_GR0.XMI
AUDIO/ST1_GR2.XMI
AUDIO/ST1_GR3.XMI
AUDIO/ERROR.MSG
CHECKSYS.EXE
COLORS
TRIDENT/VESA.EXE
TRIDENT/TVGAVESA.DOC
OPEN.RES
A32SBDG.DLL
SBDIG.ADV
MOUSE.DAT
HACK.TBL
PCSPKR.ADV
TECMAR/VGAVESA.COM
TECMAR/VESATEST.EXE
TECMAR/VESATEST.C
A32SBFM.DLL
GENOA/VESAMODE.EXE
GENOA/VESA.COM
GENOA/READ.ME
GENOA/VESABOX.EXE
GENOA/GENBOX.PAS
GENOA/GENBOX.EXE
GENOA/VESABOX.PAS
README.TXT
CODE.2
APPIAN/APVESA.EXE
RLOFTSR.EXE
NEWCHAR
MT32MPU.ADV
ATI/VVESA.COM
CODE.3
OPEN.TBL
SOUND.EXE
HACK.RES
SAVEGAME/SETTINGS.DAT
SAVEGAME/NEWSCORE.EXE
SAVEGAME/VISIBLE.DAT
SAVEGAME/PC.DAT
SAVEGAME/HISCORE.DEF
SAVEGAME/HISCORE.DAT
SAVEGAME/SAVEGAME.DIR
SAVEGAME/SETSAVE.DAT
ORCHID/ORCHDVSA.COM
ORCHID/ORCHDVSA.DOC
SAVE/TEST2.VCR
SAVE/TEST1.VCR
DATA/3DSPRITE.SHP
DATA/3DSHIPS.SHP
RES0
CIRRUS/CRUSVESA.COM
CIRRUS/README.TXT
RES7
RES9
RES10
SINTAB
RES8
RES6
RES1

(If you need the complete tree instead of the first 200 entries, run: find ~/Downloads/HACK -type f | sed 's|^~/Downloads/HACK/||')

## Notes & next actions
- I used 7z for ISO and ARJ extraction; if you'd prefer mounting the ISO and inspecting it interactively, hdiutil attach ~/Downloads/DungeonHackUSA.iso will mount the image on macOS.
- If you want a CUE pointing to the generated ISO instead of the BIN, add a small cue file like:

  FILE "DungeonHackUSA.iso" BINARY
    TRACK 01 MODE1/2048
      INDEX 01 00:00:00

- The final extracted directory (~/Downloads/HACK) contains the game executables, data files, drivers, and art assets needed to run the DOS game (under DOSBox or similar).

If you want, next can:
- Produce a tar.gz of ~/Downloads/HACK for transport
- Create a mountable .img with the CUE referencing the ISO
- Run a quick DOSBox test script to verify the demo/installation runs

---

## Boot Architecture (AESOP/Thirdeye Integration)

### DOS Launcher (HACK.BAT)

The DOS batch file `HACK.BAT` reveals the correct boot sequence:

```batch
@echo off
set F=0

checksys 56 640              # System check: CPU 386+, 640KB RAM minimum
if ERRORLEVEL 1 goto EXIT

if exist savegame\settings.dat goto CHECKDEMO
md savegame                   # Create save directory if needed

:CHECKDEMO
if NOT exist OPEN.RES goto CONTINUE
aesop open opening            # BOOT: Load OPEN.RES, run "opening" object

:CONTINUE
aesop hack phase-one          # Load HACK.RES, run "phase-one" handler
set F=1
if ERRORLEVEL 3 goto CONTINUE
if ERRORLEVEL 2 goto CHECKDEMO
if ERRORLEVEL 1 goto EXIT
cd savegame
..\maze %1 %2                 # Run MAZE.EXE (random dungeon generator)
cd ..
if ERRORLEVEL 1 goto EXIT

aesop hack phase-two          # Load HACK.RES, run "phase-two" -- the game itself
if ERRORLEVEL 1 goto EXIT
goto CONTINUE
```

#### Boot Sequence Summary

1. **System check**: CHECKSYS.EXE verifies 386+ CPU and 640KB RAM
2. **Create save directory**: mkdir savegame (if first run)
3. **Load OPEN.RES** (if present):
   - Command: `aesop open opening`
   - This boots the "opening" object from OPEN.RES (the intro/menu handler)
   - OPEN.RES is optional (if not present, skips to HACK.RES directly)
4. **Load HACK.RES**:
   - Command: `aesop hack phase-one`
   - Runs the "phase-one" handler from HACK.RES (initializes game state)
   - Returns error code to control flow:
     - 0: continue
     - 1: exit
     - 2: restart from CHECKDEMO (menu)
     - 3+: loop CONTINUE
5. **Generate the dungeon**: MAZE.EXE writes LEVELS.DAT / FEA%02d.DAT /
   ITEMS.DAT into `savegame/`
6. **Play**: `aesop hack phase-two`
   - Runs the "phase-two" handler from HACK.RES -- this is the game loop,
     and it consumes what MAZE just generated

#### AESOP Command Semantics

The batch uses `aesop <res-basename> <object-name>` which implies:
- `aesop` is a launcher that loads a RES file and instantiates an object
- The RES file is found as `<res-basename>.RES` (e.g., `open` → `OPEN.RES`, `hack` → `HACK.RES`)
- The `<object-name>` is the boot object to instantiate and run
- The `AESOP.EXE` in the HACK directory is likely the launcher executable

---

### AESOP RES Structure for Dungeon Hack

#### OPEN.RES (1.5 MB)

**Boot object**: `"opening"` (resource 154)

**Export dictionary**:
- Object name: "opening"
- Exported variables: "L:tick", "L:_update"
- Message handlers:
  - M:0 "create" (offset 1002) — initialization
  - M:1 "destroy" (offset 1112) — cleanup
  - M:3 "timer tick" (offset 142)
  - M:5 "draw current screen" (offset 362)
  - M:6 "draw background" (offset 282)
  - M:9 "shutdown" (offset 467)
  - M:10 "next scene" (offset 508)
  - M:11 "Run Scene 0" (offset 552)
  - M:12 "Run Scene 1" (offset 771)

**Purpose**: Displays opening cinematic, menu screens, and transitions. If OPEN.RES exists, it's the entry point before HACK.RES.

#### HACK.RES (6.8 MB)

**Boot objects**: At minimum, "phase-one" and "phase-two" handlers must exist

**Known objects**:
- `kernel` (resource 1972) — core game logic, party management, message routing
  - Creates 4 infrastructure objects during initialization:
    - "bare hands" (2118)
    - "bracers of archery" (2274)
    - "floor pit" (2864)
    - "current stairs up" (2867)
    - "current stairs down" (2870)
  - Message handlers: move, step, enter game, combat, spell management, etc.

**Purpose**: Game content, levels, NPCs, items, gameplay state machine.

---

### Implications for Thirdeye

1. **Boot order**: Load OPEN.RES first (if present), then HACK.RES
2. **Boot object**: Look for "opening" in OPEN.RES (not "start")
3. **Multi-RES support**: Engine must support loading multiple RES files and switching between them
4. **Object instantiation**: Implement `aesop <res> <object>` semantics:
   - Parse resource basename → locate `.RES` file
   - Instantiate object by name from export dictionary
   - Set up message loop and event dispatch
5. **Exit codes**: The AESOP launcher returns error codes to control program flow:
   - Game returns exit code that HACK.BAT checks to decide next action
   - Thirdeye should support similar semantics (return code → continue/retry/exit)

---

### Comparison: EOB3 vs Dungeon Hack Architecture

#### EOB3 — Pure AESOP Bytecode Runtime

**EYE.BAT** (2 lines):
```batch
@echo off
aesop eye start
```

**Architecture**:
- Single launcher: `AESOP.EXE` or `INTERP.EXE`
- Single RES file: `EYE.RES` (7.1 MB)
- All game logic in bytecode (SOP VM)
- Boot object: "start"
- Message handlers: M:0 (create) → M:3 (timer tick) → all rendering, input, combat in bytecode

**How it works** (from INTERP.C source in `arun/src/`):
```c
// Parse command line
strcpy(RES_name, argv[1]);        // e.g., "eye"
strcpy(object_name, argv[2]);     // e.g., "start"
strcat(RES_name, ".RES");         // "EYE.RES"

// Load RES file
RTR = RTR_construct(mem, heap, max_objs, RES_name);

// Find object in export dictionary
code = RTD_lookup(HROED, object_name);

// Create and run the object
rtn = create_program(1, bootstrap, code);
rtn = destroy_object(1, rtn);

// Return exit code
exit(rtn);
```

The launcher simply loads a RES file, finds a named object in its export dictionary, creates it (calls M:0), runs its message loop, destroys it, and exits with a return code.

#### Dungeon Hack — AESOP Bytecode + a Separate Dungeon Generator

**HACK.BAT** (multi-phase):
```batch
aesop open opening           # Phase 1: Load OPEN.RES, run intro/menu
aesop hack phase-one         # Phase 2: Load HACK.RES, initialize game state
..\maze %1 %2                # Phase 3: generate the dungeon files
aesop hack phase-two         # Phase 4: Load HACK.RES, cleanup/save
```

**Architecture**:
- Multiple executables: `AESOP.EXE` (the AESOP runtime) + `MAZE.EXE`
  (random dungeon generator, run once per new game)
- Multiple RES files: `OPEN.RES` (1.5 MB intro/menu) + `HACK.RES` (6.8 MB gameplay)
- Split: the SOP bytecode does intro/menu/gameplay; `MAZE.EXE` only
  pre-generates the dungeon files the gameplay SOP reads
- Boot objects: "opening" (OPEN.RES), "phase-one" (HACK.RES), "phase-two" (HACK.RES)
- Error code flow control: Exit codes (0/1/2/3+) control batch flow

**Why the difference?**

| Aspect | EOB3 | Dungeon Hack |
|--------|------|------|
| **Intro/Menu** | In "start" bytecode handlers | In "opening" (separate RES) |
| **Game Init** | In "start" bytecode handlers | In "phase-one" bytecode handler |
| **Game Loop** | "start" M:3 timer tick + Thirdeye renderer | "phase-two" bytecode handler |
| **Dungeon source** | Hand-authored `LVLnn.TMP` shipped with the game | Generated per playthrough by MAZE.EXE |
| **Optimization** | Pure bytecode (simpler) | Pure bytecode + a separate generator binary |
| **Distribution** | Single RES file (7.1 MB) | Split RES files (8.3 MB) |

#### What Does MAZE.EXE Do?

**Corrected (2026-08-05):** MAZE.EXE is *not* the game loop — that was
speculation. It is the **random dungeon generator**: a small Borland C++
utility ("Random Dungeon Generator v1.0/386  Event Horizon Software Inc.")
that reads `savegame/SETTINGS.DAT`, generates a fresh dungeon, and writes
`LEVELS.DAT` + `FEA%02d.DAT` (per-level) + `ITEMS.DAT` + `SEED.TXT` into
`savegame/`. `phase-two` (a HACK.RES SOP object) is the game loop; it
consumes MAZE's output via `load_level_map` / `open_feature_file` /
`get_feature_record` runtime calls.

Full RE writeup — file formats, feature tables, `dungeon` object's load
sequence, and paths to reimplementation — lives in
[dungeon_hack_maze.md](dungeon_hack_maze.md).

Errorlevel semantics (from HACK.BAT, confirmed against `phase-one`'s
observed return values). Batch `if ERRORLEVEL n` matches **n and above**,
and HACK.BAT tests high-to-low, so the effective routing is:
- `>= 3` → back to `:CONTINUE` (re-run phase-one)
- `2` → back to `:CHECKDEMO` (re-run intro, then phase-one)
- `1` → EXIT (quit game)
- `0` → fall through: run MAZE, then phase-two

#### Implications for Thirdeye

**Current state**: Thirdeye plays EOB3 (pure bytecode, single RES). For DH
we have OPEN.RES/HACK.RES loading, the phase-one/phase-two boot chain
(`bootObject` interprets HACK.BAT errorlevels), page compositing, the DH
palette-region map, and the 3D wall renderer — `load_level_map`,
`get_feature_record`, `init_viewspace`, `build_clipping` and `draw_walls`
are all implemented, and a phase-two session with movement reports **zero**
stubbed CALLs. What's missing is dungeon *content*: we ship a native
mini-MAZE that seeds structurally valid but empty files, so the party
starts sealed in rock until MAZE.EXE output (or a native generator)
provides a real map.

**Two paths forward** (detail in [dungeon_hack_maze.md](dungeon_hack_maze.md)):
1. Bootstrap by running MAZE.EXE once under DOSBox, capture its output,
   feed our stubs against real files. Proves format and unblocks
   phase-two without reimplementing MAZE.
2. Reimplement MAZE natively. Needs a Ghidra pass + a DOSBox baseline to
   diff against.

The architecture is largely compatible. The main work is adding multi-RES support and making boot object discovery more flexible.

---

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
