# Thirdeye — AESOP engine reimplementation

A from-scratch C++ reimplementation of SSI/Westwood's **AESOP** engine (AESOP/16 and
AESOP/32), the bytecode VM that powers **Eye of the Beholder 3** (`EYE.RES`) and
**Dungeon Hack** (`HACK.RES` + `OPEN.RES`). Written by John Miles in 1992.

**End goal:** play EOB3 (and Dungeon Hack) natively from the original game data. You must own
the original games — Thirdeye ships no game assets.

Version 0.87.0 · GPLv3 · SDL2 (Linux/macOS/Windows), C++20.

## Where things live

- [docs/architecture.md](docs/architecture.md) — engine internals: build targets, subsystems,
  SOP VM implementation, stack discipline, runtime hook, host loop.
- [docs/progress.md](docs/progress.md) — chronological narrative of what's been built/fixed
  (the milestone story per subsystem).
- [docs/roadmap.md](docs/roadmap.md) — phased plan, what's still pending (combat, save/load,
  Dungeon Hack, Phase 5 originals).
- [docs/game_data.md](docs/game_data.md) — `../data/` (EOB3 install) + `../eob3_research/`
  (Westwood-released sources & docs) + daesop cheatsheet.
- [docs/perf_notes.md](docs/perf_notes.md) — perf history + the `THIRDEYE_*` diagnostic env
  vars (worth skimming once — they save hours when something stalls silently).
- [docs/eob3_savegame_format.md](docs/eob3_savegame_format.md) — `ITEMS.TMP` / `LVLnn.TMP`.
- [docs/equipment_slots.md](docs/equipment_slots.md) — RE of `W:inventory[]` body-part
  layout + EOB1 type → EOB3 class table + why the current chargen placement is wrong.
- [docs/create_sav_and_item_format.md](docs/create_sav_and_item_format.md) — char-gen save +
  EOB1 14-byte item format.
- [docs/upgrade_to_cpp20.md](docs/upgrade_to_cpp20.md) — build notes.

## Build + test

Typical: `cmake -S . -B build -G Ninja && cmake --build build`. Deps: SDL3, OpenAL,
WildMIDI; GoogleTest + CLI11 are auto-fetched (network on first configure). Default build is
**`Debug` -O0**; for the interpreter-heavy bring-up loop a Release config is 5–10×
faster — reconfigure with `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` (still under
`build/`, no separate `build-release/` tree).

Tests: `cmake --build build --target runtests && ctest --test-dir build --output-on-failure`.

Run the VM over data:
- `thirdeye <file.RES>` — boot from `start` (full game) or list + drive any `.RES`.
- Common flags: `--vm`, `--skip-intro`, `--skip-menu`, `--debug`, `--scale=N`.
- For EOB3 the user's install lives at `../data/`:
  `build/thirdeye.app/Contents/MacOS/thirdeye ../data/EYE.RES --skip-intro --vm --skip-menu --scale=4`.

### End-to-end / regression harness

For repro + regression of anything that needs the game running (rendering, input,
menu flows), drive the engine headlessly with `THIRDEYE_AUTOWALK` (comma-sequence
of hex scan codes, one per ~40 pumps; last code repeats) + `THIRDEYE_DUMP` (BMP
snapshot per present), then diff with Pillow.

```bash
# title menu Enter (Continue — already selected when a save exists) → Enter
# (load slot 1) → turn-right → walk fwd ×4
THIRDEYE_DUMP=/tmp/f_%d.bmp \
THIRDEYE_AUTOWALK=0d,0d,4900,4800,4800,4800,4800 \
  build/thirdeye.app/Contents/MacOS/thirdeye ../data/EYE.RES --skip-intro --vm --scale=2 &
sleep 15 && kill -INT $!
python3 -c "from PIL import Image, ImageChops; \
  a=Image.open('/tmp/f_15.bmp').convert('RGB'); \
  b=Image.open('/tmp/f_25.bmp').convert('RGB'); \
  print(ImageChops.difference(a,b).getbbox())"
```

