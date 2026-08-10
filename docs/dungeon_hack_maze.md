# Dungeon Hack — MAZE.EXE (random dungeon generator)

Reverse-engineering notes for `MAZE.EXE`, the DOS binary Westwood ships beside
`HACK.RES` that generates each new game's dungeon. Everything here is derived
from `dh_research/dh/MAZE.EXE` (52,672-byte MS-DOS MZ, Borland C++ 1991), the
side-by-side data files, `HACK.BAT`, and the DH SOP's consumer-side calls in
`HACK.RES`'s `dungeon` object.

Raw RE artifacts (r2 disasm, strings, xrefs, `int 21h` sites) live in
[`../../dh_research/MAZE/`](../../dh_research/MAZE/) — same layout as
`../eob3_research/CHARCOPY/`. Regenerate via the commands in that dir's
README. Read this document for the *interpretation*; go there for the
raw evidence when you need to dig deeper.

## Where it sits in the boot flow

From `HACK.BAT`:

```batch
:CONTINUE
aesop hack phase-one
if ERRORLEVEL 3 goto CONTINUE     ; re-run phase-one
if ERRORLEVEL 2 goto CHECKDEMO    ; re-run intro then phase-one
if ERRORLEVEL 1 goto EXIT
cd savegame
..\maze %1 %2                     ; <-- run FROM inside savegame/
cd ..
if ERRORLEVEL 1 goto EXIT
aesop hack phase-two              ; consumes what maze produced
if ERRORLEVEL 1 goto EXIT
goto CONTINUE                     ; back to phase-one after a game ends
```

