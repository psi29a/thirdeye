#!/usr/bin/env python3
"""Burial Glen phase-3 driver (docs/control_channel.md phase 3).

Throwaway-quality by design -- the *channel* is the deliverable; this script
is the proof it can drive real play. Usage:

    glen_drive.py /tmp/te_ctl.sock goto 27 30     # BFS-walk to a cell
    glen_drive.py /tmp/te_ctl.sock scan            # dump walkability grid
    glen_drive.py /tmp/te_ctl.sock kill-adjacent   # melee loop on front cell
"""
import sys
import time
from collections import deque

sys.path.insert(0, __import__("os").path.dirname(__file__))
from ctl import talk as _talk  # noqa: E402


def talk(sock, cmd, tries=3):
    # ponytail: dumb retry -- a mid-walk engine hiccup shouldn't kill the run
    for _ in range(tries - 1):
        try:
            return _talk(sock, cmd)
        except OSError:
            time.sleep(2)
    return _talk(sock, cmd)

# Facing vectors: fdir 0=N 1=E 2=S 3=W (automap.cpp facingVecs).
DIRS = {0: (0, -1), 1: (1, 0), 2: (0, 1), 3: (-1, 0)}
KEY_FWD, KEY_TL, KEY_TR = "4800", "4700", "4900"
# Walkable plane-0 features: graves (2054) per automap kBlockerBases.
# Hackable trees (2060) count as walkable for PATHING -- goto chops them on
# contact (ALL ATTACK swings clear them; that's the intended EOB3 mechanic).
# Trap classes (automap kTrapClasses) treated as blocked -- don't walk into pits.
WALKABLE_P0 = {2054}
HACKABLE = 2060
# Weapon-hand click points (logical 320x200), harvested from a live frame:
# PC0 sword, PC1 axe, PC3 mace. Kept as fallback; ALL ATTACK is better.
MELEE_CLICKS = [(229, 20), (296, 18), (297, 70)]
# Name-plate click points (toggles yellow "selected" state). With any PC
# selected, the ALL ATTACK button appears under the arrow pad -- one click
# swings every selected PC's weapon (GameBanshee gameplaynotes.html).
NAME_CLICKS = [(216, 6), (286, 6), (216, 57), (286, 57), (216, 110), (286, 110)]
ALL_ATTACK = (146, 165)


def pose(sock):
    for ln in talk(sock, "party").splitlines():
        if ln.startswith("pose"):
            kv = dict(p.split("=") for p in ln.split()[1:])
            return int(kv["x"]), int(kv["y"]), int(kv["fdir"]), int(kv["lvl"])
    raise RuntimeError("no pose (not in game?)")


def monsters(sock):
    out = []
    for ln in talk(sock, "monsters").splitlines():
        if ln.startswith("npc"):
            kv = dict(p.split("=") for p in ln.split()[1:])
            m = {k: int(v) for k, v in kv.items()}
            if m["hp"] > 0 and m["x"] <= 31 and m["y"] <= 31:
                out.append(m)
    return out


def cell_heads(sock, x, y):
    """[(plane, headclass or None), ...] for the 3 planes."""
    out = {}
    for ln in talk(sock, f"cell {x} {y}").splitlines():
        if ln.startswith("plane"):
            plane = int(ln[5])
            if "chain=" in ln:
                first = ln.split("chain=")[1].split(",")[0]
                out[plane] = int(first.split(":")[1])
            else:
                out[plane] = None
    return out


def scan_grid(sock):
    """32x32 walkability: real walls from the lvlmap verb (dungeon
    B:lvlmap@0..1023) + plane-0 blocking features. True = enterable."""
    grid = [[True] * 32 for _ in range(32)]
    rows = [ln for ln in talk(sock, "lvlmap").splitlines()
            if ln and ln[0] in ".#"]
    for y, row in enumerate(rows[:32]):
        for x, ch in enumerate(row[:32]):
            if ch == "#":
                grid[y][x] = False
    for y in range(32):
        for x in range(32):
            if not grid[y][x]:
                continue
            p0 = cell_heads(sock, x, y).get(0)
            if p0 is not None and p0 not in WALKABLE_P0 and p0 != HACKABLE:
                grid[y][x] = False
    return grid


