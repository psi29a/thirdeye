# Thirdeye — AESOP engine reimplementation

A from-scratch C++ reimplementation of SSI/Westwood's **AESOP** engine (AESOP/16 and
AESOP/32), the bytecode VM that powers **Eye of the Beholder 3** (`EYE.RES`) and
**Dungeon Hack** (`HACK.RES` + `OPEN.RES`). Written by John Miles in 1992.

**End goal:** play EOB3 (and Dungeon Hack) natively from the original game data. You must own
the original games — Thirdeye ships no game assets.

Version 0.89.0 · GPLv3 · SDL3 (Linux/macOS/Windows), C++20.

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
- [docs/dungeon_hack_maze.md](docs/dungeon_hack_maze.md) — RE of Dungeon Hack's `MAZE.EXE`
  (random dungeon generator) + `LEVELS.DAT` / `FEA%02d.DAT` / `ITEMS.DAT` formats + the
  DH SOP consumer-side calls. Read before doing more Phase 4 work.
- [docs/upgrade_to_cpp20.md](docs/upgrade_to_cpp20.md) — build notes.
- [docs/control_channel.md](docs/control_channel.md) — design for the live control
  socket (agent-driven play + interactive debugging); phased hand-off spec.

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
`1b`=Esc. `M`=automap toggle (host-side overlay; a hex keycode can't reach it),
`Lx:y`/`Rx:y`=mouse click at native 320x200 coords. A bbox capped at `x=176` means no leak past the dungeon-view rect — that's
how we caught the page-92-vs-99 wall-clip bug (see [progress.md](docs/progress.md)).
`THIRDEYE_PARTY=x,y,fdir` seeds position/facing. To reach gameplay, drive the title
menu through `THIRDEYE_AUTOWALK` (no shortcut env var — the shortcut bypassed SOP
state and reproduced bugs that didn't exist in real play). Menu gotchas: with a
save present the menu starts on "Continue the Quest" (a `5000` Down moves you to
"Gather a New Party" → chargen); the Florn Falconhand cutscene now shows a
dialog selector — an extra `0d` picks the first choice and dismisses it.

Don't hand-author long sequences: play the flow once with `THIRDEYE_RECORD=1` and
paste the printed line. Recorded sessions live in `scripts/walks/*.walk`
(comment-stripped token files); `WALK=scripts/walks/<f>.walk RES=../data/EYE.RES
scripts/ci-valgrind.sh` replays one under valgrind in docker and fails on memory
errors (the walk ends with an in-game quit, so leak reports cover the real quit
path).

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
- `THIRDEYE_RECORD=1` (or `=<path>`) — record real keys/clicks during a hand-played
  session; at exit prints (and with a path, writes) the replayable `THIRDEYE_AUTOWALK=`
  line. The record→replay lever for building CI/valgrind scripts.
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

<!-- rtk-instructions v2 -->
# RTK (Rust Token Killer) - Token-Optimized Commands

## Golden Rule

**Always prefix commands with `rtk`**. If RTK has a dedicated filter, it uses it. If not, it passes through unchanged. This means RTK is always safe to use.

**Important**: Even in command chains with `&&`, use `rtk`:
```bash
# ❌ Wrong
git add . && git commit -m "msg" && git push

# ✅ Correct
rtk git add . && rtk git commit -m "msg" && rtk git push
```

## RTK Commands by Workflow

### Build & Compile (80-90% savings)
```bash
rtk cargo build         # Cargo build output
rtk cargo check         # Cargo check output
rtk cargo clippy        # Clippy warnings grouped by file (80%)
rtk tsc                 # TypeScript errors grouped by file/code (83%)
rtk lint                # ESLint/Biome violations grouped (84%)
rtk prettier --check    # Files needing format only (70%)
rtk next build          # Next.js build with route metrics (87%)
```

### Test (60-99% savings)
```bash
rtk cargo test          # Cargo test failures only (90%)
rtk go test             # Go test failures only (90%)
rtk jest                # Jest failures only (99.5%)
rtk vitest              # Vitest failures only (99.5%)
rtk playwright test     # Playwright failures only (94%)
rtk pytest              # Python test failures only (90%)
rtk rake test           # Ruby test failures only (90%)
rtk rspec               # RSpec test failures only (60%)
rtk test <cmd>          # Generic test wrapper - failures only
```

### Git (59-80% savings)
```bash
rtk git status          # Compact status
rtk git log             # Compact log (works with all git flags)
rtk git diff            # Compact diff (80%)
rtk git show            # Compact show (80%)
rtk git add             # Ultra-compact confirmations (59%)
rtk git commit          # Ultra-compact confirmations (59%)
rtk git push            # Ultra-compact confirmations
rtk git pull            # Ultra-compact confirmations
rtk git branch          # Compact branch list
rtk git fetch           # Compact fetch
rtk git stash           # Compact stash
rtk git worktree        # Compact worktree
```

Note: Git passthrough works for ALL subcommands, even those not explicitly listed.

### GitHub (26-87% savings)
```bash
rtk gh pr view <num>    # Compact PR view (87%)
rtk gh pr checks        # Compact PR checks (79%)
rtk gh run list         # Compact workflow runs (82%)
rtk gh issue list       # Compact issue list (80%)
rtk gh api              # Compact API responses (26%)
```

### JavaScript/TypeScript Tooling (70-90% savings)
```bash
rtk pnpm list           # Compact dependency tree (70%)
rtk pnpm outdated       # Compact outdated packages (80%)
rtk pnpm install        # Compact install output (90%)
rtk npm run <script>    # Compact npm script output
rtk npx <cmd>           # Compact npx command output
rtk prisma              # Prisma without ASCII art (88%)
```

### Files & Search (60-75% savings)
```bash
rtk ls <path>           # Tree format, compact (65%)
rtk read <file>         # Code reading with filtering (60%)
rtk grep <pattern>      # Search grouped by file (75%). Format flags (-c, -l, -L, -o, -Z) run raw.
rtk find <pattern>      # Find grouped by directory (70%)
```

### Analysis & Debug (70-90% savings)
```bash
rtk err <cmd>           # Filter errors only from any command
rtk log <file>          # Deduplicated logs with counts
rtk json <file>         # JSON structure without values
rtk deps                # Dependency overview
rtk env                 # Environment variables compact
rtk summary <cmd>       # Smart summary of command output
rtk diff                # Ultra-compact diffs
```

### Infrastructure (85% savings)
```bash
rtk docker ps           # Compact container list
rtk docker images       # Compact image list
rtk docker logs <c>     # Deduplicated logs
rtk kubectl get         # Compact resource list
rtk kubectl logs        # Deduplicated pod logs
```

### Network (65-70% savings)
```bash
rtk curl <url>          # Compact HTTP responses (70%)
rtk wget <url>          # Compact download output (65%)
```

### Meta Commands
```bash
rtk gain                # View token savings statistics
rtk gain --history      # View command history with savings
rtk discover            # Analyze Claude Code sessions for missed RTK usage
rtk proxy <cmd>         # Run command without filtering (for debugging)
rtk init                # Add RTK instructions to CLAUDE.md
rtk init --global       # Add RTK to ~/.claude/CLAUDE.md
```

## Token Savings Overview

| Category | Commands | Typical Savings |
|----------|----------|-----------------|
| Tests | vitest, playwright, cargo test | 90-99% |
| Build | next, tsc, lint, prettier | 70-87% |
| Git | status, log, diff, add, commit | 59-80% |
| GitHub | gh pr, gh run, gh issue | 26-87% |
| Package Managers | pnpm, npm, npx | 70-90% |
| Files | ls, read, grep, find | 60-75% |
| Infrastructure | docker, kubectl | 85% |
| Network | curl, wget | 65-70% |

Overall average: **60-90% token reduction** on common development operations.
<!-- /rtk-instructions -->