Codes: `4800`=fwd `5000`=back `4b00`/`4d00`=strafe L/R `4700`/`4900`=turn L/R `0d`=Enter
`1b`=Esc. A bbox capped at `x=176` means no leak past the dungeon-view rect — that's
how we caught the page-92-vs-99 wall-clip bug (see [progress.md](docs/progress.md)).
`THIRDEYE_PARTY=x,y,fdir` seeds position/facing. To reach gameplay, drive the title
menu through `THIRDEYE_AUTOWALK` (no shortcut env var — the shortcut bypassed SOP
state and reproduced bugs that didn't exist in real play). Menu gotchas: with a
save present the menu starts on "Continue the Quest" (a `5000` Down moves you to
"Gather a New Party" → chargen); the Florn Falconhand cutscene now shows a
dialog selector — an extra `0d` picks the first choice and dismisses it.

## What runs today

- Intro cinematic (with music), title screen/menu graphics, palette + mouse cursor; SFX/XMIDI
  on keypress (EOB3 install path).
- `thirdeye <file.RES>` loads any AESOP `.RES`, then executes its SOP bytecode (every message
  handler runs through the VM; `--debug` prints an annotated instruction trace). Validated
  end-to-end against `files/SAMPLE.RES` (the `AMAZE` demo's `start` object).
- EOB3 boots via SOP, renders its title menu (interactive — mouse + keyboard), and with
  `--skip-menu` enters the game: walls/floor/depth across all 14 levels, party + HUD,
  movement (WASD/QE + arrow keys + compass mouse), equipment screen with real char-gen gear,
  per-level objects from `LVLnn.TMP`. See [docs/progress.md](docs/progress.md).

The current top-of-stack work item is save/load + party import — combat now lands
(troll dies, sword wraith dies on contact). See [docs/roadmap.md](docs/roadmap.md) Phase 3.

## Conventions & gotchas (read this once)

- **Our job is to *run* EYE.RES, not to RE it.** Thirdeye is an AESOP-compatible runtime;
  the game logic lives in EYE.RES bytecode and executes unchanged on our VM. When something
  misbehaves, the bug is almost always in our runtime (a stubbed CALL, a wrong opcode
  semantic, an uninitialized static the original loader would have set) — not in the SOP.
  **Read `../eob3_research/` first.** John Miles released the AESOP/32 runtime C source
  (`runtime/*.C`: `EYE.C`, `RTCODE.C`, `RTOBJECT.C`, `INTERP.C`, `EVENT.C`, `GRAPHICS.C`,
  etc.) and the compiler source (`aesop32/DEV/`) — that's the ground truth for how a CALL
  is supposed to behave, how `create_program` initializes state, how events dispatch.
  Reach for `daesop` only as a *debugging* tool, to confirm what the bytecode expects of
  our runtime — never to "fix" the SOP itself.
- **EOB3-specific runtime C lives in `../eob3_research/arun/src/`, not `runtime/`.**
  `runtime/EYE.C` is the later AESOP/32 build and is missing the EOB3-only CALLs;
  `arun/src/EYE.C` is the 16-bit "Eye III engine support" original (28-Oct-92) with
  `spell_request`/`spell_list`/`magic_field`/`do_dots`/`do_ice`/`step_square_*`/the whole
  save cluster, plus `GRAPHICS.C`'s `solid_bar_graph` etc. Its box-drawing comment bytes
  make grep treat it as binary — **use `grep -a`**. Constants (`MTYP_*`, `DIR_*`, `LVL_X`,
  `NUM_SAVEGAMES`) are in `arun/src/SHARED.H`; `save_range`'s CDESC record format is in
  `arun/src/RTOBJECT.C`/`RTOBJECT.H`.
- **VM stack discipline is subtle.** `PUSH` reserves a slot, value loads/constants
  **overwrite the top slot in place** (which is why real bytecode emits a `PUSH` before each
  load), scalar stores don't pop, binary ops are left=second/right=top, branches don't pop.
  Match `RT.ASM`, not intuition. Full details in
  [docs/architecture.md § Stack discipline](docs/architecture.md#stack-discipline-non-obvious-baked-into-the-port).
- **RES resource names are case-sensitive** (`"holy symbol"` ≠ `"Holy symbol"`).
- **The container magic is `AESOP/16 V1.00` even for AESOP/32 files.**
- **`daesop`/`arc` are ports of old C** — pure C style, struct alignment **packed to 1 byte**
  (`PACK(...)` macro in [apps/thirdeye/resources/res.hpp](apps/thirdeye/resources/res.hpp)).
  Memory is leaked by design in the original tools (process-exit cleanup); don't "fix" unless
  it matters.
- **Never commit game assets** (`*.RES`, `*.GFF`, `INTRO.*`) — Thirdeye requires the user's
  own original game install. Don't copy from `../data/` either.
- **`std::map::operator[]` is a foot-gun for sparse-index containers** — it default-inserts
  on miss. We've been bitten by this once (the 17-second `set_mouse_pointer` boot stall — see
  [docs/perf_notes.md](docs/perf_notes.md)). Prefer `.at()` or an explicit `.find()`.

## Diagnostic env vars (quick reference)

Full list in [docs/perf_notes.md](docs/perf_notes.md). The first-reach-for ones:

- `THIRDEYE_DUMP=/tmp/frame_%04d.bmp` — snapshot every present (numbered if path has `%`).
- `THIRDEYE_AUTOWALK=4d00` (or comma-list `5000,0d,0d,4900,4800,4800,4800`) — scripted
  SYS_KEYDOWN(s) every ~40 pumps; last value repeats. Drives headless e2e/regression
  tests (4800=fwd, 4b00/4d00=strafe L/R, 4700/4900=turn L/R, 0d=Enter, 1b=Esc).
- `THIRDEYE_TIMING=1` — log when each runtime fn is first called; `=2` logs every call.
- `THIRDEYE_SLOWOP=1` — log any single opcode (CALL/SEND) that took >50 ms.
- `THIRDEYE_PERF=1` — per-present `{draws, drawImage µs, present ms, gap}`.
- `--debug` — VM step trace + runtime CALL trace (slow, redirect to file).

## daesop cheatsheet

```
daesop -ir EYE.RES out.txt              # resource listing
daesop -j  <res> <name> out.txt          # disassemble a code resource by name
daesop -k  <res> <num>  out.txt          # ...or by number
daesop -eob3conv EYE.RES EYE2.RES        # EOB3→AESOP/32 conversion
```
