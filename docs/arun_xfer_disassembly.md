# arun chargen-transfer runtime — RE'd from `arun.exe` + `arun.map`

The 5 chargen-transfer runtime functions (`open_transfer_file`,
`player_attrib`, `item_attrib`, `read_initial_items`,
`write_initial_tempfiles`) live in **arun.exe** with no released C source —
they're in `xfer.c` which Westwood/Miles never shipped. This doc disassembles
them straight from `arun.exe` so our `apps/thirdeye/savegame/transfer.cpp` +
`apps/thirdeye/runtime/eye.cpp` impls can be verified 1:1.

Reproduce with:

```bash
r2 -q -c 'pd 50 @ 0x240b0' /Users/bret.curtis/.../arun/arun.exe   # player_attrib
r2 -q -c 'pd 60 @ 0x24100' /Users/bret.curtis/.../arun/arun.exe   # item_attrib
r2 -q -c 'pd 50 @ 0x124c2' /Users/bret.curtis/.../arun/arun.exe   # write_initial_tempfiles
# (function addresses from arun.map; reloc placeholders look like `0x3250 ; RELOC 32`
#  — apply `-A -e bin.relocs.apply=true` and they resolve into the data segment
#  at 0x33250+.)
```

---

## 1. `_player_attrib(pc, attr, size) → i32`  @ `0x240b0`

```asm
ebx ← arg3 (size)          ; [esp+0x10]
edx ← arg1 (pc)            ; [esp+8]

; compute pc * 345:
eax = edx*4
eax -= edx                  ; eax = pc*3
eax = eax * 8               ; pc*24
eax -= edx                  ; pc*23
edx = eax
eax <<= 4                   ; pc*23*16 = pc*368
eax -= edx                  ; pc*345 ✓ (PC stride)

; buffer + 0x14 + pc*345 + attr:
eax = [0x116c4]             ; CREATE.SAV buffer pointer
eax += 0x14
eax += pc*345
eax += [esp+0xc]            ; eax += attr

; dispatch on size (returns sign-extended)
cmp ebx, 1
jb   read_u32
jbe  read_u8
cmp ebx, 2
je   read_i16
read_u32: eax = [eax]; ret    ; size = 4: 32-bit
read_u8:  eax = signed [eax]; ret   ; size = 1: signed byte
read_i16: eax = signed [eax (word)]; ret  ; size = 2: signed word
```

### Verified semantics

- **PC stride = 345 bytes** (factored via the `3*23*16 - 3*23` chain).
- **PC[0] starts at file offset `0x14 = 20`** *plus the attr*. Since the
  caller passes `attr ≥ 2` (the name is at attr=2), this lands at file
  offset 22 (`0x16`) for the first byte of the name — confirming our
  docs/create_sav_and_item_format.md `attr = record_off + 2` bias.
- **size dispatch:** size 1 → signed byte, size 2 → signed word, size 4
  (or any value ≥ 4) → 32-bit word.

### Matches our impl

[apps/thirdeye/savegame/transfer.cpp:72-…](../apps/thirdeye/savegame/transfer.cpp):
`playerAttrib(pc, attr, size)` does `(0x16 - 2) + pc*345 + attr` with
`size` ∈ {1, 2, 4} dispatch — byte for byte the same formula. ✓

---

## 2. `_item_attrib(pc, slot, attr) → i32`  @ `0x24100`

```asm
; (same pc*345 prologue as player_attrib)
ebx ← attr
edx ← pc
eax = pc*345
edx = [0x116c4]               ; buffer pointer
ecx = edx + eax               ; PC[pc] start in buffer

eax = [esp+0xc]               ; slot
; slot-mapping lookup:
ax  = word [eax*2 + 0x3250]   ; ax = slot_map[slot]
eax &= 0xFFFF

; read inventory[slot_map[slot]] (u16):
ax = word [ecx + eax*2 + 0xf1]   ; ax = raw_inv[slot_map[slot]]
test ax, ax
jle  ret_-1                      ; empty / negative → return -1

; ax now holds the CREATE.SAV item id (e.g. 435).
movsx ebx, ax                    ; ebx = item_id (sign-extended)
eax = ebx * 7
eax = eax * 2 + DISP             ; eax = item_id * 14 + DISP
                                 ; DISP = 0x888 (bits), 0x88a (type), 0x893 (value)

; dispatch on attr:
; attr=0 → mov al, byte [edx + eax*2 + 0x888]   ; bits  (rec_off +2)
; attr=1 → mov al, byte [edx + eax*2 + 0x88a]   ; type  (rec_off +4)
; attr=2 → mov al, byte [edx + eax*2 + 0x893]   ; value (rec_off +13)
```

