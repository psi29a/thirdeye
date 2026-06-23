---
name: thirdeye-diagnostics
description: How to diagnose thirdeye at runtime — `THIRDEYE_*` env vars, `--debug` VM trace, Debug-vs-Release build config (always under `build/`), the `[mon-msg]`/`[atk]`/`[xfer]` log formats, and the standard combat-debug incantation. Use whenever something's broken/slow/silent and you need to see what the VM or runtime is doing: triggers include "debug", "trace", "diagnose", "stuck", "slow", "stall", "what's the engine doing", "THIRDEYE_", "no hit lands", "combat doesn't work", "monster doesn't die", "frame dump", "autowalk".
---

# Diagnosing thirdeye at runtime

## Build config

All builds live under `build/` — no separate `build-release/` tree. Switch between
configs by re-running `cmake` against the same dir:

- Debug -O0 (default): `cmake -S . -B build -G Ninja`. Use for VM correctness work
  (gdb-friendly, asserts on).
- Release: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`. **5–10× faster
  interpreter** — use for anything that requires running long enough to observe behavior
  (combat, AI, perf).

After editing engine code:
```sh
cmake --build build 2>&1 | tail -5
```

## Env vars (highest signal first)

| Var | What it does |
|---|---|
| `THIRDEYE_ATTACK=1` | Spawn a target monster in front of the party, enable kernel `auto-attack` (M:208), force `B:auto_attack` on each PC, clear `B:h_stat` recovery each tick. Logs `[atk] mon N hp H (live=L)` and `[hand] pc P R=N L=N`. |
| `THIRDEYE_CONTINUE=1` | Opt into `SAVEGAME/ITEMS.TMP` for party position on `resume_level`. Off by default (chargen-transferred party would land at the saved party's spot — a mismatch). Will become unconditional once PC-record restoration lands. |
| `THIRDEYE_MONTRACE=1` | Trace SEND dispatch for combat-relevant messages (163/77/78/71/162/43/82/55/22/235 + AI 91/85/107/99 on monster-range objects). Format: `[mon-msg] idx N msg M handler C arg0=… arg1=…`. Capped at 600 lines — bump in [vm/objects.cpp:301](../../../apps/thirdeye/vm/objects.cpp#L301) if you need more. |
| `THIRDEYE_MONCLASS=N` | Override the `THIRDEYE_ATTACK` target. `1904`=sword wraith (bit-6 incorporeal), `1934`=troll (normal melee), `1943`=warrior shade, etc. Pair with `THIRDEYE_MONHP=N`. |
| `THIRDEYE_DUMP=/tmp/frame_%04d.bmp` | Snapshot every present (use `%` to number; otherwise overwrites). The fastest way to see what's actually on screen when X-forwarding is awkward. |
| `THIRDEYE_AUTOWALK=4d00` | Inject a scripted scancode every ~40 pumps: `4800`=fwd, `4b00`=L, `4d00`=R, `5000`=back. Drives the dungeon without you needing focus. |
| `THIRDEYE_SLOWOP=1` | Log any single opcode (`CALL`/`SEND`) that took >50 ms. The first reach when "the game is stalled but not crashed". |
| `THIRDEYE_TIMING=1`/`=2` | `=1` logs first-call time of each runtime fn (handy for boot-cost analysis). `=2` logs every call (very noisy, redirect). |
| `THIRDEYE_PERF=1` | Per-present `{draws, drawImage µs, present ms, gap}`. For render-pipeline tuning. |
| `THIRDEYE_SENDS=1` | Per-100ms SEND counts by message id. Tells you what the VM is spinning on when the runtime trace looks quiet. |
| `--debug` | VM step trace + runtime CALL trace. Extremely verbose; always redirect (`> /tmp/x.log 2>&1`). |

## Standard "combat-doesn't-work" recipe

```sh
THIRDEYE_MONTRACE=1 THIRDEYE_ATTACK=1 THIRDEYE_MONCLASS=1934 \
  build/thirdeye.app/Contents/MacOS/thirdeye ../data/EYE.RES \
  --skip-intro --vm --skip-menu --scale=2 > /tmp/atk.log 2>&1 &
PID=$!; sleep 10; kill $PID 2>/dev/null; wait 2>/dev/null
grep -E "atk\] mon|mon-msg" /tmp/atk.log | head -40
```

10 seconds of release-build is enough to see HP drop and `die`/`remove` fire. Expected chain
for a normal target: `163→77→71→162→…→82→55→22`. For bit-0x40 incorporeal: `163→77→71→55→22`
(weapon's `hand-to-hand attack` line 866 SENDs `die` directly, skipping damage roll).

## Quick "is the bytecode running at all?" trick

If `[atk]` never logs, the kernel loop isn't pumping — usually a stalled CALL. Reach for
`THIRDEYE_SLOWOP=1` first. If you see `[atk]` but PC msg 163 never fires, `auto_attack` is
0 or the kernel isn't dispatching — check `B:auto_button` (kernel offset 260).

## Common gotcha: `THIRDEYE_MONTRACE` cap

The `[mon-msg]` trace stops after N messages (currently 600, used to be 60 and that hid a
real kill chain). If `die`/`remove` "doesn't fire" but HP is dropping, suspect the cap
before you suspect the bytecode.