So MAZE runs *from inside `savegame/`* — its cwd-relative reads/writes all
land there. Two optional command-line arguments (from the batch's `%1 %2`)
pass through; a real player normally invokes MAZE with none. Exit code 1
means "give up, quit the game."

## Identity

Strings pulled from the binary (`strings -a -n 4 MAZE.EXE`):

```text
Borland C++ - Copyright 1991 Borland Intl.
Random Dungeon Generator v1.0/386  Event Horizon Software Inc.
Divide error
Abnormal program termination
```

"Event Horizon Software" is the DH studio credit. `/386` is the Borland
target flavour (386 minimum, but the binary itself is 16-bit MZ — the
32-bit path lives elsewhere).

## Files

MAZE opens `SETTINGS.DAT` (in the current directory, i.e. `savegame/`),
optionally overrides settings from `argv`, and writes four artefacts:

| File | Direction | Format | Notes |
|---|---|---|---|
| `SETTINGS.DAT` | in | 4B seed + 12B struct | dungeon-gen params (§SETTINGS.DAT) |
| `LEVELS.DAT` | out | 4B seed + `(DEPTH+10)`×0x400 | per-level 32×32 tile map (§LEVELS.DAT) |
| `FEA%02d.DAT` | out (per level) | stream of 8-byte records | feature records (§FEA) |
| `ITEMS.DAT` | out | stream of **8-byte** records + zero terminator | initial item placements (permuted from a 5-byte source record) |
| `SEED.TXT` | out | ASCII | seed + settings for repro |

Error strings that anchor the writer paths:

```text
Unable to create file %s
Unable to open parameter file SETTINGS.DAT
Invalid character '%c' in command line settings "%s"
ERROR: Adjacency door memory exceeded
ERROR: Unable to allocate memory
Seed=%08lX Settings=
[LEVEL %2d]
ENTRY=(%d,%d)
FDIR=%d
```

The `[LEVEL %2d]` / `ENTRY=(%d,%d)` / `FDIR=%d` block goes into `SEED.TXT`
(human-readable dump; the row-number ruler `01234567890123456789012345678901`
is the tile-grid header for a 32-column ASCII map dumped alongside).

## `SETTINGS.DAT` — dungeon-gen parameters

Layout confirmed against `FUN_1325_4189` (r2 `0x73d9`) — MAZE does two
freads: a 4-byte `SEED` (returned via eax:edx to main for the SEED.TXT
header) followed by a 12-byte settings struct. Any bytes past offset 16
are ignored by MAZE.

**The DH SOP kernel reads SETTINGS.DAT with a different (wider) layout.**
In `HACK.RES/kernel` (see `/tmp/hack_dasm/kernel.dasm` after running the
bulk daesop), the create-handler does:

```text
open_file("SAVEGAME\SETTINGS.DAT")
read_array_from_file(&B:setting, 4)    ; read+discard first 4 bytes (MAZE's SEED)
read_array_from_file(&B:setting, 19)   ; overwrites with 19-byte setting struct
read_number_from_file(4) -> L:189      ; read a 4-byte long into staticVar189
close_file()
```

Total 27 bytes — matches the shipped `dh/SAVEGAME/SETTINGS.DAT` exactly.
So MAZE (16 bytes) and the SOP kernel (27 bytes) use **different formats**
for the same file. Consequence: MAZE-generated SETTINGS.DAT would be
short from the kernel's perspective; our mini-MAZE seed only touches the
files MAZE writes (never SETTINGS.DAT), so the shipped 27-byte file the
kernel needs remains intact.

| Off | Size | Field |
|---|---|---|
| 0  | 4 | `SEED` (u32 LE — passed to `Seed=%08lX` in SEED.TXT) |
| 4  | 1 | `DEPTH` (number of levels to generate) |
| 5  | 1 | `FREQ_MONSTERS` |
| 6  | 1 | `FREQ_TREASURE` |
| 7  | 1 | `FREQ_ILLUSIONARY_WALLS` |
| 8  | 1 | `FREQ_FOOD` |
| 9  | 1 | `FREQ_KEYS` |
| 10 | 1 | `FREQ_TRAPS` |
| 11 | 1 | `FREQ_PITS` |
| 12 | 1 | `HINT_SHEET_FREQ` |
| 13 | 1 | `ZONES_ON` (0=off; any non-`' '` value = on — Borland cmdline idiom) |
| 14 | 1 | `WATER_ON` |
| 15 | 1 | `MULTI_LEVEL_PUZZLES_ON` |

Verified against the shipped `dh/SAVEGAME/SETTINGS.DAT`:

```text
e0 56 01 00 | 0f 07 00 00 07 07 07 07 00 01 01 00 | 01 07 07 01 07 00 01 2e 00 00 00
   SEED         DEPTH ................booleans      (ignored past offset 16)
   0x156e0      15 7  0 0 7 7 7 7 0  1 1 0
```

= `SEED=0x000156e0`, `DEPTH=15`, `FREQ_MONSTERS=7`, `ZONES_ON=1`,
`WATER_ON=1`, `MULTI_LEVEL_PUZZLES_ON=0`.

## `LEVELS.DAT` — two different files, same name

**Important**: `dh/LEVELS.DAT` and `dh/savegame/LEVELS.DAT` are
structurally different files. MAZE only writes the savegame one (it
runs from `savegame/`); the game-root copy is a phase-one preview.

### `savegame/LEVELS.DAT` — MAZE-generated (runtime dungeon)

Structure (from `FUN_1325_4053` = r2 `0x72a3` writer):

```text
[4 bytes: the u32 seed  (fwrite of &DAT_1777_4ba4)]
[(DEPTH+10) × 0x400 bytes: one 32×32 tile map per level]
```

Each 0x400-byte chunk **is** the level's tile map: a 32×32 row-major
grid, one byte per cell, indexed `grid[y * 32 + x]`.

> **Correction (2026-08-07).** An earlier reading of this document
> called the chunk "a per-cell entropy mask" produced by
> `random(0..255)`. Both halves were wrong, and the cause is worth
> remembering: `FUN_1766_00c1` takes **two** 16-bit arguments that
> Borland pushes as one dword, so Ghidra renders `random(0, 1)` as the
> single literal `FUN_1766_00c1(0x10000)`. Read as one argument it
> looks like a 64K-wide roll; read correctly it is a coin flip picking
> between two wall textures. Any `FUN_1766_00c1(0xNNNN0000)` in this
> binary is `random(0, 0xNNNN)`.

The last pass before the write, `FUN_1325_4017`, is what turns the
generator's working buffer into the on-disk encoding:

```c
for (i = 0; i < 0x400; i++)
    buf[i] = (buf[i] == 0xDB) ? random(0, 1)   /* wall, 2 texture variants */
                              : 0xFF;          /* open floor              */
```

`0xDB` is the generator's "undecided rock" sentinel. Everything the
generator carved (or left as scaffolding) becomes floor; everything
still sealed becomes a wall. That matches the encoding the SOP
consumes — `lvlmap == -1` (0xFF) means "no wall here" — so
`load_level_map(&lvlmap, party_lvl)` reading chunk `party_lvl` at
offset `4 + party_lvl * 0x400` hands the bytes straight to
`draw_walls` with no further interpretation.

### `dh/LEVELS.DAT` — shipped preview (52,286 bytes)

Structurally different from the runtime one — a 6-entry container of
variable-sized level payloads. Header (decoded by hand from the first
32 bytes):

```text
00: 06 00 00 00                          u32 num_levels = 6
04: 1c 00 00 00                          u32 offset[0] = 0x001C
08: 9c 22 00 00                          u32 offset[1] = 0x229C
0c: 37 2c 00 00                          u32 offset[2] = 0x2C37
10: 69 56 00 00                          u32 offset[3] = 0x5669
14: b8 7f 00 00                          u32 offset[4] = 0x7FB8
18: 3e a8 00 00                          u32 offset[5] = 0xA83E
1c: <level 0 payload begins>
```

Level sizes (offset[n+1] − offset[n], last one to EOF):

| Level | Bytes |
|---|---|
| 0 | 8,832 |
| 1 | 2,459  (small — hub / puzzle?) |
| 2 | 10,802 |
| 3 | 10,575 |
| 4 | 10,374 |
| 5 | 11,144 |

Each level payload starts with a 12-byte header, comparing across all
6 levels in the shipped file:

| Level | Bytes 0-11 (as 6× u16 LE) | Interpretation |
|---|---|---|
| 0 | `3a 3a ffff 11 60 0` | `w=58, h=58, pA=-1, pB=17, pC=96, pD=0` |
| 1 | `0c 0c 4 5 60 0`     | `w=12, h=12, pA=4,  pB=5,  pC=96, pD=0` |
| 2 | `40 40 3 0 60 d00`   | `w=64, h=64, pA=3,  pB=0,  pC=96, pD=0xD00` |
| 3 | `40 40 0 3 60 d00`   | `w=64, h=64, pA=0,  pB=3,  pC=96, pD=0xD00` |
| 4 | `40 40 ffff 11 60 0` | `w=64, h=64, pA=-1, pB=17, pC=96, pD=0` |
| 5 | `40 40 ffff 11 60 0` | (same as level 4)                       |

- **bytes 0-1: u16 width, bytes 2-3: u16 height** — square in all shipped
  levels (12, 58, or 64), plausible dungeon grid sizes.
- **bytes 4-5, 6-7: pA, pB** — likely entry-point (x,y) or a link to
  another level. When `pA == 0xFFFF` (levels 0/4/5), `pB` is a small
  const `0x11` — looks like a sentinel/mode. When both are small
  (levels 1/2/3), they read as coordinates.
- **bytes 8-9: pC = 0x60** — constant across all levels. Probably a
  tile-value marker used later as sentinel/default in the payload
  (which is dense in `0x60`/`0x62` u16s).
- **bytes 10-11: pD** — either 0 or 0xD00; a flag word.

Post-header is a u16 stream of small values (0x27..0xCE range) — these
index into a merged feature/tile table (feature-name table has ~40
entries, so a byte-index would suffice; using u16 leaves room for
flags in the high byte). Cell count doesn't divide the payload
cleanly (`58×58×2 = 6728` vs 8820 payload bytes for level 0), so
there's post-grid data (features / spawns / doors) appended.

Full per-payload decode is left for when someone actually wants the
phase-one preview to render — the runtime dungeons come from MAZE's
`savegame/LEVELS.DAT` (entropy format above), not this file.

Loaded from the SOP with `load_level_map(&lvlmap, party_lvl)` (2 args,
returns a long). The two `open_file(name)` + `read_number_from_file` /
`read_array_from_file` primitives are the underlying reader; DH's own
`load_level_map` is a wrapper that walks the offset table.

## `FEA%02d.DAT` — feature records, one file per level

Consumed by three DH runtime calls (from `dungeon`'s import table):

```text
C:open_feature_file          ; 1 arg (level index / seed passed in)
C:get_feature_record         ; 1 arg (dest buffer &fea_in)
C:close_feature_file         ; 0 args
```

Written by `FUN_1325_3c76` (r2 `0x6ec6`). Structure per FEA file:

```text
[ header record ]              # 8 bytes; byte 0 = 0 marker + per-level bit-packed flags
[ body record ] × N            # 8 bytes each; byte 0 = feature-type (one of 22)
[ terminator record ]          # 8 bytes, all zeros
```

Outer loop iterates `(DEPTH+10)` levels, each opening a fresh
`FEA%02d.DAT`. Inner loop iterates over the per-level record list
whose count comes from `DAT_1777_467e[level]` (a u16 array indexed
`shl bx, 1`) and whose element pointer comes from
`DAT_1777_46b0[level]` (a far-pointer table indexed `shl bx, 2`).
Source record stride is **9 bytes** (from `imul dx, 9`).

Inside the inner loop is a switch on some byte of the source record
(range 0..21, so 22 distinct feature types). Each switch case
packs the 8-byte output buffer differently — first byte is the
record type marker, remaining 7 bytes are type-specific payload.

**Traced fields for the level header record only** (bytes 1-5, from a
separate 16-byte-per-level array at `DAT_1777_4ba0`):

```text
byte 0: 0                                          (header marker)
byte 1: src16[level*16 + 0x10]
byte 2: src16[level*16 + 0x0f]
byte 3: src16[level*16 + 0x15]
byte 4: (src16[level*16 + 0x08] >> 5) & 1
byte 5: (src16[level*16 + 0x08] & 0x1f) == 0 ? 1 : 0
bytes 6-7: (not traced)
```

The body-record variants dispatch on source byte 8, decremented then
compared against 21 (`cmp bx, 0x15`). Values outside 0..21 fall to
the "default" path (writes an empty record and iterates). The
compiler emitted 22 near-pointer entries at r2 `0x3feb`, but Ghidra's
decompile shows **only ~20 distinct case values are actually reached
in practice**, with some sharing bodies via fall-through (a common
compiler pattern for related feature types):

| Case (dec) | Ghidra decompile line | Body size (lines) | Notes |
|---|---|---|---|
| 0     | 118 | ~24  | Level header — traced (see structure above) |
| 3, 19 | 143 | ~41  | Shared body |
| 4, 20 | 185 | ~8   | Shared, small |
| 5, 21 | 194 | ~418 | Shared, large |
| 7     | 613 | ~337 | Big body |
| 8     | 951 | **~1902** | Largest single case — likely the primary feature (monster/decoration placement) |
| 2, 18 | 2853| **~5380** | **Enormous** — hosts nested switches; the meat of the writer |
| 6     | 8233| ~480 | |
| 9     | 8714| ~22  | |
| 10, 11 | 8737 | 2  | Ghidra emitted `halt_baddata()` — decompile broken here |
| 12    | 8743 | ~34 | |
| 13    | 8778 | ~36 | |
| 14    | 8815 | ~4  | |
| 15    | 8820 | ~2  | Trivial |
| 16    | 8823 | ?   | `goto` to case 10's target |
| default | 120 | ~21 | Empty output, several helper calls; likely the "unclassified feature" path |

**Ghidra caveat.** `FUN_1325_3c76` was flagged with "control flow
encountered bad instruction data" and "Instruction at 0x0001c484
overlaps instruction at 0x0001c483" — the resulting pseudocode is
unreliable for cases 3+ (some bodies decode as calls to `swi(0x21)`
INT 21h, which is nonsense for a field packer). **Trust the raw
disasm in `../../dh_research/MAZE/disasm.txt`, not the pseudocode
in `decompile.txt`, when tracing individual case bodies.** Each case
target lives at r2 offset given by the word at `0x3feb + 2*case`.

Per-case field-by-field byte mapping is left as a follow-up — it's
mechanical but hours of work per case for the large ones. The
reader side (`get_feature_record`) doesn't need any of it (fixed
8-byte read); thirdeye only needs per-case field semantics when we
start *interpreting* FEA feature records for dungeon-render or
gameplay logic. For now, phase-two consumes zero-content FEA files
seeded by the native mini-MAZE and dispatches records with type-0
headers only — sufficient to keep the tick loop running.

`fea_in` on the SOP side is an extern byte array on the `dungeon`
object at offset 9259; `get_feature_record(&fea_in)` reads 8 bytes.

## Feature type table (record kinds)

Strings section contains the feature-type name table, in two groups:

**Group A — items / tokens** (indices 0–?):

```text
0  hint sheet
1  rations
2  treasure
3  special door item
4  extra-cool object
5  coin
6  grappling hook
7  amulet of return
8  helm/water
9  +1 magic weapon
```

**Group B — dungeon features**:

```text
current door        floor pit
door frame button   ceiling pit
illusionary wall    magical teleporter
stairs up           spinner
stairs down         current arch
regular button      current window
hidden button       current pillar
current lever       current shelf
keyhole             healer
gem hole            floor decoration
special activator   special window
level creature      special pillar
magic zone
object hole
spell hole
level decoration
solid wall
spike trap
```

The two groups are separated by a `null` sentinel in the strings dump. The
u16 values (0x27, 0x2e, 0x62 etc.) seen in level payloads probably index
into a *merged* table where group A occupies 0..9 and group B starts at 10,
but confirm by cross-referencing the field-load pattern in MAZE.

## `ITEMS.DAT`

Written by `FUN_1325_3bb0` (r2 `0x6e00`) but **never read by any DH SOP
object via `open_file`**. Sweep across all HACK.RES code resources
(dungeon / kernel / cgen / custom / automap) finds these three SOP-side
filename literals only: `SAVEGAME\PC.DAT`, `SAVEGAME\SETTINGS.DAT`,
`SAVEGAME\SETSAVE.DAT`. So the ITEMS.DAT consumer is either the
AESOP.EXE C runtime (an internal init not visible to the SOP layer),
`explode_save`, or the file is genuinely a MAZE-only artifact that
phase-two doesn't consume. Our mini-MAZE still emits it (zero
terminator = 8 bytes) so any consumer that does exist finds a
structurally-valid empty stream.