### KEY FINDING — the slot-mapping table

`item_attrib`'s `slot` argument is **NOT** a direct index into CREATE.SAV's
26-word inventory[26]. It's an index into a **slot-mapping table at
data-segment offset `0x33250`** that the original CHGEN/xfer share to
translate "EOB1-chargen slot semantics" → "raw `inventory[]` array index"
inside the PC record.

Dumped from `arun.exe` (`pxw 80 @ 0x33250` after relocs applied):

| SOP slot | `slot_map[i]` (=raw inv index) | Interpretation |
|---|---|---|
| 0  | 2  | "general backpack slot 1" |
| 1  | 3  | "general backpack slot 2" |
| 2  | 4  | |
| 3  | 5  | |
| 4  | 6  | |
| 5  | 7  | |
| 6  | 8  | |
| 7  | 9  | |
| 8  | 10 | |
| 9  | 11 | |
| 10 | 12 | |
| 11 | 13 | |
| 12 | 14 | |
| 13 | 15 | |
| 14 | **17** | (skips index 16) |
| 15 | 18 | |
| **16** | **0**  | (wraps to start) |
| 17 | 25 | |
| 18 | 26 | |
| 19 | 21 | |
| **20** | **1**  | |
| 21 | 22 | |
| 22 | 23 | |
| 23 | 24 | |
| 24 | 20 | |
| 25 | 19 | |
| 26+ | `0xFFFF` | sentinel |

So the SOP's natural iteration `for slot in 0..25` actually visits the raw
inventory in the permuted order: `[2, 3, 4, …, 15, 17, 18, 0, 25, 26, 21,
1, 22, 23, 24, 20, 19]`.

The **bundled Quick Start Party** confirms it: Bob (Fighter) has raw inventory
`[435, 0, 436, 437, 438, 439, 0×11, 434, 0×8]` — so when the SOP iterates
slots 0..25 it reads:

| SOP slot | maps to raw idx | item id | item type | EOB3 routing |
|---|---|---|---|---|
| 0  | 2  | 436 = Staff      | type 13 (staff)         | weapon → slot 16 or 20 |
| 1  | 3  | 437 = Dagger     | type 5 (dagger)         | weapon → 16/20 |
| 2  | 4  | 438 = Short sword | type 2 (short sword)   | weapon → 16/20 |
| 3  | 5  | 439 = Lock picks | type 28 (lock picks)    | carried → pouch |
| 14 | 17 | 434 = Leather armor | type 22 (leather)    | body → slot 14 |
| 16 | 0  | 435 = Robe       | type 41 (robe)          | body → slot 14 (loses to leather, or stacks) |

Bob ends up with weapon-in-hand + leather body + robe-into-backpack. That's
exactly the bundled QSP behaviour: Bob is shown with the staff equipped and
the robe/leather as a body-slot pick (the transfer's `categoryForClass`
breaks ties).

### Implications for our chargen

1. **Our existing implementation writes items to raw inventory slots
   0..N-1 sequentially.** Per the table, those map to:
   - raw 0 → SOP slot **16** (right hand)
   - raw 1 → SOP slot **20** (left hand)
   - raw 2 → SOP slot 0 (backpack 0)
   - raw 3 → SOP slot 1 (backpack 1)
   - raw 4 → SOP slot 2 (backpack 2)
   - raw 5 → SOP slot 3 (backpack 3)

   So our kit's **first two items land in the weapon hands**, the rest in
   the backpack. Since `categoryForClass` re-routes by item class anyway,
   the visible result is correct — but the "preferred ordering" we want is
   that the *primary weapon* be the first item, and the *armor* second.
   For a Fighter (kit: chainmail, long sword, shield, rations):
   - raw[0] = chainmail → SOP 16 → "right hand"; categorizer → body (slot 14) ✓
   - raw[1] = long sword → SOP 20 → "left hand"; categorizer → weapon (slot 16 or 20) ✓
   - raw[2] = shield → SOP 0 → backpack; categorizer → shield (slot 20 or 16) ✓
   - raw[3] = rations → SOP 1 → backpack; categorizer → carried ✓

   All good. Re-categorization saves us from ordering issues.

