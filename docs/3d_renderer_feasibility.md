# Feasibility: 3D dungeon rendering in thirdeye

**TL;DR.** Yes, it's feasible. SDL3 already ships **SDL_GPU**, a shader-based
abstraction whose Vulkan backend is first-class on Linux/Windows (Metal on
macOS, D3D12 on Windows) — so we do **not** need to write our own Vulkan
loader/driver plumbing. The dungeon view is a fixed 176×121 sub-rect of a
320×200 SOP-driven scene; we can replace only that rect with a 3D render and
leave every other pixel (HUD, panels, text, cinematics, menus, inventory)
untouched. Complexity is dominated by content (wall textures, monster
sprites/models) and by matching the grid-tile idioms EOB3 relies on (spinners,
teleports, illusory walls), not by graphics-API work.

## What "3D" means for EOB3

EOB3's dungeon is already 3D in intent — a 32×32 grid, party stands on a
tile facing N/E/S/W, and each frame the SOP renders a **fixed 22-square
viewshed** by asking `drawImage` to blit pre-rendered wall/floor/ceiling
sprites at five depth tiers (near → far). Monsters, items, doors, and
decorations are billboard sprites drawn at scaled sizes into the same tiers.
See [architecture.md](architecture.md) for the SOP draw loop.

A "real 3D" renderer keeps the *simulation* the SOP already runs (party
position, facing, maze cells, monsters, spell effects, doors) and replaces
only the **presentation** of the dungeon-view rect: extrude the 32×32 grid
into a mesh, put the camera on the party tile at eye height, render
first-person perspective with the current facing. Everything the SOP thinks
happens per tile still happens per tile — 90° turns, tile-step movement,
grid-based combat, spinner effects — because the grid is the game.

## Does SDL3 give us Vulkan?

Yes. **SDL_GPU** (`SDL3/SDL_gpu.h`, already in our SDL3 install at
`/opt/homebrew/include/SDL3/SDL_gpu.h`) is a shader-based cross-vendor GPU
API added in SDL 3.x. Backends:

| Platform | Backend | Notes |
|----------|---------|-------|
| Linux | Vulkan | primary |
| Windows | Vulkan / D3D12 | driver name `"vulkan"` selectable at `SDL_CreateGPUDevice` |
| macOS | Metal | Vulkan not native; MoltenVK works but SDL_GPU's Metal path is lighter |
| iOS / Android | Metal / Vulkan | already the plan per [android.md](android.md) / [ios.md](ios.md) |
| Switch | vendor | out of scope here |

Shaders are provided as **SPIR-V** (and SDL cross-compiles at load, or you
ship per-backend blobs via `SDL_shadercross`). Practical meaning: write one
SPIR-V vertex/fragment pair for the dungeon; SDL runs it on Vulkan
natively, translates it to Metal/D3D12 elsewhere. This is the same trade
bgfx and sokol_gfx make, but SDL_GPU is already linked and matches SDL's
window/event lifecycle we already use.

We **do not** need to write our own renderer in the driver sense. We *do*
need to write:

- one vertex shader + one fragment shader (textured mesh, palette-lookup or
  RGBA — see below),
- a small mesh builder that walks the 32×32 grid and emits floor/ceiling/wall
  quads for the cells around the party,
- a camera that maps `(party_x, party_y, facing)` to a view matrix (four
  fixed yaw angles unless we add smooth turns).

That is the whole graphics-programming surface. It's a weekend of code, not
a subsystem.

### Alternative renderers

- **bgfx** — mature, huge backend matrix, own shader lang (`shaderc`). More
  library to vendor; overlaps what SDL_GPU already gives us.
- **sokol_gfx** — header-only, tiny, similar spirit. Missing SDL's
  window/event integration; we'd bridge them.
- **raw Vulkan** — full control, weeks of instance/device/swapchain/sync
  code, and we'd still need Metal/D3D fallbacks for the ports.

Recommendation: **SDL_GPU**. Zero new dependencies, Vulkan on the desktops
where you asked for it, portable to the mobile/console targets already
planned, matches the "reach for stdlib/platform first" habit the rest of
the codebase already follows.

## What we render — the data pipeline

Two paths, pick per phase:

**A. Reuse the original 2D wall art as textures.** The wall bitmaps EOB3
ships (loaded per-level, palette PAL_WALLS at 0xB0) are 8-bit indexed
sprites drawn at fixed depths. We can decode them once (we already do — see
`Graphics::mShapeCache`), upload as `R8_UINT` textures + a 256-entry
palette LUT texture (or bake to RGBA at load), and paste them on the wall
quads. Look stays vintage; textures come "for free" from the game files.
Downside: sprites were authored for one viewing angle and don't tile
cleanly across a wall face — some rework to identify "face" vs "trim"
regions, or accept the seams.

**B. New textures.** Hand-authored or AI-generated 128×128+ tileable
textures per level theme (crypt, sewer, temple, …). Best-looking result;
requires content pipeline (`data/textures/lvl01_wall.png`, …) and a
`--classic` toggle so purists can flip back to path A.

Either way the **geometry** is trivial: cell (x, y) with `wall_N/E/S/W` flags
in the level map ([docs/eob3_savegame_format.md](eob3_savegame_format.md))
extrudes to a floor quad + ceiling quad + one wall quad per solid face.
Emit only cells within N tiles of the party (N=8 covers the visible cone
comfortably); rebuild on party move (~30 quads/frame — negligible).

Monsters, items, decorations: keep as **billboard sprites** in phase 1
(fast, preserves art). Upgrade to 3D models later if wanted; the SOP won't
notice — it only asks the runtime to place an entity at a tile.

## Integration plan

Phased so nothing regresses:

1. **Split the dungeon-view rect out of the flattened SDL surface.** Today
   everything renders to one `SDL_Surface *mScreen` blitted through
   `SDL_Texture *mPresentTex`. Add a second target — an SDL_GPU
   framebuffer — for the 176×121 dungeon rect. Compose them at present:
   3D rect on the bottom, 2D surface on top with palette index 0
   transparent inside the rect. `setClip` already gates the region; we
   intercept the SOP's dungeon-view draws (they're identifiable — they
   pass a specific `cacheId` and target coordinates inside the rect) and
   *drop* them. HUD, panels, compass, monsters (initially) still hit the
   2D surface.
2. **Static geometry MVP.** Textured walls/floor/ceiling from path (A).
   Camera on party tile, yaw from facing. Turn = snap camera to new yaw.
   Verify with `THIRDEYE_AUTOWALK` (the harness already exists — see
   CLAUDE.md).
3. **Billboards.** Monsters + items as SDL_GPU-drawn quads, using the
   sprite the SOP would have drawn. Depth-sort against the mesh via the
   depth buffer. Compare frame diffs against the 2D baseline with the
   image-compare skill.
4. **Grid effects.** Doors (animated Y-slide of a wall quad), teleports
   (already logic-only; visuals unchanged), spinners (yaw the camera —
   the SOP already flipped `party_facing`, we just interpolate). Illusory
   walls: skip the wall quad when the cell is flagged illusory.
5. **Polish.** Smooth turns / smooth steps (optional; a `--classic-turn`
   flag keeps snap for muscle-memory), simple per-light torch flicker,
   fog on far tiles for depth cue.

Phases 1–3 give a playable 3D dungeon; 4–5 are quality.

## Risks / gotchas

- **Palette animation.** EOB3 cycles palette entries for water/torch
  effects. Path A needs the palette LUT texture updated per frame (cheap,
  one 256-byte upload). Path B loses this "for free" and needs shader
  animation for the same effect.
- **VM stack discipline is unchanged.** The runtime side of this is
  read-only — we consume `party_x/y/facing` and the level maze. Do not
  "help" the SOP; it must still run its draw handlers even if we ignore
  the output. Skipping SOP draws entirely will desynchronise state the
  handlers side-effect on (see [architecture.md](architecture.md), stack
  discipline warnings).
- **Monsters in the dungeon rect stop being 2D at phase 3.** Until then
  they draw on the 2D surface *over* the 3D rect — the transparent-index
  compose keeps them visible but they won't be depth-sorted against walls
  (a monster around a corner is drawn on top). Acceptable for MVP; phase 3
  fixes it.
- **Text/HUD/inventory are untouched.** All the fiddly SOP-driven UI (text
  windows, save picker, spell picker, compass restamp,
  `pixelFade`/`snapshotScreen`) still runs against the 2D surface as
  today. The 3D work is *additive*.
- **macOS Vulkan.** Not native. SDL_GPU picks Metal there without asking
  us to care; if a contributor specifically wants Vulkan-on-macOS they
  link MoltenVK, but there's no reason we should.

## Ultima Underworld-style feel: mouselook + free movement?

Two very different asks, don't lump them:

**Free look (mouse yaw/pitch): trivial.** Camera-only. Rotates the view
matrix; the SOP never sees it — internal facing is still snapped to
N/E/S/W between turn keypresses. Ceiling/floor become visible so they
need real textures (already in the plan). Snap the yaw back to a cardinal
when the player presses a turn key so gameplay facing stays coherent.
~50 lines on top of phase 2.

**Free movement (step off the grid, Underworld-style): expensive and
half-incompatible.** Every SOP handler — combat reach, monster AI, spell
targeting, doors, teleports, spinners, pressure plates, `step_square_*` —
assumes the party occupies exactly one tile. Underworld was grid-free by
design; EOB3 was not. Two ways to fake it:

- **Grimrock trick — smooth glides between tiles.** Camera interpolates
  the walk/turn animation over ~150 ms; the sim still ticks discretely
  on arrival. Feels 90% Underworld, keeps the SOP honest, no state-drift
  risk. Cheap — one lerp on top of the existing tile-step.
- **Actually free-roam.** Own collision against the grid, re-project the
  party to the nearest tile whenever the SOP asks. Anything the SOP
  triggers on tile-entry (traps, encounters, scripts) becomes fragile.
  Bug farm. Not recommended.

Recommendation: **mouselook + smooth glides.** Underworld *feel* without
fighting the fact that EOB3's logic is grid-native.

## Grimrock-style presentation: kill the panel HUD?

Yes, in the same sense that phase 1 already sidesteps the SOP's dungeon
draws: **intercept and drop, then re-host in our own overlay.** The 320×200
panel art (side rails, character portraits column, compass box, action
buttons) is drawn by identifiable SOP `drawImage` calls into known screen
regions; the same interception hook that ignores wall shapes ignores those.

What replaces it:

- **Fullscreen 3D.** The dungeon-view rect grows from 176×121 to the
  whole window at native resolution. No compose seam needed for the
  common case — the 2D surface only lights up when a modal opens.
- **Floating party cards.** Four (or six) portrait tiles laid out along
  the bottom or side, each showing portrait + name + HP/SP bar + status
  icons. These are our own widgets drawn straight into SDL_GPU (or as
  SDL_Renderer overlay on top of the SDL_GPU pass — both work). Front
  row / back row is a 2-column arrangement; a 3×2 grid for six members
  reads well at 1080p+.
- **Compass at the bottom.** Free — we already snapshot the compass rect
  every frame (`mCompassSnap`). Re-target that texture to a bottom-center
  overlay position at whatever size looks right; the SOP still refreshes
  its 32×32 source rect and we upscale for display.
- **Modal panels (inventory, spellbook, camp, save/load).** Keep the
  existing SOP-driven screens for phase 1 — open them as a centered
  translucent-backed 320×200 window over the 3D view. They already work;
  don't rebuild what isn't broken. Rebuild them native later if we care.

The one sticky bit is **input routing.** Today, clicking a character
portrait or an attack slot hits SOP-owned pixels and the SOP handles it.
Once the portraits move to our overlay at new coordinates, either:

- **Forward hits back to the SOP's original screen coords.** Cheap, keeps
  the SOP as the single source of truth for "what did the player click."
  Works for everything the SOP already handles (portrait select, attack,
  spell button). One remap table.
- **Route direct to runtime state.** Cleaner long-term, but you end up
  reimplementing a chunk of the SOP's UI logic. Skip for MVP.

Take the forward path. It's ugly, it's honest, and every hour spent
re-doing the SOP's UI is an hour not spent on Phase 3 combat/save.

**Cost added to the plan:** one overlay module (portrait cards + compass
placement + hit-forwarding table). Sits between phase 3 (billboards) and
phase 4 (grid effects). Assume a week; less if we accept the SOP's modals
as-is.

Skipped, add when someone asks:

- Custom high-res font for the new HUD (current text is 320×200-native and
  will look chunky at 1080p — arguably a feature, à la Grimrock's pixel
  aesthetic; swap it if the pixels bother us).
- Native-3D inventory/spell screens.
- Per-portrait damage flash / hit-animation polish.

## Bottom line

- **Possible?** Yes, at a scope commensurate with roughly the current
  graphics module (~1.4k lines) — most of that is content wrangling and
  the compose seam, not GPU driving.
- **Vulkan without our own driver?** Yes, via SDL_GPU's Vulkan backend.
- **When to start?** Not before combat + save/load land ([roadmap.md](roadmap.md)
  Phase 3). A 3D view over a still-broken combat loop is a distraction; a
  3D view over a shippable game is a headline feature.