File format on disk is a stream of 8-byte records terminated by an
all-zero record:

```text
[ record ] × item_count       # 8 bytes each
[ terminator ]                # 8 zero bytes
```

`item_count` = `word[DAT_1777_4714]`, set by the dungeon-gen pass.
Source array is at `DAT_1777_4716` (a far pointer), **5 bytes per
source element**. Each output record permutes the source and
zero-pads to 8:

```text
output byte 0 = source byte 3
output byte 1 = source byte 1
output byte 2 = source byte 0
output byte 3 = source byte 2
output byte 4 = source byte 4
output bytes 5-7 = 0
```

Source field semantics need cross-referencing with the DH SOP's
`items` object externs (`W:itmflags`, `B:bonus`, `B:key_kind`,
`B:spec_kind`, `B:gem_kind`, `B:hint_num`) to name each of the 5
source bytes. That mapping isn't traced yet.

## `SEED.TXT`

Written by `main` at the very end (r2 `0x75c5+`). ASCII dump for
seed-reproduction: `Seed=%08lX Settings=…\n` header + per-level
`[LEVEL %2d]\nENTRY=(%d,%d)\nFDIR=%d\n` blocks + an ASCII map dump
(the `01234567890123456789012345678901` ruler string is the column
header). Non-load-bearing for the game.

