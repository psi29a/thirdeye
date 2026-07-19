# Game data + research materials

What the EOB3 install at `../data/` contains, how to point Thirdeye at it, and where to find
the original sources/docs Westwood released. **Never copy or commit any of this** — it's the
user's own game install.

## `../data/` — the EOB3 install

Run the VM against it:
`build/thirdeye.app/Contents/MacOS/thirdeye ../data/EYE.RES --skip-intro --vm --skip-menu`.

### The game itself
- **`EYE.RES`** (6.8 MB) — the AESOP resource container that *is* the game. 2449 entries
  across 20 directory blocks; created Apr–Jul 1993. Special tables: 0 (2444 resource names),
  1/2 (596/597 source-file names), **3 = 135 low-level/runtime functions** (the engine API
  catalog), **4 = 407 message names** (the `SEND` vocabulary). Contains **376 SOP code
  objects** — e.g. `main menu`, weapons/armor (`axe`, `long sword`, `spellbook`, `holy
  symbol`), plus monsters/dungeon/party objects — alongside bitmaps, palettes, fonts, sounds,
  maps, strings.
- **Boot object `start`** (`EYE.BAT` = `aesop eye start`): exports `M:0`@47, `M:1`@687,
  `M:250`@14; imports 17 runtime functions — the first-to-implement-for-boot set:
  `create_program`, `destroy_object`, `init_graphics`, `init_sound`, `init_interface`,
  `set_palette`, `light_fade`, `wipe_window`, `hide_mouse`, `flush_input_events`, `launch`,
  `create_initial_binary_files`, `peekmem`, `pokemem`, `shutdown_graphics`/`_sound`/
  `_interface`.

### Cinematics (GFF format, like `INTRO.GFF` the engine already plays)
- `INTRO.GFF` (1.28 MB), `FINALE.GFF` (1.13 MB), `LICH.GFF` (713 KB), `DARK.GFF` (609 KB).

### Original engine binaries (DOS — reference only)
- `AESOP.EXE` (52 KB) — stub launcher (`AESOP.C`): spawns `DOS4GW.EXE` + `INTERP.EXE`.
- `INTERP.EXE` (160 KB) — AESOP/32 bytecode interpreter (the `RT.ASM`/`INTERP.C` host).
- `DOS4GW.EXE` (265 KB) — DOS extender. `CINE.EXE` (106 KB) — cinematic player.
- `CHARCOPY.EXE`/`.DAT`, `SOUND.EXE` — utilities.

### Sound (Miles AIL) drivers / config
- `A32ADLIB.DLL`, `A32MT32.DLL`, `A32SBDG.DLL`, `A32SPKR.DLL` — AESOP/32 sound DLLs.
- `ADLIB.ADV`, `MT32MPU.ADV`, `PCSPKR.ADV`, `SBDIG.ADV`, `STDPATCH.AD` — AIL `.ADV` drivers.
- `SOUND.CFG` (selects `adlib.adv` + `sbdig.adv`), `SBLASTER.COM`, `XVA.DRV`.
- `MCGA.DLL` — MCGA graphics driver.

