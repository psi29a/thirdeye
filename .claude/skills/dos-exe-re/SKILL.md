---
name: dos-exe-re
description: Reverse-engineering the DOS executables that ship beside EYE.RES (CHGEN.EXE, CHARCOPY.EXE, AESOP.EXE, INTERP.EXE, CINE.EXE, SOUND.EXE). These are 16-bit MS-DOS MZ (or LZEXE-packed) binaries — the AESOP runtime + char-gen + cinema players — and decoding them is the ground truth for things SOP bytecode can't tell us (e.g. CREATE.SAV layout, char-copy semantics). Use whenever you need to crack open a DOS .EXE: triggers include "CHGEN.EXE", "CHARCOPY.EXE", "AESOP.EXE", "INTERP.EXE", "CINE.EXE", "disassemble exe", "DOS binary", "LZEXE", "MZ", "Ghidra", "radare2", "r2", "what does CHGEN write to CREATE.SAV", "where is the chargen logic", "how does CHARCOPY work".
---

# DOS .EXE reverse engineering

This skill exists because the side-by-side `.EXE` files in the EOB3 install
hold the *write* path for the data the SOP only reads. The SOP can tell us
"open `CHARGEN\CREATE.SAV` and read attr N for player P" but not "where in
CHGEN.EXE the file layout is set up." For that we need to read these binaries.

## Files of interest (next to EYE.RES)

| File | Format | Size | What it is |
|---|---|---|---|
| `AESOP.EXE` | MZ 16-bit | 52 KB | the AESOP runtime stub (chains to INTERP.EXE) |
| `INTERP.EXE` | MZ 16-bit | 160 KB | the SOP interpreter itself (our reference for opcode semantics) |
| `CHGEN.EXE` | MZ 16-bit | 152 KB | character generator (writes `CHARGEN/CREATE.SAV`) |
| `CHARCOPY.EXE` | **LZEXE-packed** | 21 KB | copies chars between save slots — decompress first |
| `CINE.EXE` | MZ 16-bit | 106 KB | cinematic player (intro etc.) |
| `SOUND.EXE` | MZ 16-bit | 34 KB | sound driver shim |
| `DOS4GW.EXE` | 32-bit DOS extender | 265 KB | Watcom's protected-mode runtime — used by AESOP/32 paths |

`file <name>` tells you which class. The LZEXE wrapper hides the real entry
point until you unpack it (see "LZEXE" below).

## Tools

| Tool | Strengths | Use when |
|---|---|---|
| **Ghidra** (`/opt/homebrew/bin/ghidraRun`) | Decompiles to C-ish pseudocode; understands MZ, LE, NE, PE; multi-day project-style RE | Anything more than a 50-line probe; ground truth for "what does this fn do" |
| **radare2** (`/opt/homebrew/bin/r2`) | Fast CLI; scriptable; `aaaa` analysis; `pdf` print fn | Quick "what's at this address" / one-off scripted dumps |
| **objdump** | Standard | Marginal for DOS MZ — limited 16-bit support |
| **strings** | Trivial | First-pass orient: file paths, error msgs, format strings |

Start with `strings -a -n 4 file.exe | less` to see immediately what file
paths / messages the binary touches. That's often enough to confirm "yes,
this is the thing that writes CREATE.SAV" before you spin up Ghidra.

## LZEXE — decompress before disassembling

`CHARCOPY.EXE` (and possibly others) ship LZEXE'd. The MZ header is real but
the code is packed.

✅ **In-tree unpacker:** [`tools/unlzexe.py`](../../../tools/unlzexe.py).
Pure Python 3, stdlib-only, ~110 lines. Port of Bontchev's `unlzexe` 0.8 C
source. Handles LZEXE v0.91 (the version EOB3's `.EXE`s ship with).

```sh
python3 tools/unlzexe.py ../data/CHARCOPY.EXE /tmp/CHARCOPY.unp.exe
```

The output is a plain MZ that Ghidra/r2 can load directly. `file` reports
LZEXE'd inputs as `MS-DOS executable, LZEXE v0.91 compressed` so you know
when to run it.

> If you hit a v0.90-packed binary, the unpacker will assert; v0.90's reloc
> table format and signature differ. Port `reloc90` from Bontchev's source
> if needed.

## 16-bit MZ in Ghidra

- File → Import → choose the `.EXE`. Ghidra autodetects MZ.
- Language: **x86:LE:16:Real Mode** (NOT the default 32-bit).
- Analyzer options: enable "DOS interrupt resolution" if available.
- Entry symbol is `entry`. Many DOS binaries jump immediately to a relocator
  stub — single-step past it to find the real `main`.
- 16-bit segments make the address space weird. Use Ghidra's segment view
  (Window → Symbol Tree → Imports) to navigate.

## 16-bit MZ in radare2

```sh
r2 -b 16 -a x86 -m 0x100 file.exe   # COM-style base 0x100; for .EXE use:
r2 file.exe                          # autodetects MZ, sets up segments
> aaaa                               # analyze all (slow on big files; aaa is faster)
> afl                                # list functions
> s sym.main; pdf                    # seek to main, print disassembled fn
> /c int 21                          # cross-ref DOS API calls (file I/O)
> ii                                 # interrupt vector table
```

For finding **file I/O** (which CREATE.SAV writes go through), grep DOS
interrupts: `int 21h` AH=3Ch (create), 3Dh (open), 3Fh (read), 40h (write),
3Eh (close). `/c int 21` finds the call sites; backtrack from there.

## Workflow for CHGEN → CREATE.SAV (worked example)

1. `strings CHGEN.EXE | grep -i create.sav` → confirm the path string.
2. Open in Ghidra, search for the string, find its xrefs.
3. The xref's function is the one that constructs the file. The bytes it
   `int 21h / AH=40h` writes are what we read in
   [savegame/transfer.cpp](../../../apps/thirdeye/savegame/transfer.cpp).
4. Compare against the RE table in
   [docs/create_sav_and_item_format.md](../../../docs/create_sav_and_item_format.md);
   any field we labelled `?` should resolve to a CHGEN local.

## Already-completed: CHARCOPY.EXE

`../eob3_research/CHARCOPY/` (sibling of the thirdeye repo) is a fully
worked-out RE artifact dump for the EOB1/2 → EOB3 save-staging utility:
unpacked binary, full disasm, string xrefs, `int 21h` site list, and a
README summarising what it does. Useful as a template for the same
treatment on the other DOS binaries. See its `README.md`.

## When the EXE isn't the right tool

The *AESOP runtime functions* (CALL targets — post_event, dispatch_event,
create_program, …) are in `INTERP.EXE` / `AESOP.EXE` but their C source
exists under `../eob3_research/runtime/*.C`. **Read the C first** — it's
infinitely easier than disassembling. See the [aesop-runtime](../aesop-runtime/SKILL.md)
skill. EXE RE is for the things the C source doesn't cover: `CHGEN`,
`CHARCOPY`, `CINE`, the EXE-side savegame writers.

## Common gotchas

- DOS file paths use `\\`; the SOP passes them through `open_transfer_file`
  literally. We already split on `\\` in
  [runtime/eye.cpp](../../../apps/thirdeye/runtime/eye.cpp); the EXE will too.
- `int 21h AH=3Fh` (read) returns CF set on error. Many CHGEN paths branch
  on CF — don't ignore the post-call test.
- The CPS files (`CHARGEN.CPS`, `CHARPICS.BMP`) are graphics CHGEN draws
  with; their layout is in CHGEN's resource handlers, *not* in `EYE.RES`.