## What the game actually reads from SAVEGAME/

Shipped alongside a fresh install (as GoG delivers it):

```text
HISCORE.DAT   1.0K   high-score board (leading u16 count + entries; player
                     "Urlithani Windleaf won" visible in the first record)
HISCORE.DEF   1.0K   default (empty?) high-score table
PC.DAT       33 B    the shipped ready-to-play character: a 20-byte
                     NUL-padded name + a 13-byte stat block. Confirmed
                     from phase-one's own writes -- create_file then
                     write_array_to_file(ptr,20) + (ptr,13). Ships as
                     "Kathra Shallowtaint".
SETSAVE.DAT  19 B    binary snapshot of the last-used SETTINGS
SETTINGS.DAT 27 B    binary SETTINGS.DAT (see §SETTINGS.DAT)
VISIBLE.DAT  25 KB   visibility masks (per-level, consumed by
                     load_visibility(&lvlvis, party_lvl))
```

`VISIBLE.DAT` is what `C:load_visibility` reads — a per-level byte mask of
cells the party has seen. Its shipped 25,600 bytes = 25 × 0x400, exactly
matching the `(DEPTH+10) × 0x400` chunk layout `load_level_map` uses (with
DEPTH=15) — so `load_visibility` reads a 0x400-byte chunk at offset
`party_lvl * 0x400`, no header prefix.

Loaded alongside `load_level_map`:

```text
CALL explode_save(0, &setting, &beento_level) -> L:seed
CALL load_level_map(&lvlmap, party_lvl)
CALL load_visibility(&lvlvis, party_lvl)
```

`explode_save` is a DH-only runtime call (ADDITIONAL list: "auxiliary
when restoring game (unpacking of the save)") — it decompresses / parses
whatever save-blob DH persists across quits. Args: `(compressed_source,
setting_dest, beento_dest)`; returns the level RNG seed. This is likely
LZ-style decompression of a save chunk into two output arrays.

## DOS internals

40+ `int 21h` sites. Only three matter at the application layer:
- **AH=0x3C create** at `0x27b5` — inside `FUN_1000_27a9`, the `_creat`
  primitive.
- **AH=0x3D open** at `0x296d` — inside `FUN_1000_2945`, `_open`.
- **AH=0x40 write** at `0x27d0` — inside `FUN_1000_27c4`, `_write`.

Everything else is Borland C RTL setup (stderr writes for the "Divide
error" / "Abnormal program termination" messages, memory allocation via
AH=0x4A, standard exit). No XMS/EMS driver hits, no timer manipulation.
Pure generate-write-exit.

## MAZE.EXE structure (from the Ghidra decompile pass)

See [`../../dh_research/MAZE/README.md`](../../dh_research/MAZE/README.md)
for the full function table. Highlights:

- `main` (`FUN_1325_4375`) reads `SETTINGS.DAT`, processes argv,
  allocates buffers, generates the dungeon, invokes the write pipeline
  (`fcn.00007329`), then opens+writes `SEED.TXT` and exits.
- Write pipeline calls three writers in order (identities pinned via
  DGROUP push audit — see `../../dh_research/MAZE/README.md`):
  - `FUN_1325_4053` → **`LEVELS.DAT`** — a 4-byte header holding the
    seed, then `(DEPTH+10)` × 0x400-byte **32×32 tile maps**
    (`FUN_1325_4017` finalizes each: `0xDB` → `random(0,1)`, a wall with
    one of two texture variants; everything else → `0xFF`, open floor).
    Not entropy — see the correction in the LEVELS.DAT section.
  - `FUN_1325_3bb0` → **`items.dat`** (DOS is case-insensitive, so this
    is the same file referred to elsewhere as `ITEMS.DAT`; note no DH SOP
    object opens it — see the ITEMS.DAT section) — 5-byte source records
    permuted `[3,1,0,2,4]` into 8-byte output, zero-record terminator
  - `FUN_1325_3c76` → **`FEA%02d.DAT`** (one file per level) —
    8-byte records; the type byte dispatches through a 22-entry jump
    table (r2 flagged it as 10 cases, but the bound is `cmp bx, 0x15`;
    several entries share a body)
- All file I/O goes through the Borland C `fopen`/`fread`/`fwrite`/
  `fclose` layer (Ghidra fns `FUN_1000_210a` / `224f` / `24bf` / `1d6c`);
  `int 21h` primitives (AH=3C create, 3D open, 40 write) are only
  reached from within those.

## The generator itself (2026-08-07)

The piece that was missing for a long time: how the tile maps are
actually built. It is now traced end to end, and the geometry half is
ported natively in
[apps/thirdeye/runtime/dh_maze.cpp](../apps/thirdeye/runtime/dh_maze.cpp).

### The PRNG is R250 — and it has to be exact

`main` never calls the C library's `rand`. Segment `1766` holds a
private generator:

| Ghidra | Role |
|---|---|
| `1766:0005` | `srand(long)` |
| `1766:006e` | `rand()` → u16 |
| `1766:00c1` | `random(lo, hi)` = `lo + rand() % (hi - lo + 1)` |
| `1766:00dd` | `roll(n, sides, bonus)` |

It is **R250** (Kirkpatrick–Stoll lagged-Fibonacci XOR, lags 250/103):

```c
/* srand */
for (k = 0; k < 250; k++) { s[k] = seed >> 16; seed = seed * 0x015a4e35 + 1; }
mask = 0xFFFF; msb = 0x8000;
for (k = 3; k != 179; k += 11) { s[k] = (s[k] & mask) | msb; mask >>= 1; msb >>= 1; }
i = 0;

/* rand */
j = (i < 147) ? i + 103 : i - 147;
v = s[i] ^ s[j];  s[i] = v;
i = (i < 249) ? i + 1 : 0;
return v;
```

Two transcription traps, both of which the unit tests pin:

- The staircase loop's bound is `!= 179`, **not** `< 250`. Running it
  to the end of the array shifts `mask`/`msb` to zero and wipes most
  of the state.
- The state array lives at `DS:0x4d3e` and the *next* global,
  `DS:0x4d38`, is a call counter — easy to mistake for part of the
  ring when reading the decompile.

Getting this right is the whole ballgame: every layout decision is a
draw from this stream, so a stand-in PRNG produces a different dungeon
even with the surrounding code transcribed perfectly.

### Each level is seeded independently

`FUN_1325_375f` (r2 `0x69af`) is the per-level entry point, and its
first instruction is:

```c
srand(seed + level + 1);
```

That is unusually convenient for a port: level *N* can be reproduced
without replaying every draw the program made before it. The `seed`
is SETTINGS.DAT's u32 (and the value echoed into LEVELS.DAT's 4-byte
header and SEED.TXT).