def chop(sock, cx, cy):
    """ALL-ATTACK the hackable tree in the front cell until we can step in."""
    for rnd in range(25):
        talk(sock, f"click L {ALL_ATTACK[0]} {ALL_ATTACK[1]}")
        time.sleep(2.3)
        if step(sock):
            print(f"chopped through ({cx},{cy}) after {rnd + 1} swings")
            return True
    print(f"chop failed at ({cx},{cy})")
    return False


def bfs(grid, start, goal, blocked_extra=frozenset()):
    q = deque([start])
    prev = {start: None}
    while q:
        cur = q.popleft()
        if cur == goal:
            path = []
            while cur:
                path.append(cur)
                cur = prev[cur]
            return path[::-1]
        for dx, dy in DIRS.values():
            nxt = (cur[0] + dx, cur[1] + dy)
            if not (0 <= nxt[0] < 32 and 0 <= nxt[1] < 32):
                continue
            if nxt in prev or nxt in blocked_extra:
                continue
            if not grid[nxt[1]][nxt[0]]:
                continue
            prev[nxt] = cur
            q.append(nxt)
    return None


def face(sock, want):
    x, y, fdir, _ = pose(sock)
    delta = (want - fdir) % 4
    keys = {0: [], 1: [KEY_TR], 2: [KEY_TR, KEY_TR], 3: [KEY_TL]}[delta]
    for k in keys:
        talk(sock, f"key {k}")
        time.sleep(0.8)


def step(sock):
    """One forward step; returns True if the pose moved."""
    x0, y0, _, _ = pose(sock)
    talk(sock, f"key {KEY_FWD}")
    time.sleep(0.9)
    x1, y1, _, _ = pose(sock)
    return (x0, y0) != (x1, y1)


def fight_adjacent(sock):
    """If any live monster is in a cardinally-adjacent cell, face it and
    ALL-ATTACK until the neighbourhood is clear. Returns #kills engaged."""
    engaged = 0
    while True:
        x, y, _, _ = pose(sock)
        adj = [(d, (x + dx, y + dy)) for d, (dx, dy) in DIRS.items()]
        threat = None
        for d, cell in adj:
            if any((m["x"], m["y"]) == cell for m in monsters(sock)):
                threat = d
                break
        if threat is None:
            return engaged
        face(sock, threat)
        kill_adjacent(sock)
        heal_party(sock)  # debug top-up after every engagement
        engaged += 1


def goto(sock, tx, ty):
    select_all(sock)  # arm ALL ATTACK for the fights en route
    grid = None
    for attempt in range(60):
        fight_adjacent(sock)  # never walk with a mist on our flank
        if attempt % 5 == 0:
            heal_party(sock)  # top up chip damage from passing mists
        x, y, fdir, _ = pose(sock)
        if (x, y) == (tx, ty):
            print(f"arrived ({tx},{ty})")
            return True
        if grid is None:
            print("scanning grid...")
            grid = scan_grid(sock)
        mob_cells = {(m["x"], m["y"]) for m in monsters(sock)}
        path = bfs(grid, (x, y), (tx, ty), blocked_extra=mob_cells)
        if not path or len(path) < 2:
            # allow ending NEXT TO a mob-occupied goal
            path = bfs(grid, (x, y), (tx, ty))
            if not path or len(path) < 2:
                print(f"no path from ({x},{y}) to ({tx},{ty})")
                return False
        nx, ny = path[1]
        want = next(d for d, (dx, dy) in DIRS.items()
                    if (x + dx, y + dy) == (nx, ny))
        face(sock, want)
        if not step(sock):
            # Hackable tree in the way? Chop through it (ALL ATTACK swings).
            if cell_heads(sock, nx, ny).get(0) == HACKABLE:
                if chop(sock, nx, ny):
                    continue
            # A cutscene dialog (Florn Falconhand etc.) freezes movement --
            # Enter picks the highlighted choice / dismisses OK. Try that
            # (twice: choice + OK) and retry before concluding it's a wall.
            talk(sock, "key d")
            time.sleep(1.5)
            talk(sock, "key d")
            time.sleep(1.5)
            if step(sock):
                continue
            # Real walls live in the dungeon maze grid, not lvlobj plane 0 --
            # the scan can't see them. Learn: mark the cell blocked, reroute.
            print(f"step blocked at ({x},{y}) fdir={want}; marking ({nx},{ny})")
            grid[ny][nx] = False
    print("goto: gave up after 60 iterations")
    return False