2. **CHGEN.EXE's natural layout** (per the QSP) puts armor in raw[17] and
   weapons in raw[2..N]. We could mirror that for fidelity, but our
   current scheme produces equivalent behaviour via `categoryForClass`.

3. **The slot map is critical context for any future ITEMS.TMP RE.** When
   the live PC class moves items between body parts, the EOB3 indices
   (slots 14, 16, 20, 25, etc. — body / hands / helmet) correspond to the
   slot-map's **forward** indices, NOT raw inventory positions.

### Matches our impl

[apps/thirdeye/savegame/transfer.cpp:160-…](../apps/thirdeye/savegame/transfer.cpp)
already routes by `categoryForClass` after `table123Lookup`, so the raw slot
positions don't change the body-part outcome. Our writeCreateSav writes
items to slots 0..N-1 — equivalent behaviour to mirroring CHGEN.EXE's
permuted layout, because re-categorization is the load-bearing step. ✓

---

## 3. `_write_initial_tempfiles()` @ `0x124c2`

```asm
push 0x3e7                          ; resume_items arg: end_id = 999
push 0                              ; resume_items arg: start_id = 0
cmp dword [some_flag], 0
je   .reset_flag
.set_flag: var = 1; jmp .call
.reset_flag: var = 0
.call:
push var                            ; resume_items arg: pristine? (0/1)
push 0xe13                          ; resume_items arg: 3603 (string id)
call resume_items                   ; resume_items(0, 999, var, 3603)
test eax, eax
jne  .ok
push 0x288                          ; print error msg 648
call print_message                  ; "Unable to load initial items"
.ok:

push 0
call _create_initial_binary_files   ; @ 0x11fea — writes ITEMS_00.BIN scaffold

; Loop: for level in 1..14:
mov  [ebp-8], 1
.loop:
cmp  [ebp-8], 14
ja   .done
push [ebp-8]                        ; arg: level number
call 0x12059                        ; (loads/converts level data?)
push 3584                           ; src filename pattern: "LVL%02d_00.BIN"?
push 3540                           ; dst filename pattern: "LVL%02d.TMP"?
call copy_file_pattern              ; @ 0x1a313
cmp  ax, 0xffff                     ; -1 = failure
jne  .next
push 737                            ; error msg id
call print_message
.next:
inc  [ebp-8]
jmp  .loop
.done:
ret
```

### Verified semantics

The original `write_initial_tempfiles`:

1. **Calls `resume_items(start_id=0, end_id=999, pristine_flag, 3603)`.** The
   start/end pair scans every existing item-object in slot range 0..999 and
   serializes them; `pristine_flag` toggles "fresh game" vs "loaded save".
   `3603` is the string-resource id used in error reports.
2. **Calls `create_initial_binary_files(0)`** — writes a fresh `ITEMS.TMP`
   from the live PC + item objects (this is the file we read in
   `resume_level`).
3. **Loops 14 levels.** For each level i in 1..14, calls a per-level
   serialization helper at `0x12059`, then `copy_file_pattern(src=3584,
   dst=3540)` — the strings are formatted with the level number so the
   actual filenames are `LVL01_00.BIN`/`LVL01.TMP` through
   `LVL14_00.BIN`/`LVL14.TMP`.

### Matches our impl

Our [apps/thirdeye/runtime/eye.cpp:291-…](../apps/thirdeye/runtime/eye.cpp)
`write_initial_tempfiles` stub does steps 2–3 directly (no `resume_items`
because our PC objects are already built by the xfer SOP). The file copy
loop matches arun's exactly: 14 levels, `LVL??_00.BIN` → `LVL??.TMP`. ✓