### Zones pick one of five layout algorithms

Before any level is generated, `FUN_1325_3986` assigns each level a
5-bit *zone* in descriptor field `[0]`:

| Level | Zone |
|---|---|
| 0–3 | 1 |
| 4 … DEPTH+8 | `random(0, 2) + 1` → 1, 2 or 3 |
| DEPTH+9 (deepest) | 4 |
| one random non-water level in 4 … DEPTH+8 | 0 |

`FUN_1325_375f` then switches on it (jump table at Ghidra `1325:397c`,
file `0x71cc`):

| Zone | Pipeline |
|---|---|
| 0 | `04ab` → `05a0` → `056a` → `0c1d` → `107a` |
| 1 | `0e3c(grid, 0)` → `107a` |
| 2 | `04ab` → `05a0` → `056a` → `0c1d` → `0aac` → `107a` |
| 3 | `0e3c(grid, 1)` → `107a` |
| 4 | `0e3c(grid, 2)` |

Zones 0 and 2 are the **maze** layouts; zones 1, 3 and 4 run
`FUN_1325_0e3c`, a room-and-corridor generator. If `WATER_ON`, one
level in `[7, DEPTH+8]` additionally gets descriptor bit 5 and a
type-11 feature linking it back to a random shallower level.

### The maze carver (zones 0 and 2) — ported

Three functions, all short and all now in `dh_maze.cpp`:

**`FUN_1325_04ab(level)` — initialise.** Fill the 32×32 grid with
`0xDB`, *except* row 31 and column 31, which get `0xFF`. That is not a
border — it is a sentinel: the carver only steps into `0xDB`, so
pre-marking the last row/column stops it walking off the buffer
without a single bounds check. Then the entry cell:

```c
if (level == 0) { desc[3] = random(0,14)*2 + 1;   /* row */
                  desc[4] = random(0,14)*2 + 1; } /* col */
else            { desc[3] = prev_desc[5] | 1;
                  desc[4] = prev_desc[6] | 1; }   /* previous level's stairs */
```

so levels chain: you enter level *N* where level *N−1*'s stairs came
down, forced onto an odd coordinate.

**`FUN_1325_05a0(grid, row, col)` — carve.** A stackless recursive
backtracker over the odd coordinates `(1,1)…(29,29)` — a 15×15 cell
maze. The trick that removes the stack: each cell stores *the
direction it was entered from* (0–3, or 4 at the root) while the walk
is inside it, and is overwritten with `0xFF` on the way back out.

```c
pos = row*32 + col;  grid[pos] = 4;
d = random(0,3);  d0 = d;
for (;;) {
    while ((np = pos + step[d]) >= 0 && grid[np] == 0xDB) {
        grid[np] = d;                  /* remember the way back */
        grid[pos + step[d]/2] = 0xFF;  /* knock out the wall between */
        pos = np;  d = random(0,3);  d0 = d;
    }
    d = (d < 3) ? d + 1 : 0;
    if (d != d0) continue;             /* try the next direction */
    from = grid[pos];  grid[pos] = 0xFF;
    if (from > 3) break;               /* back at the root: done */
    pos -= step[from];
    d = random(0,3);  d0 = d;
}
```

`step[]` is MAZE's own table at `DGROUP:0x258` — `{+2, -64, -2, +64}`,
i.e. two columns east, two rows north, two columns west, two rows
south. Halving a step lands on the wall cell between two rooms. (The
four words that follow at `0x260`, `{-32, +1, +32, -1}`, are the
unit-step table used elsewhere.)

**`FUN_1325_056a(grid)` — seal.** Put row 31 and column 31 back to
`0xDB` so the finalize pass turns them into wall.

Output, verified against the port (`0x000156e0`, level 0):

```text
 0 ################################
 1 #...#...........#.............##
 2 #.#.#.#####.#####.###########.##
 3 #.#...#...#...#...#.....#...#.##
 4 #######.#.###.#.#####.###.#.#.##
 5 #.......#.#...#.#.....#...#.#.##
 …
30 ################################
31 ################################
```

225 cells, 224 corridors, 449 open tiles, all connected, no loops — a
perfect maze, which is exactly what a correctly transcribed
backtracker must produce and what
[apps/tests/dhmaze_tests.cpp](../apps/tests/dhmaze_tests.cpp) asserts.

### Entry point and facing

`FUN_1325_0c1d` walks the entry cell (via `FUN_1325_033f`) until it
sits in a **dead end** — exactly one open neighbour — writes tile
`0x18` there, and calls `FUN_1325_0bb1` to derive the facing from
which side is open, storing it in descriptor field `[13]`. Those three
values (`[8]`, `[7]`, `[13]`) are what the FEA header record exports
as bytes 1, 2 and 3.

This matters more than it looks: the maze lives on odd coordinates, so
without the header entry the party spawns at `(0,0)` — solid rock. Our
FEA writer emits the entry and derives the facing the same way;
`draw_walls` confirms the party appearing at `(1,5)` facing `1`
(east) and walking cleanly to `(7,5)`, which is exactly row 5 of the
map above.

### The room-and-corridor generator (zones 1, 3, 4) — ported

