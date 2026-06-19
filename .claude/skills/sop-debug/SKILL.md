---
name: sop-debug
description: Reference for reading AESOP SOP bytecode in EYE.RES / HACK.RES — daesop disassembler commands, VM stack discipline, how to find message handlers, naming conventions (`M:N` messages, `C:foo` runtime calls, `W:/B:/L:` exported vars). Use whenever you need to disassemble or interpret a SOP handler: triggers include "disassemble", "daesop", "what does M:N do", "M:71", "SOP", "bytecode", "stack discipline", "PUSH overwrites top", "LSWA", "SEND", "MESSAGE_HANDLER_NAME", or any time you're cracking open `.dasm` output.
---

# Reading SOP bytecode

This skill exists because we *run* EYE.RES, but we sometimes need to read it to understand
what the runtime is being asked to do. We never edit it.

## daesop cheatsheet

```sh
# Resource listing (find class numbers + names)
build-release/thirdeye.app/Contents/MacOS/daesop -ir ../data/EYE.RES /tmp/eye.lst

# Disassemble a code resource by number (1369 = PC, 1622 = NPC, 1688 = weapons,
# 1370 = entities, 1382 = kernel, 2380 = tables, 1380 = xfer)
build-release/thirdeye.app/Contents/MacOS/daesop -k ../data/EYE.RES 1369 /tmp/pc.dasm

# ...or by name
build-release/thirdeye.app/Contents/MacOS/daesop -j ../data/EYE.RES PC /tmp/pc.dasm
```

The `.dasm` output is **non-ISO extended-ASCII** (resource strings can be any byte). Always
`LC_ALL=C grep -an` not plain `grep` or `awk`, or you'll get "multibyte conversion failure"
and silent drops.

## Finding handlers in a `.dasm`

```sh
LC_ALL=C grep -an 'MESSAGE_HANDLER_NAME' /tmp/pc.dasm
# → lines + names. Body of handler N runs from its line up to the next handler's line.
```

Handlers are listed twice: once in the import table near the top of the file (with `M:71`
short codes and message-number), once before each handler body as
`.MESSAGE_HANDLER_NAME "roll to hit"`. The body line is what you `sed -n 'A,Bp'` to read.

## Class hierarchy (parents)

`N:PARENT` near the top of each `.dasm` tells you what the class inherits from. Common chain:
`PC → entities`, `NPC → entities`, `weapons → arms → (root)`. SEND walks up this chain
looking for a handler. PASS does the same starting at a given parent class.

## Object index vs class number

A bare `INTC #N` in a SEND target is an **object index** (slot in the kernel's object table),
*not* a class number. E.g. `INTC #07d3 ;2003` in PC.M:71 is the singleton instance of `tables`
class (2380), not the "marble plaque" code resource (which happens to also have id 2003).
daesop's `; possible reference to ... (code)` comments are heuristic and often wrong.

## Stack discipline (THE non-obvious rule)

Burnt-in baked-in critical:

- `PUSH` **reserves** a new top slot.
- Value loads/constants (`LSW`, `LSB`, `LAW`, `LAD`, `SHTC`, `INTC`, `LNGC`, `LSWA`, `LXW`,
  …) **overwrite the top slot in place** — they don't push. That's why real bytecode emits
  `PUSH` before each load: to make room.
- Scalar stores (`SSW`, `SSB`, `SAW`, `SAB`, `SXB`, `SXW`, …) **don't pop** — the value
  stays on top after the store.
- Binary ops (`ADD`/`SUB`/`MUL`/`DIV`/`BAND`/`BOR`/`EQ`/`NE`/`LT`/`LE`/`GT`/`GE`/`SHL`/`SHR`)
  consume two slots: **left = second-from-top, right = top**, result on top.
- Branches (`BRF`/`BRT`/`BRA`) **don't pop** the condition. The next instruction
  inherits a meaningful top, or there's a `04 PUSH` shortly after to discard it.
- `SEND #N, msg` reads `N + 2` slots (N args + object ref + return holder pattern), result
  on top.
- `END` returns the value on top of stack to the caller.

Tracing a snippet mentally? Match against `RT.ASM`, not your intuition. Full deep dive in
[docs/architecture.md § Stack discipline](../../../docs/architecture.md#stack-discipline-non-obvious-baked-into-the-port).

## Naming conventions

- `M:N` — a message handler (`M:71` = "roll to hit"). `N` is the AESOP message number; the
  name string only exists in the symbol table.
- `C:foo` — a runtime function (`C:dice`, `C:post_event`). These dispatch to our
  `defaultRuntimeCall` in [apps/thirdeye/engine.cpp](../../../apps/thirdeye/engine.cpp).
- `W:foo` — exported word (2 bytes), `B:foo` byte, `L:foo` long (4 bytes), `D:foo` double-word
  (sometimes 4, sometimes packed). Position in the class's static block follows the export.
- `paramVar_L:0` is THIS (first arg, the receiver as a pointer-sized value); `paramVar_L:65532`
  is the next arg (offset -4 from the frame); `paramVar_L:65528` is the arg after (-8), etc.
- `locVar_W:N` is a local at frame offset N.

## When NOT to RE

If the question is "what's `create_program` supposed to do?", **stop** and read
`../eob3_research/runtime/RTOBJECT.C` instead. The SOP only describes how the bytecode
*uses* runtime functions — not what they should do. See the [aesop-runtime](../aesop-runtime/SKILL.md)
skill.

If the question is "what does CHGEN.EXE / CHARCOPY.EXE / CINE.EXE actually do?",
that's a different skill again — those are DOS 16-bit binaries beside `EYE.RES`
and Ghidra/radare2 are the tools. See [dos-exe-re](../dos-exe-re/SKILL.md).
