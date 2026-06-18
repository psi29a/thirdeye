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
- [docs/create_sav_and_item_format.md](docs/create_sav_and_item_format.md) — char-gen save +
  EOB1 14-byte item format.
- [docs/upgrade_to_cpp20.md](docs/upgrade_to_cpp20.md) — build notes.

## Build + test

Typical: `cmake -S . -B build -G Ninja && cmake --build build`. Deps: SDL2, OpenAL,
WildMIDI; GoogleTest + CLI11 are auto-fetched (network on first configure). Default build is
**`Debug` -O0**; a Release build (`-DCMAKE_BUILD_TYPE=Release -S . -B build-release`) is
5–10× faster for the interpreter.

Tests: `cmake --build build --target runtests && ctest --test-dir build --output-on-failure`.

Run the VM over data:
- `thirdeye <file.RES>` — boot from `start` (full game) or list + drive any `.RES`.
- Common flags: `--vm`, `--skip-intro`, `--skip-menu`, `--debug`, `--scale=N`.
- For EOB3 the user's install lives at `../data/`:
  `build/thirdeye.app/Contents/MacOS/thirdeye ../data/EYE.RES --skip-intro --vm --skip-menu --scale=4`.

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

The current top-of-stack work item is combat (the attack chain runs but no hit lands — see
[docs/roadmap.md](docs/roadmap.md) Phase 3).

## Conventions & gotchas (read this once)

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
- `THIRDEYE_AUTOWALK=4d00` — scripted move every ~40 pumps (4800=fwd, 4b00/4d00=strafe L/R).
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