`FUN_1325_0e3c` (r2 `0x408c`) is a genuinely separate function, not an
entry point inside Ghidra's `FUN_1325_0cdc` — Ghidra merged the two
because `0cdc`'s own jump table sits between them. It places rooms
*first* and then runs the same backtracker over whatever rock is left,
so the maze grows around the rooms rather than the other way round:

```c
initGrid();                                  // 1325:04ab
rooms[0] = { entryRow-1, entryCol-1, 3, 3 }; // keep-out box, never filled
if (mode != 0) {                             // two crossing 1-wide corridors
    c = random(0,4)*2 + 11;  if (c == entryCol) c += 2;
    fill({1, c, 29, 1});                     // rooms[1]
    r = random(0,4)*2 + 11;  if (r == entryRow) r += 2;
    fill({r, 1, 1, 29});                     // rooms[2]
}
if (mode < 2) {                              // scatter odd-aligned rooms
    budget = 60d5;                           // 60..300 cells, mean 180
    while (budget > 0 && roomCount < 20) {
        h = random(1,2)*2+1;  w = random(1,2)*2+1;      // 3 or 5
        row = random(1, 30-h) | 1;  col = random(1, 30-w) | 1;
        if (fits()) { rooms[n++] = t; budget -= h*w; fill(t); }
    }
}
backtrack(entryRow, entryCol);   // 1325:05a0
fillPockets();                   // 1325:06d1 -- re-run from any sealed pocket
sealBorder();                    // 1325:056a
placeEntry();                    // 1325:0c1d
for (i = (mode ? 3 : 1); i < roomCount; i++) punchDoors(rooms[i]);
```

| mode | zone | corridors | rooms | room doors | `107a` top-up |
|---|---|---|---|---|---|
| 0 | 1 | no | yes | `rooms[1..]` | yes |
| 1 | 3 | yes | yes | `rooms[3..]` | yes |
| 2 | 4 | yes | **no** | **none** | **no** |

Mode 2 falls out as a quirk worth knowing: with no rooms the door loop
starts at `rooms[3]` when only three slots are filled, and the zone
switch skips `107a`, so **the deepest level has no doors at all**.

Doors go through one choke point, `FUN_1325_07ce`, which only accepts a
cell whose rock neighbours form a clean passage — mask 5 (rock north and
south) gives an east-west door `│`, mask 10 an north-south door `─`.
Anything else (a corner, a junction, open ground) is refused.

We reproduce one likely bug on purpose: `FUN_1325_08cb`'s perimeter-door
loop starts at `rooms[2]`, but `FUN_1325_0aac`'s first real room is
`rooms[1]`, so the first accepted room on every zone-2 level never gets
perimeter doors. Fixing it would desync us from MAZE.

Three loops in the original can spin forever — `0cdc` and the room
scatter never decrement on rejection, and `033f` re-rolls unbounded. We
cap the attempts. Everything else is transcribed as-is.

### The grid is a CP437 character map

Which is why the tile bytes look arbitrary until you see them as text:
`0xDB █` rock, `0xB3 │` / `0xC4 ─` doors, `0xBA ║` / `0xCD ═` arches,
`0x18 ↑` / `0x19 ↓` stairs, `0xB0 ░` pit, `0xB1 ▒` illusionary wall,
`0xEF ∩` teleporter, and `'A' + region_id` for open floor.
`FUN_1325_0001` dumps the grid verbatim into `SEED.TXT` — and `main`
does that **before** `FUN_1325_4017` collapses everything to
wall/floor.

That makes **SEED.TXT a byte-exact oracle for the port**: run the real
MAZE under DOSBox with a known SETTINGS.DAT, run ours with the same
seed, and diff. A match proves the RNG, the zone table, both
generators, every door pass and the entry/exit placement all at once —
far tighter than comparing LEVELS.DAT, where `4017` has already thrown
the semantics away. We have not been able to do this yet (no DOSBox
run), so *nothing here is validated against real MAZE output* — only
against the disassembly and against structural invariants.

### The feature-placement tail — ported

`FUN_1325_375f` ends with fifteen passes that turn an empty layout into a
dungeon, run in this order (r2 `0x6b40`):

```
2081 → [10a6] → 2132 → 1cfb → 330f → 219b → 299e → 27a6 → 254d
     → 2a81 → 23be → 24e4 → 2415 → 289b → 2b51
```

plus two whole-dungeon passes afterwards, `26f6` (pits, which need two
levels to line up) and `36a2` (the quest objects). All of it is in
`dh_maze.cpp`; the field-by-field reading is in
[`dh_research/MAZE/FEATURES.md`](../../dh_research/MAZE/FEATURES.md).

**Regions are the backbone.** `FUN_1325_2081` floods the level from the
party's arrival cell, labelling each connected patch of floor with its
region id — literally, as the character `'A' + id`, which is why the
grid reads as text. Doors bound regions, and each region records how
many doors lie between it and the entry. That *depth* is what every
later pass steers by, and it is what makes the dungeon solvable rather
than merely random: `FUN_1325_3004` never hides a key deeper than the
shallowest lock that opens with it.

**Counts come from the `FREQ_*` settings** through `FUN_1325_0117`:
roll a percentage, then roll dice, both read from an 8-entry table
indexed by the setting byte. All seven tables are transcribed verbatim
(and verified byte-for-byte against DGROUP). So `FREQ_MONSTERS=7` means
`100% 10d10` monsters per level, and `HINT_SHEET_FREQ=0` means `0%` —
which is why the shipped settings produce no hint sheets at all.

**Two record streams, two type namespaces.** `FUN_1325_121d` emits
9-byte *feature* records (30 types: doors, stairs, traps, pits,
teleporters, pillars, shelves, the healer…); `FUN_1325_11af` emits
5-byte *item* records (12 types: keys, gems, treasure, rations, the
quest objects). They are separate numbering schemes and were conflated
in earlier notes. The writers permute both into 8 bytes on disk.

What a live 25-level dungeon at the shipped settings comes out as:

| | |
|---|---|
| doors / frame buttons | 489 / 43 |
| locks (keyhole, gem hole, activator) | 385 — and **385** matching items |
| creatures | 1147 |
| decorations (wall + floor) | 851 + 190 |
| illusionary walls / solid-wall doors | 105 / 13 |
| arches / windows / pillars / shelves | 81 / 35 / 17 / 72 |
| traps (spike, spinner, holes) | 13 / 16 / 34 |
| pits (floor + ceiling, paired) | 3 + 3 |
| teleporters (always in pairs) | 18 |
| stairs up / down | 25 / 24 |

