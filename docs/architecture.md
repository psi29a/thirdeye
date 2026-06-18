# Thirdeye — engine architecture

Internals of the C++ AESOP/32 host. For the project overview see [CLAUDE.md](../CLAUDE.md);
for what's been built (chronological), [progress.md](progress.md); for what's pending,
[roadmap.md](roadmap.md).

## Build targets (`apps/`)

| App | Role | State |
|-----|------|-------|
| **thirdeye** | The runtime/game engine (SDL2). | SOP VM structurally complete (all 88 opcodes except `BRK`; objects/`SEND`/statics/externs) + event system (EVENT.C) + windowing/region input + text. EOB3 boots via SOP and renders its title menu (and, with `--skip-menu`, the in-game HUD). |
| **daesop** | RES file tool: disassembler + EOB3→AESOP/32 converter (port of Mirek Luza's DAESOP 0.85). | Complete & working. |
| **arc** | AESOP resource compiler / SOP assembler (port of John Miles' 1993 DEV tools). | Ported, compiles. |
| **launcher** | Launcher front-end. | Stub. |
| **tests** | GTest unit tests (GoogleTest auto-fetched via FetchContent). | VM + resource tests; on by default, run in CI on Linux/macOS/Windows. |

Subprojects are gated in the top [CMakeLists.txt](../CMakeLists.txt) (`apps/thirdeye` always
builds; daesop/arc/launcher behind options; tests via `UNIT_TESTS`, default **ON**).
`components/` is a static lib (file/path/config management in OpenMW style, plus
`misc/stringops`). CLI parsing uses CLI11 (header-only, fetched). GoogleTest and CLI11 are
both pulled in by CMake FetchContent — no system install needed (needs network on first
configure).

## `thirdeye` engine subsystems (`apps/thirdeye/`)

- **resources/** — `res.cpp` reads the `.RES` container (global header, directory blocks of 128
  entries, entry headers, assets by name/number, dictionary tables 0–4). Also:
  `getCodeResourceNames()` (resources with the `0x10` code attribute), `getExports()`/`getImports()`
  (parse a code object's `.EXPT`/`.IMPT` — same dictionary format as tables 0–4, via
  `parseDictionary()`). `gffi.cpp` reads `INTRO.GFF`.
- **vm/** — the SOP bytecode interpreter. `opcodes.hpp` (opcode enum + `opName`/`opDesc`),
  `vm.hpp`/`vm.cpp` (`Interpreter`: stack machine, dispatch, trace), `objects.hpp`/`.cpp`
  (`ObjectSystem`: classes + instances + SEND/PASS), `events.hpp`/`.cpp` (`EventSystem`: notify
  list + FIFO queue + region windowing).
- **graphics/** — `graphics.cpp` (SDL2: palette, bitmap blit, video playback, zoom, mouse cursor)
  + `bitmap`/`font`/`palette`.
- **sound/** — `sound.cpp` (digital sound mixer) + `xmidi.cpp` (XMIDI→MIDI + WildMIDI).
- **engine.cpp / engine.hpp** — top-level. `go()` resolves the resource file
  (`resolveResourceFile()` accepts a `.RES` file *or* a game-data dir, auto-detecting the game
  from the filename). For a recognized full game (INTRO.GFF present) it plays the
  intro→title path; for any other `.RES` it lists resources and drives the VM
  (`runResourceVM()`): finds each code object, runs every exported message handler, and reports
  where each ends. `main.cpp` uses CLI11 (positional `<game-data|.RES>` + flags).

---

## SOP / bytecode reading

**Disassembly: COMPLETE** (in `daesop`, see [apps/daesop/dasm.cpp](../apps/daesop/dasm.cpp)).

All **88 opcodes `0x00`–`0x57`** are defined in [files/bytecode.def](../files/bytecode.def)
(`<hex>,<name>,<param-count>,<param-types>,<desc>`). Param types: byte/word/long/import/message.
`99` = handled specially in code (only `CASE`).

Two-pass disassembler: pass 1 builds a code map (marks message-handler/procedure starts,
jump/CASE/`LECA` targets, data vs. code, constant tables, auto/static/extern variable refs);
pass 2 emits annotated assembly with resolved symbolic names. Resolves import (runtime functions,
extern vars/arrays), export (object name, message handlers, public statics), message-table refs,
and resource-number → name comments.

The opcode set is a stack VM: branches (`BRT/BRF/BRA/CASE`), stack/arithmetic/logic ops,
constant loads (`SHTC/INTC/LNGC`), `CALL` (runtime function), `SEND`/`PASS` (object messages),
`JSR/RTS` (procedures), and load/store families for **auto** (locals/params), **static** (object
state), **extern** (cross-object), **table** (constant), plus effective-address variants. `END`
ends a handler.

---

## SOP VM — implementation status (`apps/thirdeye/vm/`)

A native C++ port of the original dispatcher (`eob3_research/runtime/RT.ASM`, proc `RT_execute`
+ `op_dispatch`). `Interpreter` takes a code resource's bytes and executes a handler from an
entry offset.

**Implemented:**
- All 88 opcodes except `BRK` (the original's int-3 debugger hook).
- Branches `BRT/BRF/BRA/CASE`; stack ops `PUSH/DUP`; all unary + binary arithmetic/logic/compare;
  constants `SHTC/INTC/LNGC`; `JSR/RTS`; `END`.
- **Auto (local/param) scalars** `LAB/LAW/LAD` + `SAB/SAW/SAD` + `LEAA`; `LECA`.
- **`RCRS` + `CALL`**: `RCRS` pushes a runtime-function handle (the import number); `CALL` pops
  args, resolves the handle→name via the `.IMPT` map, and dispatches through a pluggable
  `RuntimeCall` hook. `readCodeString()` resolves code-space (`LECA`) address args to inline
  strings. The engine supplies a stub library (logs each call, returns 0); `launch` is real — it
  spawns the named program if present.
- **Debug trace** (`setTrace`): `describe(pc)` prints PC + opcode + decoded operand + a short
  description, in fixed-width aligned columns.
- **`SEND`/`PASS` + object system** (`vm/objects.hpp`/`.cpp`, `ObjectSystem`): code objects register
  as classes (code + header + handler map from `.EXPT` + imports); `SEND(obj, msg, args)` resolves
  the handler by walking the class hierarchy and runs it with `THIS`=obj; `PASS` forwards the
  current message to the parent. Parameters are passed into the handler frame above `fptr` and
  read via signed auto offsets (params negative, locals/THIS positive).
- **Static (object-state) variables**: scalars `LSB/LSW/LSD` + `SSB/SSW/SSD` and arrays
  `LSBA/LSWA/LSDA` + `SSBA/SSWA/SSDA`, backed by per-instance storage. **Constant tables**
  `LTBA/LTWA/LTDA` (code-resident). **Array indexing** `AIM`/`AIS`.
- **Inherited statics**: an instance allocates the whole class chain's statics, laid out
  base-class-first; a handler's static offsets are relative to its defining class's block.
- **Auto arrays** `LABA/LAWA/LADA` + `SABA/SAWA/SADA` (`addr = fptr − offset + index`).
- **Extern (cross-object) variables** — link layer, ported from `RTLINK.C construct_thunk` but
  resolved lazily + cached: scalars `LXB/LXW/LXD` + `SXB/SXW/SXD`, arrays `LXBA…SXDA`, `LEXA`,
  `SXAS`, `SOLE`. A class's `.IMPT` `B:/W:/L:<name>` entry maps an XR-list byte offset to
  `"<XR-offset>,<source-class>"`; the tag is looked up in the source class's `.EXPT` (walking
  derived→base), and the exporting class's static base is added. The target object index travels
  on the stack (low word); for arrays the high word holds the byte-scaled index, merged in by
  `SXAS`. `SOLE` probes the object list.
- **Event system** (`vm/events.hpp`/`.cpp`, port of `EVENT.C`): the FIFO event queue +
  notification-request list. `notify(index, message, event, parameter)` registers "SEND `message`
  to `index` when `event`/`parameter` fires"; `post_event` queues; `dispatch_event` pulls and
  SENDs to each matching client. Faithful match-parameter rules (`-1` wildcard, `SYS_TIMER` ≥,
  else `==`), app-vs-system event priority, `destroy_object`→`cancel_entity_requests`. Wired into
  the runtime hook in `engine.cpp`.

**Tagged addresses.** Code/stack/static live in separate buffers, so an effective address is a
tagged `Value` (high nibble = space: `Code`/`Stack`/`Static`/`Extern`; low bits = offset, plus
object index for static/extern — see `makeAddr`/`decodeAddr` in `vm.hpp`). `LECA`/`LETA` → Code,
`LEAA` → Stack, `LESA` → Static(THIS), `LEXA` → Extern(target). The tag sits clear of normal
integer values and index arithmetic only touches the offset bits, so a tagged address survives
`AIM`/`AIS`. `readCodeString` decodes Code addresses (used for string args); static/extern can be
dereferenced via `ObjectSystem::staticsPtr`.

---

## Stack discipline (non-obvious; baked into the port)

- The operand stack is one contiguous, **byte-addressed** region growing downward; `mSp` = top
  VALUE slot, `mFptr` = frame base.
- `PUSH` reserves+zeroes a slot; **value loads/constants overwrite the top slot in place** (they
  don't push — which is why real bytecode emits a `PUSH` before each load).
- **Scalar stores don't pop** (assignment-as-expression).
- Binary ops take **left = second-from-top, right = top** (so `SUB`=l−r, `DIV`=l/r, `SHR` is
  *logical*).
- **Auto vars** live at `fptr − offset` (byte-addressed); byte/word loads sign-extend. A handler
  begins with a 2-byte `auto_size` (MHDR) that *includes* the 2-byte `THIS` slot at `fptr−2`.
- **Branches do NOT pop.** `BRT`/`BRF`/`BRA`/`CASE` read the top value but leave it on the stack.
  The next value-load overwrites it in place (so branch targets often start with a bare
  `LAW`/`LSB`, no preceding `PUSH`). Popping in a branch drifts the stack up one slot per branch
  — invisible in short tests, fatal in a 2000-iteration loop.
- **The operand stack needs slack.** AESOP loads the word-sized `THIS` (at `fptr−2`) with a
  *dword* opcode, reading 2 bytes past the top; the original tolerated this via the malloc
  block's slack. `mStk` is allocated with a `kStackGuard` (256 B) above the logical top so a
  benign boundary straddle doesn't trip the bounds check, while a runaway still does.

## Addresses & symbols

- **Entry points** come from `.EXPT`: `N:OBJECT`→object name, `M:<n>`→handler byte offset,
  `B:/W:/L:<name>`→public static's offset.
- **Imports** from `.IMPT`: `C:<name>`→XR-list byte offset of a runtime function (what `RCRS`
  references); `B:/W:/L:<name>`→`"<XR-offset>,<source-class>"` extern variable (what
  `LXB…LEXA` reference). Runtime-fn and extern imports share one XR offset space per class
  (4-byte XCR vs 2-byte XDR entries in the original thunk).
- **Special tables are the runtime catalog**: table 3 (`Low level functions`) lists *all*
  runtime functions the engine provides (≈135 in EOB3) — the master to-do list for the runtime
  library; table 4 (`Message names`) feeds `SEND`.
- **Class inheritance via `N:PARENT`** (real EYE.RES subclassing): a code object's real
  superclass is the `.EXPT` `N:PARENT` resource number (e.g. `axe`→1688 `weapons`→1373 `arms`→
  1371 `items`), NOT the code header's `parent` field (an unrelated encoded value daesop can't
  resolve either). `registerClasses` reads `N:PARENT` and sets `header.parent` from it, so
  subclasses inherit their base's handlers/statics.

## Runtime hook

`engine.cpp defaultRuntimeCall` is the C++ implementation of the runtime API (`EYE.C`,
`RTCODE.C`, `GRAPHICS.C`, `MOUSE.C`, `INTRFACE.C`). Each runtime function from the bytecode's
`CALL` opcode dispatches to a name match here; most are still stubs (log + return 0), but the
hot paths are real (graphics: `set_palette`/`draw_bitmap`/`refresh_window`/`set_mouse_pointer`;
text: `text_window`/`text_style`/`text_color`/`text_xy`/`print`/`sprint`; windowing:
`assign_subwindow`/`release_window`/`set_x1`/`set_x2`/`get_x1`..`get_y2`; events:
`notify`/`post_event`/`dispatch_event`/`peek_event`; object mgmt:
`create_program`/`create_object`/`destroy_object`; transfer/save:
`open_transfer_file`/`player_attrib`/`item_attrib`/`resume_level`; math: `absv`/`minv`/`maxv`/
`dice`/`rnd`; misc: `peekmem`/`pokemem`/`launch`).

## Host loop

The kernel's main loop is `while (!quit) dispatch_event();` — under DOS it span the CPU while
keyboard/timer ISRs injected events into the queue asynchronously. We can't change it (it's the
game's own bytecode). **Host seam** (`engine.cpp pumpHost`): the polled runtime functions
`dispatch_event`/`peek_event` pump SDL each call — translate input → AESOP events
(`SYS_KEYDOWN`; window-close → `QuitRequested`, which unwinds the VM to a clean exit), post a
~30 Hz `SYS_TIMER` heartbeat (`EventSystem::postTimer`, coalesced), present the frame, and
`SDL_Delay(~10ms)` when the queue is idle. That turns the 100% spin into an event-driven,
frame-paced loop. The whole game session runs *inside* `send(start, MSG_CREATE)` (returns only
at quit), so the top-level SDL loop lives in `dispatch_event` — the shape of the original
`INTERP.EXE`. With graphics active the instruction budget is disabled (`setMaxSteps(0)`);
headless keeps it.