def heal_party(sock):
    """Debug top-up: poke every live PC's W:hpts (raw offset 181 =
    staticBase(1369)+171) back to hmax. Keeps the QSP alive through the
    mist gauntlet on delivery runs; does NOT resurrect the dead."""
    for ln in talk(sock, "party").splitlines():
        if not ln.startswith("pc "):
            continue
        kv = dict(p.split("=") for p in ln.split()[1:] if "=" in p)
        cur, mx = kv["hp"].split("/")
        if int(cur) <= 0:
            continue  # dead stays dead
        obj, mxv = int(kv["obj"]), int(mx)
        talk(sock, f"poke {obj} 181 {mxv & 0xFF:02x} {(mxv >> 8) & 0xFF:02x}")


def select_all(sock):
    """Toggle every PC's name yellow so the ALL ATTACK button exists.
    Clicks TOGGLE, so guard with a per-engine marker file (keyed on the
    engine pid in /tmp/te.pid) to stay idempotent across invocations."""
    import os
    try:
        pid = open("/tmp/te.pid").read().strip()
    except OSError:
        pid = "unknown"
    marker = f"/tmp/te_sel_{pid}"
    if os.path.exists(marker):
        return
    for cx, cy in NAME_CLICKS:
        talk(sock, f"click L {cx} {cy}")
        time.sleep(0.4)
    open(marker, "w").close()


def kill_adjacent(sock, selected=True):
    """ALL-ATTACK whatever stands in the front cell until it's gone."""
    if not selected:
        select_all(sock)
    for rnd in range(40):
        x, y, fdir, _ = pose(sock)
        fx, fy = x + DIRS[fdir][0], y + DIRS[fdir][1]
        targets = [m for m in monsters(sock) if (m["x"], m["y"]) == (fx, fy)]
        if not targets:
            print(f"front cell ({fx},{fy}) clear after {rnd} rounds")
            return True
        talk(sock, f"click L {ALL_ATTACK[0]} {ALL_ATTACK[1]}")
        time.sleep(2.3)
        heal_party(sock)  # flankers hit during the fight; keep everyone up
    print("kill-adjacent: gave up")
    return False


def floor_ids(sock, x, y):
    out = set()
    for ln in talk(sock, "items").splitlines():
        if f"x={x} y={y}" in ln and ln.startswith("item"):
            out.add(int(ln.split()[1].split("=")[1]))
    return out


def floor_count(sock, x, y):
    return len(floor_ids(sock, x, y))


def obj_place(sock, oid):
    for ln in talk(sock, f"obj {oid}").splitlines():
        if ln.startswith("cls="):
            return int(dict(p.split("=") for p in ln.split())["place"])
    return None


# Inventory screen geometry (logical 320x200, harvested live):
# portrait click opens a PC's inventory; backpack = 2x7 grid; page-curl
# bottom-right closes; top-right arrows page between PCs.
PORTRAIT = (200, 30)
# Bottom-up: QSP packs are occupied in the top rows only, and stowing into
# an occupied slot swaps the occupant onto the cursor (which we'd then lose
# track of). Filling from the bottom keeps collisions out of the picture.
BACKPACK_SLOTS = [(cx, cy) for cy in (144, 128, 112, 96, 80, 64, 48)
                  for cx in (210, 192)]
NEXT_PC = (300, 47)
CLOSE_INV = (303, 158)
# Candidate sprite points: OWN-CELL band only (stand ON the loot to pick it
# up) -- ahead-cell pickup is unreliable because a clickable feature behind
# the items (fruit trees at the #3 site) owns the click region and eats
# every click ("The tree is barren of fruit"). Own-cell sprites render in
# the bottom band below the horizon, unobstructed. HARD CAP at y=118: the
# movement arrow pad starts at ~(117,123) and clicking it walks the party
# around mid-loot (that mistake killed half the QSP party once).
SPRITE_GRID = [(x, y) for y in range(102, 119, 4) for x in range(12, 169, 7)]


_TAKEN_IDS = []  # module-level: every item id this process has ever stowed