**Deviations, all deliberate:**

- **The region walk is ours, not MAZE's.** `FUN_1325_13d8`'s frontier
  array was not fully traced, so we reproduce its *shape* — components
  split at doors, each carrying its door-distance from the entry —
  rather than its exact cell order. Region ids won't match a real run
  even though everything built on them behaves the same.
- **Stairs placement.** `FUN_1325_10a6` scans regions deepest-first for
  a dead-end pocket, which we do; where it finds none we fall back to
  the furthest cell by BFS rather than leaving the level without an
  exit.
- **Three loops in the original never terminate on failure** (the
  monster scatter, the room scatter, the door punch). We cap the
  attempts.
- **Two robustness fixes with no MAZE counterpart.** The arrival cell is
  claimed as soon as it is chosen, so a later door or teleporter cannot
  be built on top of the player; and region labelling falls back to any
  floor cell if the entry has been overwritten. Without these, a level
  can end up with zero regions — and therefore no keys, no stairs and
  no monsters. We hit exactly that on 2 of 25 levels before fixing it.
- **Wall textures.** `FUN_1325_4017` runs at write time off the global
  stream, not the per-level one, so our texture bytes differ from a
  real run even where the geometry matches. Nothing but the renderer
  reads them.

## What we still don't know

1. **FEA body-record field layouts (22 switch cases).** The jump
   table at r2 `0x3feb` has 22 near-pointer entries dispatching on the
   feature-type byte. Each case builds an 8-byte output from the
   9-byte source. Reading each case identifies which source-byte
   maps to which output-byte for each type. Reader side (`get_feature_record`)
   only needs the fixed 8-byte size; thirdeye interprets fields by
   type after reading.
2. **items.dat source-byte semantics.** Wire format is nailed
   (5-byte source, permute to 8-byte output). Naming each of the 5
   source bytes as `key_kind`/`gem_kind`/`spec_kind`/`bonus`/etc.
   needs cross-referencing with the DH SOP's `items` object externs
   (`W:itmflags`, `B:bonus`, `B:key_kind`, `B:spec_kind`,
   `B:gem_kind`, `B:hint_num`) by reading who fills the source array
   in MAZE.
3. **`dh/LEVELS.DAT` per-entry payload post-header.** Header decoded
   (12 bytes: width, height, 4 params); post-header u16 stream is
   dense in the `0x60`/`0x62` sentinel range but full grid decode
   isn't done. Phase-one preview only — not blocking runtime.
4. **`explode_save` algorithm.** The DH save (de)compression. Not in
   MAZE (it's a DH runtime CALL inside `AESOP.EXE`/`HACK.RES`) but
   blocks savegame support.

## Screen layout + page compositing (2026-08-06)

Two engine-level differences from EOB3 had to be fixed before any dungeon
pixels could appear. Both are DH-only and gated so EOB3 is untouched
(verified pixel-identical against a baseline frame).

### 1. DH composites through offscreen pages

EOB3 draws everything straight to one flattened surface. DH instead draws
each panel into its **own offscreen page** at page-local `(0,0)` and then
`copy_window`s it to the screen rect where it belongs:

```text
draw_bitmap(12, 60, …) @ 0,0      Inventory display -> page 12
draw_bitmap(14, 61, …) @ 0,0      Character display -> page 14
draw_bitmap(16, 159,…) @ 0,0      Floor-6           -> page 16
copy_window(12, 1)                page 12 -> screen
copy_window(14, 89)               page 14 -> (139,136)
copy_window(16, 9)                page 16 -> (138,13)   <- the dungeon view
```

The two window calls mean different things, which is the crux:

| Call | Meaning | Example |
|---|---|---|
| `assign_window(owner, x0,y0,x1,y1)` | **offscreen page**, page-local space | `(0,0,175,119)` = a 176×120 buffer |
| `assign_subwindow(owner, parent, x0,y0,x1,y1)` | **screen rect** | `(138,13,313,132)` = where it lands |

Both were funnelling into the same `EventSystem::assignWindow`, so every
DH panel drew at screen `(0,0)` and stomped the previous one — the inventory,
character sheet and floor art all piled up in the top-left corner while the
dungeon view stayed empty. `Win::offscreen` now records which is which;
`draw_bitmap` redirects into the page's own surface (Graphics swaps its
`mScreen` pointer, so every existing draw routine follows with no changes),
and `copy_window` blits the page to the destination's registered origin.

**Screen layout that falls out of the rects:**

```text
(0,0)-(135,181)    inventory / paper-doll column   (left)
(138,13)-(313,132) 3D dungeon view                 (right, under the arch)
(139,136)-(…)      character portrait
(40,182)-(319,199) message bar
(0,182)-(35,199)   CAMP button
```

### 2. DH carves the palette up differently

EOB3's regions (`arun/src/SHARED.H`):

```text
PAL_FIXED 0 = 00-AF     PAL_M1 2 = C0-DF
PAL_WALLS 1 = B0-BF     PAL_M2 3 = E0-FF
```

DH's kernel loads only three regions, and not at those bases
(`HACK.RES/kernel`, the level-enter handler):

```text
set_palette(0, <fixed>)                          225 colours
set_palette(1, table978[wallpal[party_lvl]])     16 colours   <- WALL
set_palette(2, table978[floorpal[party_lvl]])    16 colours   <- FLOOR
```

Probing the art's actual index usage pins the bases exactly:

| Region | Art | Indices used | Base | Covers |
|---|---|---|---|---|
| 0 fixed | — | — | `0x00` | 0–224 |
| 2 floor | `Floor-6` (159) | 196–238 | `0xE0` | 224–239 |
| 1 wall | wallsets 190–196 | …246–253 | `0xF0` | 240–255 |

With EOB3's bases (`B0`/`C0`/`E0`) nothing ever loaded DAC entries 225–255,
so the wallset's 246–253 all resolved to `(0,0,0)` — **the dungeon view
rendered solid black even though the shape decoder was working perfectly**
(sub 8 decodes to 12,384 bytes / 12,102 non-zero pixels). With the corrected
bases the same indices resolve to a proper 8-step wall gradient.