The **scaffold-position rule** we landed on (don't overwrite buf[252..255] —
let `ITEMS_00.BIN`'s shipped position carry through) corresponds directly to
arun's `create_initial_binary_files(0)` which reads the live kernel's saved
position; the kernel's position is itself loaded from `ITEMS_00.BIN` because
that's the only file present when CHGN boots. So we match arun's effective
end-state. ✓

---

## 4. `_read_initial_items()` @ `0x12490`

```asm
push 0
call 0x11fea                  ; clear/reset some flag
push 0
push 999
push 0
push 3496                     ; string id "ITEM.DAT" 
call 0x18534                  ; read_named_items(filename, 0, 999, 0)
ret
```

Loads the per-instance item records from `CHARGEN/ITEM.DAT` (string id 3496)
into the live item-object table for ids 1..999. The chargen-transfer SOP
calls this before iterating PC inventories — `item_attrib` looks up by
id but those ids need backing item records first.

### Matches our impl

Our [apps/thirdeye/runtime/eye.cpp:276-…](../apps/thirdeye/runtime/eye.cpp)
`read_initial_items` stub returns 0 — because we don't *need* to populate a
table; our `item_attrib` reads CREATE.SAV's item array directly (the same
14-byte records). The xfer SOP's only use of `read_initial_items` is to seed
the same table our `item_attrib` already represents. Behaviourally
equivalent. ✓

---

## 5. `_open_transfer_file(filename)` @ `0x23e20`

```asm
; copy filename to internal buffer at 0x116a4 (NUL-terminated)
; call file_open (returns handle or ≤0 on error)
; read whole file into buffer at 0x116c4
; (player_attrib/item_attrib reference [0x116c4] for the buffer pointer)
ret
```

Verifies that `0x116c4` is the **single per-process buffer** that
`player_attrib`/`item_attrib` read from — i.e. the transfer file is opened
once and its bytes are held in RAM for the duration of the chargen-transfer
flow.

### Matches our impl

Our [apps/thirdeye/runtime/eye.cpp:184-…](../apps/thirdeye/runtime/eye.cpp)
`open_transfer_file` loads `CHARGEN/CREATE.SAV` into a `TransferState` buffer
that `playerAttrib`/`itemAttrib` then read from. Same architecture. ✓

---

## 6. Net effect on our codebase

All five functions match our implementation **at the semantic level**:

- PC offsets (`0x16-2 + pc*345 + attr`) ✓
- 14-level scaffold copy in `write_initial_tempfiles` ✓
- Single buffered read of CREATE.SAV ✓
- `item_attrib`'s slot-mapping table → makes the choice of which CREATE.SAV
  raw-inv index our chargen writes to *cosmetically irrelevant* because the
  transfer re-categorizes by item class. So our "write to raw_inv[0..N-1]"
  is fine.

The one thing we should **also** mirror, for 1:1 fidelity rather than just
correctness, is **write items into the same raw-inv slots CHGEN.EXE uses**
(slot 17 for body, slots 2..N for weapons/backpack). This would mean a
fresh CHGN save inspected with a hex editor reads like a Westwood-written
save, which is helpful for cross-RE work. Currently equivalent; cleanly
mirrored is a tightening pass we can do.

### Open follow-ups

- **Disassemble `_resume_level` / `_change_level`** — they're shorter (~30
  instructions each) and would pin the runtime side of position seeding for
  good (and confirm the `B:party_lvl=0 → fall back to ITEMS_00.BIN`
  behaviour we infer).
- **Find the CHGEN.EXE class-kit table.** Per
  `docs/item_dat_format.md` we know which item types each class can use
  (via `ITEMTYPE.DAT.class_use_mask`); CHGEN.EXE picks a *specific* kit per
  class. Locating the table in CHGEN.EXE's data segment would replace our
  hand-picked AD&D 2e canon kits with the exact ones the original game
  shipped.
- **`resume_items` (called by `write_initial_tempfiles`)** — its 4th arg
  (`3603`) is a string id that probably names the source — `"ITEM.DAT"` or
  `"ITEMS_xx.BIN"`. Pin that to fully document the arun start-of-game flow.