### `CHARGEN/`
The character-creation sub-program that `start` spawns via the `launch` runtime function (so
EOB3's `launch` really does run a child program). Contents: `CHGEN.EXE` + assets
`CHARGEN.CPS`/`CHARGENB.CPS`, `CHARPICS.BMP`, `FONT6.FNT`/`FONT8.FNT`, `ITEM.DAT`/
`ITEMTYPE.DAT`, `ITEMICN.CPS`, `PALETTE.COL`, `MGA.OVL`/`XGA.OVL`, `CREATE.SAV`,
`EOSPREFS.DAT`. The `CREATE.SAV` format we use for char-gen party transfer is documented in
[create_sav_and_item_format.md](create_sav_and_item_format.md).

### `SAVEGAME/`
46 files: `ITEMS_NN.BIN`, `LVLxx_NN.BIN` (per-level state), `*.TMP`, `SAVEGAME.DIR`. The
binary save format that `EYE.C` reads/writes; documented in
[eob3_savegame_format.md](eob3_savegame_format.md).

### Data-driven bring-up — EOB3 boot sequence
What `start.MSG_CREATE` does:
`peekmem(1264)` + CASE (hardware/mode check) → `create_initial_binary_files()` →
`init_sound(1)` → `launch("cine.exe", …)` (plays the cinematic) → `init_sound(0)` →
`init_graphics()` → `init_interface()` → `create_program(0, 1382)`.

## Research materials — `../eob3_research/`

The original sources & docs John Miles released as public domain (engine/tools/docs only;
**not** the games). Authoritative reference for the runtime API + opcodes.

- **`runtime/`** — original AESOP/32 runtime C/ASM source:
  - `INTERP.C` — host interpreter entry/bootstrap.
  - `RT.ASM` (2294 lines) — bytecode dispatcher.
  - `RTOBJECT.C` — objects/messages.
  - `RTRES.C` — resource manager (RTR).
  - `RTLINK.C` — DLL linking.
  - `RTSYSTEM.C` — system.
  - `RTCODE.C` — generic runtime fns.
  - `EYE.C` — EOB3-specific engine functions.
  - `GRAPHICS.C`, `MOUSE.C`, `EVENT.C`, `SOUND32.C`, `GIL2VFX.*` — subsystems.
  - `AESOP.EXE`, `INTERP.EXE` binaries.
- **`aesop32/DEV/`** — original ARC compiler source (`RSCOMP`, `SOPCOMP`, `MAPCOMP`,
  `PALCOMP`, `LEXAN`, `PREPROC`, `RESFILE`) — basis of our `arc`. `AMAZE.SOP` sample.
- **`aesop32/DOC/`** — **`AESOP.doc`** (729 KB) and **`MANUAL.DOC`** (425 KB): the engine
  manuals. Primary documentation source for the language, opcodes, and runtime API.
- **`DAESOP_0_85/`** — Mirek Luza's disassembler/converter source — basis of our `daesop`.
- **`sound/`** — MOD/digital sound source (`MOD32`, `MODSND32`, `SOUND32`).
- **`arun/`** — `arun.exe` (AESOP runner) + build artifacts.
- **`ADDITIONAL_DH_RUNTIME_FUNCTIONS.TXT`** / `...5.ZIP` — Dungeon Hack-only runtime functions
  (params + guessed behaviour).
- **`Docs/*.html`** — saved forum threads (VOGONS, EAB, rpgcodex, GOG) + ReWiki research on
  the AESOP/16, `.RES`, and `EYE.RES` formats.
- **`gamebanshee/`** — mirrored GameBanshee walkthrough (per-level 32x32 map images +
  extracted `legends/*.json` annotations/exits) plus plain-text GameFAQs walkthroughs.
  Its README holds the **`LVLnn` ↔ area-class ↔ location-name table** — note the game
  *starts* on LVL03 (Burial Glen); LVL01/02 are the optional Warriors' Tomb (mausoleum),
  which walkthroughs confusingly call "Level 1/2" (tomb floors, not internal numbers).

## daesop cheatsheet (for inspecting game data)

```
daesop -ir EYE.RES out.txt              # resource listing
daesop -j  <res> <name> out.txt          # disassemble a code resource by name
daesop -k  <res> <num>  out.txt          # disassemble a code resource by number
daesop -x  <res>                         # extract all resources
daesop -xh <res>                         # extract with header
daesop -eob3conv EYE.RES EYE2.RES        # EOB3→AESOP/32 (bitmaps+fonts+menu patch)
daesop -create_tbl                       # generate Dungeon Hack `.TBL` files
```

Note: **AESOP resource names are case-sensitive** (`"holy symbol"` ≠ `"Holy symbol"`).