def _stow_on_cursor(sock, pid, slot_cursor, pc_cursor):
    """Click backpack slots until `pid` (already on the cursor) lands with
    place>=0. QSP PCs start with occupied slots -- clicking one SWAPS its
    contents onto the cursor instead of failing, silently bumping whatever
    we'd already stowed there. Guard against that: after each click, check
    every previously-taken item is still place>=0; if one regressed, the
    click just swapped it out -- click the SAME slot again to swap it back
    in, then permanently skip that slot and keep looking for `pid`."""
    talk(sock, f"click L {PORTRAIT[0]} {PORTRAIT[1]}")
    time.sleep(0.8)
    ok = False
    guard = 0
    while slot_cursor[0] < 10_000 and guard < len(BACKPACK_SLOTS) * 8:
        guard += 1
        if slot_cursor[0] % len(BACKPACK_SLOTS) == 0 and slot_cursor[0] > 0:
            talk(sock, f"click L {NEXT_PC[0]} {NEXT_PC[1]}")
            time.sleep(0.7)
            pc_cursor[0] += 1
        sx, sy = BACKPACK_SLOTS[slot_cursor[0] % len(BACKPACK_SLOTS)]
        talk(sock, f"click L {sx} {sy}")
        time.sleep(0.7)
        regressed = [t for t in _TAKEN_IDS
                     if t != pid and (obj_place(sock, t) or -1) < 0]
        if regressed:
            talk(sock, f"click L {sx} {sy}")  # undo the swap
            time.sleep(0.7)
            slot_cursor[0] += 1  # this slot is occupied; never revisit it
            continue
        p = obj_place(sock, pid)
        slot_cursor[0] += 1
        if p is not None and p >= 0:
            ok = True
            break
    talk(sock, f"click L {PORTRAIT[0]} {PORTRAIT[1]}")
    time.sleep(0.8)
    return ok


def loot_cell(sock, ix, iy, slot_cursor=[0], pc_cursor=[0]):
    """Pick every floor item at (ix,iy) into party backpacks. Party must be
    adjacent facing the cell (or standing on it). The inventory panel stays
    CLOSED except during each stow -- attacks (and therefore the mist
    vigilance fights) don't land while a UI screen is up, and the page-curl
    "close" turned out to toggle to CHARACTER INFO, not close. The portrait
    click is the open/close toggle. Returns items taken."""
    taken = 0
    floor = floor_ids(sock, ix, iy)
    grid_i = 0
    misses = 0
    while floor and misses < len(SPRITE_GRID):
        # Mist vigilance (panel is closed here, so swings land).
        if grid_i % 6 == 0:
            x, y, fdir, _ = pose(sock)
            if fight_adjacent(sock):
                want = next((d for d, (dx, dy) in DIRS.items()
                             if (x + dx, y + dy) == (ix, iy)), None)
                if want is not None:
                    face(sock, want)
        gx, gy = SPRITE_GRID[grid_i % len(SPRITE_GRID)]
        grid_i += 1
        talk(sock, f"click L {gx} {gy}")
        time.sleep(0.7)
        now = floor_ids(sock, ix, iy)
        picked = floor - now
        if picked:
            pid = picked.pop()
            if _stow_on_cursor(sock, pid, slot_cursor, pc_cursor):
                _TAKEN_IDS.append(pid)
                taken += 1
            floor = now
            misses = 0
            grid_i -= 1  # same spot may hold more items (stacked)
        else:
            misses += 1
    print(f"looted {taken} items from ({ix},{iy}), {len(floor)} left")
    return taken


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sock, cmd = sys.argv[1], sys.argv[2]
    if cmd == "goto":
        ok = goto(sock, int(sys.argv[3]), int(sys.argv[4]))
        sys.exit(0 if ok else 1)
    elif cmd == "scan":
        g = scan_grid(sock)
        px, py, _, _ = pose(sock)
        for y in range(32):
            print("".join("@" if (x, y) == (px, py)
                          else "." if g[y][x] else "#" for x in range(32)))
    elif cmd == "kill-adjacent":
        sys.exit(0 if kill_adjacent(sock) else 1)
    elif cmd == "loot":
        loot_cell(sock, int(sys.argv[3]), int(sys.argv[4]))
    else:
        print(__doc__)
        sys.exit(2)
