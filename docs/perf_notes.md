# Perf notes + diagnostic env vars

History of performance gotchas hit during bring-up, the fixes applied, and the
`THIRDEYE_*` env vars that surfaced them. Keep these instruments — they're cheap when off
and the next silent-stall hunt will need the same toolkit.

## Diagnostic env vars (cheap-when-off; in `engine.cpp` + `vm/`)

| Env var | What it does |
|---|---|
| `THIRDEYE_PERF=1` | Per-present `{draws, total drawImage µs, present ms, gap}` to stderr. |
| `THIRDEYE_TIMING=1` | Log when each *runtime function* is first called, with elapsed ms since launch. `=2` logs every call (verbose; pinpoints exact spend). |
| `THIRDEYE_SENDS=1` | Log SEND throughput + top message-ids every 100 ms. A tight SEND loop is invisible to the runtime trace (SEND is an opcode not a CALL). |
| `THIRDEYE_SLOWSEND=1` | Log any single SEND whose handler took >50 ms (with class name + nesting depth). Tells you which `(class, message)` is the hot loop. |
| `THIRDEYE_SLOWOP=1` | Log any single *opcode* (CALL/SEND/etc.) that took >50 ms. Pinpoints a single slow runtime function (this is what caught `set_mouse_pointer`). |
| `THIRDEYE_VMSTEPS=1` | Log cumulative VM step count every 100 ms wall-clock with current PC. Catches a silent hot loop (no runtime calls, no SENDs visible). |
| `THIRDEYE_DUMP=/path/frame.bmp` | `saveScreenshot` every present frame. If the path contains `%`, it's printf-formatted (e.g. `/tmp/frame_%04d.bmp` = numbered frames). |
| `THIRDEYE_AUTOWALK=<hex>[,<hex>,...]` | Comma-separated SYS_KEYDOWN script (one code per ~40 pumps; last value repeats). E2E/regression lever: drive the engine headless from boot through menu navigation into gameplay. Common codes: `4800`=fwd, `5000`=back, `4b00`/`4d00`=strafe L/R, `4700`/`4900`=turn L/R, `0d`=Enter, `1b`=Esc. Example: `5000,0d,0d,4900,4800,4800,4800,4800` = title-menu Down → Enter (Continue) → Enter (load save) → turn-right → walk fwd ×4. Pair with `THIRDEYE_DUMP=/tmp/f_%d.bmp` to capture frames per present. |
| `THIRDEYE_AUTOKEY=<scancode>` | Push an SDL_KEYDOWN with that physical scancode every ~40 pumps. `THIRDEYE_AUTOKEY1=1` makes it single-shot. |
| `THIRDEYE_RECORD=1` (or `=<path>`) | Record the session's real keypresses + clicks and print them at exit as a ready-to-paste `THIRDEYE_AUTOWALK=` line (`=<path>` also writes the raw sequence to a file). The record→replay lever: play a flow once by hand, pin it in a CI/valgrind run. Timing is quantized to AUTOWALK's 40-pump windows (idle windows become `_`, rounded up so replay never fires earlier than the human did); click press edges record as `L<x>:<y>`/`R<x>:<y>` (releases are implicit in replay, so drags don't round-trip). Chargen's separate SDL loop is not captured. SIGINT (the `kill -INT` harness pattern) still dumps — SDL turns it into a quit event. |
| `THIRDEYE_CLICK="x,y[;x,y;...]"` | Inject a click sequence (logical coords). `THIRDEYE_CLICK1=1` plays once and stops. |
| `THIRDEYE_MAZE=x0,y0,x1,y1` | Dump maze cells in the given rect on level load. |
| `THIRDEYE_PARTY=x,y,fdir` | Seed party position + facing for the new game. |
| `THIRDEYE_EOB2_SAVE=<path>` | CHARCOPY.EXE stand-in: copy an EOB2 save (EOBDATA?.SAV/FINAL.SAV) to `TRANSFER.SAV` beside the .RES at boot, so "Summon the Heroes of Darkmoon" (menu option 3) can import the party. EOB1 saves are rejected (wrong record layout — matches the original CHARCOPY's refusal). |
| `THIRDEYE_MONTRACE=1` | Trace creature SEND dispatch (combat msgs 163/77/78/71/162/43/82/55/22/235 + AI 91/85/107/99 on monster-range objects). Format: `[mon-msg] idx N msg M handler C arg…`. Capped at 600 lines. |
| `THIRDEYE_TESTOBJ=1` | Spawn a Mausoleum skull door in front of the party. |
| `THIRDEYE_NO_OBJECTS=1` | Skip `loadLevelObjects` (sanity check). |
| `THIRDEYE_REGIONS=1` | Dump every `assign_subwindow` rect. |
| `THIRDEYE_TEXTDBG=1` | Log every text draw's window rect + cursor + string. |
| `THIRDEYE_MOUSE=1` | Log each mouse event → logical coord mapping. |
| `THIRDEYE_ATTACK=1` | Enable + drive auto-attack and log wraith HP + PC hands. |
| `THIRDEYE_QUIT_AFTER_FIRST=1` | Exit right after the first frame (for `time` measurement). |

## Perf gotcha — the runtime-call trace is gated behind `--debug`

`defaultRuntimeCall` logs one line per runtime call (`CALL <fn>(args) [result]`), and the
boot makes ~190,000 of them. Redirected to a file that's ~0.2s, but to a *live terminal*
each flushed line blocks the main render thread — making the SDL window appear to take
~8s to show its first frame. The trace is off unless `--debug` (file-scope `gRtTrace` gate
+ `rt()` null-sink stream in `engine.cpp`; the readCodeString-per-arg prefix is skipped
entirely, since that decode is the trace's real CPU cost). Result: first frame in ~0.2s.
The trace still turns on with `--debug` (or the `THIRDEYE_AUTOWALK`/`TESTOBJ`/`CLICK`
/`AUTOKEY` debug env vars).

## Perf — SYS_TIMER must be heartbeat-gated, or the loop redraws flat-out

The kernel's main loop polls `dispatch_event` with no rate limit, relying on a `SYS_TIMER`
event to drive its ~30 Hz "draw view" redraw. `pumpHost` posts that timer (`postTimer`,
`SDL_GetTicks()>>5`). Originally it re-posted on *every* poll, so the timer was
**perpetually pending** → timer-tick (and the full dungeon redraw it drives) fired on
*every* iteration: the loop pegged a core and animations ran ~3× too fast. Fix
(`EventSystem::postTimer`): only inject a tick when the heartbeat actually advances
(`mLastTimerBeat` guard). Now the loop idles (`SDL_Delay`) between ticks — **menu CPU
dropped 100% → ~12%**, and the rate is correct.

## Per-frame render hot-path (keypress→render latency)

The in-game view re-runs the full draw bytecode every present (~15 draws per frame:
walls, floor, ceiling, objects), so per-draw cost is multiplied by ~30 Hz and felt
directly as input latency. Three fixes, all in `graphics/graphics.cpp`:

1. **Decoded-shape cache** (`Graphics::mShapeCache`, keyed by `{source vector data ptr,
   index}`). Was: every `drawImage` constructed a `Bitmap` *and* RLE-decoded the indexed
   pixel buffer from raw bytes — the same wall shapes hit dozens of times per frame. Now:
   decode once, reuse forever (resource vectors live in `Resource::mAssets` and aren't
   reallocated, so the data pointer is stable). Mirroring stays per-draw on a copy so the
   canonical cache entry isn't mutated; the common `mirror=0` path skips the copy
   entirely. Steady-state in-game total `drawImage` time dropped ~**100×** (tens of ms
   per frame → <1 ms once warm); the first frame at a new view position still pays the
   one-time decode for any not-yet-seen shape.
2. **Rect-scoped backdrop mirror** (`Graphics::drawImage`). The backdrop snapshot
   (`mBackdrop`, used by `setTextWindow` "restore background" so HUD text overlays panel
   art) was blitted full-screen on *every* `drawImage` — a 320×200×32-bit blit ≈
   250 KB/draw. With ~15 in-game draws per frame at 30 Hz that's ~110 MB/s of pure
   memcpy, the dominant per-draw cost. Now we blit only the rect just drawn, intersected
   with the active SDL clip rect.
3. **Persistent streaming texture** (`Graphics::mPresentTex` + `update()`). The present
   path did `SDL_CreateTextureFromSurface` + `SDL_DestroyTexture` *per call* — a GPU
   texture alloc + full format-converted upload + free every event pump (~30 Hz). Now a
   single `SDL_PIXELFORMAT_ARGB8888` streaming texture refreshed with `SDL_UpdateTexture`.

*Pending:* (a) pre-warming the cache on level load; (b) a Release/-O2 build flavor (the
default is `Debug` -O0 — another 5–10× of headroom for free).

## Boot-latency disaster — `set_mouse_pointer` decoding a 155 MB garbage shape (17 s → 150 ms)

The boot from `--skip-menu` to a fully drawn in-game HUD was taking **~17.5 seconds**
(one cold `gfx.update()` to first interactive frame). Looked like interpreter slowness;
was actually a single runtime call.

The chain (found via `THIRDEYE_SLOWOP=1` — "log any opcode whose `vm.execute()` takes
>50 ms"): `kernel.restore_game` (M:231) → `kernel.set_pointer` (M:153) →
`set_mouse_pointer(186 [Icons], kernel.report(2000), …)`. The kernel's default `report`
returns its arg verbatim, so the shape index became **2000** — an Icons resource (~100
shapes) has no shape 2000.

`Bitmap::mBitmapOffets` is `std::map<uint16_t, uint32_t>`, so `mBitmapOffets[2000]`
silently **default-constructed an entry with value 0**. `getWidth`/`getHeight` then read
the bitmap's `"1.10"` magic bytes as the shape's boundsy/boundsx → ~12000 × 12000 pixels
(~155 MB), and the inner RLE decoder spent ~15 seconds walking that garbage. `loadMouse`
would then have scaled it by `--scale=4` → a 50 000 × 50 000 × 32-bit cursor surface.

Three-line fix (`bitmap.cpp` `inRange` guard on `operator[]`/`getWidth`/`getHeight` +
`graphics.cpp` `loadMouse` early-return when `index >= count`): out-of-range shape
lookups now throw or no-op instead of decoding garbage. **Boot bytecode now completes
in ~150 ms**, first interactive frame matches.

*Lesson:* `std::map::operator[]` is a foot-gun for sparse-index containers; `.at()` or
an explicit `.find()` would have surfaced this immediately.

## Design note — the main loop is a busy-wait, and that's faithful

The kernel's loop is `while (!quit) dispatch_event();` — under DOS it span the CPU while
keyboard/timer ISRs injected events into the queue asynchronously. We can't change it
(it's the game's own bytecode). The host seam in `engine.cpp pumpHost` makes the
polled `dispatch_event`/`peek_event` pump SDL each call — translate input → AESOP
events, present, and `SDL_Delay(~10ms)` when the queue is idle. That turns the 100% spin
into an event-driven, frame-paced loop (~6% CPU, renders live, wakes on input).
