---
name: aesop-runtime
description: How to implement or fix an AESOP runtime function (`CALL` target) in thirdeye. Always read the John Miles AESOP/32 C source under `../eob3_research/runtime/*.C` *first* — it's ground truth for what every runtime function should do; our job is to make `defaultRuntimeCall` in [apps/thirdeye/engine.cpp](../../../apps/thirdeye/engine.cpp) match. Use whenever you're adding, fixing, or chasing down a runtime function: triggers include "runtime function", "runtime CALL", "stubbed", "unimplemented", "C:foo", "implement CALL", "defaultRuntimeCall", "what does post_event/cancel/dispatch_event/create_program/destroy_object/etc. do", "stuck on a CALL", or "the bytecode expects a runtime that does X".
---

# Implementing a runtime CALL

We are an AESOP-compatible runtime. EYE.RES is data we execute. When something misbehaves,
the bug is almost always in *our* runtime — a stubbed CALL, a wrong opcode, an
uninitialized static that the original loader would have set. **Never edit the SOP.**

## Step 1 — read the original C

John Miles released the AESOP/32 runtime under `../eob3_research/runtime/*.C`:

| File | What's in it |
|---|---|
| `RTOBJECT.C` | `create_program`, `destroy_object`, `create_object`, `create_SOP_instance`, `flush_cache`, `thrash_cache` |
| `RTCODE.C` | generic helpers: `dice`, `rnd`, `absv`, `minv`/`maxv`, math/string primitives |
| `RTRES.C` | resource handle/lock layer (`RTR_*`) |
| `RTLINK.C` | inter-object link layer (`SXAS`/`SOLE` plumbing) |
| `RTSYSTEM.C` | OS/system glue |
| `EVENT.C` | `post_event`, `cancel`, `dispatch_event`, `peek_event`, the AESOP event queue |
| `GRAPHICS.C` | bitmap/region/window/palette CALLs |
| `MOUSE.C` | mouse cursor + region hit-testing |
| `INTERP.C` | the VM itself — opcode dispatch, `RT_execute`, stack mechanics |
| `EYE.C` | EOB3-specific CALLs (dungeon view, party, save/load) |
| `INTRFACE.C` | EOB3 UI helpers |
| `BSHAPE.C`, `GIL2VFX.C` | sprite/animation pipeline |
| `SOUND32.C` | sound + XMIDI |
| `DEFS.H` | message tokens (`MSG_CREATE=0`, `MSG_DESTROY`), type defs, key constants |

Open the C — even when it's `data`-typed by `file(1)` (Watcom DOS line endings), it reads
fine. The signature `LONG cdecl foo(LONG argcnt, ARG1, ARG2, …)` tells you everything:
return type, arg count, arg sizes, argument order.

## Step 2 — match it in our runtime

Our dispatch lives in `defaultRuntimeCall` in
[apps/thirdeye/engine.cpp](../../../apps/thirdeye/engine.cpp). Existing pattern:

```cpp
if (fn == "cancel" && args.size() >= 4) {
    events.cancel(args[0], static_cast<uint32_t>(args[1]), args[2], args[3]);
    return 0;
}
```

Conventions:
- `fn` is the C-side name (matches the SOP's `C:name` import). Case-sensitive.
- `args[i]` are `VM::Value` (signed 32-bit). The C source's `LONG`/`ULONG`/`WORD`/`BYTE` all
  arrive as `Value` — cast at the boundary.
- Argument order matches the C signature. The SOP pushes left-to-right, so `args[0]` is the
  first C param after `argcnt`.
- Return value goes back through `Value`. `void` C functions return 0.

## Step 3 — sanity-check against the SOP that calls it

Use the [sop-debug](../sop-debug/SKILL.md) skill to disassemble the caller and confirm:
- How many args the bytecode actually pushes (count `PUSH`/load pairs before the `CALL`).
- What return value it expects (often it stores into a `locVar_` immediately; sometimes it
  uses it as a flag for `BRT`/`BRF`).

The `RCRS` opcode resolves the runtime function by name from the import table just before
the `CALL`. The number after `CALL` is the arg count *the bytecode is passing*. If our
implementation reads more, we corrupt the stack on return.

## Step 4 — common gotchas

- **Don't half-implement.** A stub `return 0` for a fn that's supposed to allocate something
  causes downstream SENDs to look up obj 0 and clobber it. Either implement properly or
  return `-1` and log loudly.
- **`THIRDEYE_TIMING=1`** prints the first call time of each runtime fn. New stubs show up
  here — a great way to find what's silently being relied on.
- **Static initialization the original loader did** — the bytecode often assumes "fresh"
  state. Example: `NPC.die` loops on `W:carried != -1`, but `create_program` only zero-fills.
  The level loader (`LVLnn.TMP` reader) sets carried=-1; bypass it (e.g. `THIRDEYE_ATTACK`
  synthetic spawn) and you have to set it yourself.
- **Don't trust daesop's `; possible reference to ... (code)` comments** — those are
  heuristic. A bare integer might be an object index, not a class number.

## When the C is silent

The runtime C doesn't cover *game logic* — combat formulas, item placement, dungeon-event
dispatch. That's bytecode in EYE.RES. If the C has nothing on a topic, you're looking at
the wrong layer: use [sop-debug](../sop-debug/SKILL.md) to read the relevant handler, then
work backwards to figure out which runtime contract the bytecode is leaning on.