Table lives in `kFirstColorDH` in [runtime/graphics.cpp](../apps/thirdeye/runtime/graphics.cpp),
selected by `gDungeonHack`. `THIRDEYE_PALBASE=b0,b1,b2,b3` overrides the
bases at runtime — that env var is how the map above was worked out, and is
worth keeping for the next game brought up on this runtime.

## `draw_walls` — the 3D wall renderer

The one remaining gate between phase-two's working HUD and playable DH.
Call signature (from `HACK.RES/dungeon.dasm` "draw walls" handler):

```text
draw_walls(
  party_x,         // B:staticVar9515 -- byte
  party_y,         // B:staticVar9516 -- byte
  party_facing,    // B:staticVar9517 -- byte
  view_mode,       // W:view          -- word (view state / mode flag)
  wallset_id,      // L:wallset       -- long, resource number
  &lvlmap[1024],   // 32x32 grid, 1 byte per cell
  &floor_at        // per-cell floor-decoration state
)
```

Wallset resources are 190..196 (Marble / Wood / Stone / Foil / Ice /
Mine / Rock). Enumerating resource 196 (Rock Wallset, 88,419 bytes)
via our `GRAPHICS::Bitmap` decoder yields **236 sub-bitmaps** with a
very clean structure:

| Panel size (WxH) | Occurrences | Likely role |
|---|---|---|
| 25 × 120 | 26 | Near side wall |
| 25 × 95  | 26 | Medium side wall |
| 17 × 59  | 26 | Far side wall |
| 9 × 35   | 26 | Very far side wall |
| 25 × 35  | 26 | Short side (niche?) |
| 17 × 43  | 26 | Alternate far side |
| 129 × 96 | 26 | Near front wall |
| 81 × 59  | 26 | Medium front wall |
| 49 × 37  | 26 | Far front wall |
| 177 × 120 |  2 | Full-view special (opening?) |

= 9 panel roles × 26 style variants + 2 specials. Interpretation: DH
has **26 different wall styles** (mossy, cracked, wood, brick, etc.),
each with the same set of 9 panel positions covering side + front
walls at 3–4 depth tiers.

Debug hook (in `apps/thirdeye/runtime/dh.cpp`):
`THIRDEYE_DHWALL_DUMP=<sub_num>` prints the shape table on first
`draw_walls` and blits sub `<sub_num>` at the view origin so you can
identify panels by eye; `THIRDEYE_DHWALL_AT=x,y` moves that blit.

### View-space architecture (the three calls work as a set)

`init_viewspace` and `build_clipping` are **not** incidental setup — they
populate the per-cell tables `draw_walls` consumes. All three are driven from
`dungeon`'s "init viewspace" handler, and the `dungeon` object's statics show
the shape of it: **18 view cells**, one entry per parallel array.

```text
B:view_X    [18]   map X of each visible cell
B:view_Y    [18]   map Y of each visible cell
B:visible   [18]   per-cell visibility flag
W:l_clip    [18]   per-cell left clip edge
W:r_clip    [18]   per-cell right clip edge
B:floor_at  [18]   per-cell floor state
B:notblocks [18]   per-cell blocking flag
```

Signatures, read off the call sites:

```text
init_viewspace(px, py, facing, &view_X[18], &view_Y[18])

build_clipping(&l_clip[18], &r_clip[18], &visible[18], &view_X[18],
               &view_Y[18], &notblocks[18], &lvlvis[1024], &floor_at[18])

draw_walls(px, py, facing, view_window, wallset_id,
           &lvlmap[1024], &floor_at[18])
```

`draw_walls` arg[3] is the view's **window handle** (the SOP's `W:view`) —
the same handle `copy_window(W:view, W:hold)` double-buffers through — so the
destination rect comes from the window table, not a constant. Between
`init_viewspace` and `build_clipping` the SOP itself loops `i = 0..17`,
reading `lvlmap[view_Y[i]*32 + view_X[i]]` and filling `notblocks`/`floor_at`.

The panel sizes corroborate a classic EOB depth progression — front-wall
widths **177 → 129 → 81 → 49** (full view, then narrowing per depth) with
side walls **25×120 → 25×95 → 17×59 → 9×35**. The 2 177×120 specials are the
adjacent-front-wall case.

**What's still missing is the cell→screen mapping**: which of the 18 slots
corresponds to which (depth, lateral offset), and the blit x/y for each. That
is baked into AESOP.EXE's implementations of runtime functions 262
(`init_viewspace`) and 274 (`draw_walls`) — the natural next RE target, same
Ghidra headless recipe as `../../dh_research/MAZE/`.

**Status: the decoder and palette are both correct now** — a single panel
blits into the view as real wall art (`THIRDEYE_DHWALL_DUMP=8` shows the
129×96 near-front panel filling the view). What remains is the per-cell
walk: read `lvlmap` for the party's forward cone, pick the panel for each
(cell, depth, side), and blit at the matching screen offset. Standard
EOB-style view assembly; no unknown formats left in the way.

One ordering detail matters: DH calls `draw_walls` **after** the SOP has
already `copy_window`'d the floor page into the view rect, so walls paint
on top, straight to the screen at the view origin (138,13) — writing into
the floor page instead is too late to be composited.

## Path to phase-two working

Current state: **phase-two boots and runs its tick loop.** The DH
runtime layer implements all file I/O and dungeon-load helpers per
the specs above (`apps/thirdeye/runtime/dh.cpp`). A native mini-MAZE
seeds structurally-valid empty `savegame/LEVELS.DAT`, `FEA*.DAT` and
`ITEMS.DAT` at first HACK.RES boot (idempotent — real MAZE output is
preserved), so nothing crashes on missing files. Page compositing and the
DH palette map are both in (see the section above), so the HUD and
inventory render correctly and wall art resolves to real colours. What's
left is the per-cell panel walk inside `draw_walls`.

Two paths for producing *actual* dungeon content (not zero-filled):

1. **Bootstrap via a real MAZE run.** DOSBox (or `qemu-i386`), run
   MAZE.EXE inside `dh/savegame/`, capture its output, drop into the
   savegame dir. Our loaders read it verbatim. Fastest path to a
   playable dungeon.
2. **Reimplement MAZE natively.** Format is fully specified above.
   The remaining unknown is the *algorithm* that decides which cells
   get `0xDB` sentinels vs `0xFF`, which feature records land where,
   etc. Blocks on: FEA per-case field mapping, items.dat source
   semantics, and the dungeon-generation pass in MAZE that populates
   the writer buffers.
