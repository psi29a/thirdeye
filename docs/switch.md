# Nintendo Switch (+ Switch 2) — plan

> **Read this first.** There are two paths to a Switch build and only
> one is technically real for us today. The other is a licensing wall
> we can't quietly wave a lazy solution at. Both are laid out honestly
> below so the choice is informed.

## The two paths

### Path A — homebrew (`.nro`, custom-firmware only)

Runs on Switches with **CFW** (Atmosphère et al.). Not a general
audience, not a store, not GoG-shippable. Fine as "does it work?" and
as a real deliverable for the CFW community — think ScummRVM /
DevilutionX on Switch.

Toolchain: **devkitPro + libnx**, produces a `.nro` you load via the
homebrew menu. GPLv3 is fine here — nobody's asking us to hide source.

### Path B — licensed developer (retail Switch, eShop)

Requires a Nintendo Developer Portal account, a signed NDA, dev-kit
hardware (paid), and Nintendo's licensing terms. Two blockers stack
here for us:

1. **GPLv3 is fundamentally hostile to Nintendo's platform.** Their
   distribution requires code signing keys under NDA and their SDK
   terms restrict what you can publish about the platform — GPLv3's
   "convey the corresponding source" plus its anti-tivoization
   clauses (§6) make retail eShop distribution legally awkward at
   best. This is the same wall that kept ScummVM off the eShop.
   Getting past it needs either relicensing (impossible — we depend on
   GPLv3 deps and would need every contributor to agree) or a written
   exception + Nintendo legal sign-off (unrealistic).
2. **We are not the copyright holder of EoB3.** Even if the runtime
   licensing were solved, publishing an EoB3 runtime on eShop requires
   the rights holder (currently Wizards of the Coast for the D&D IP;
   the code itself was Westwood/SSI) to publish it, not us. This is
   GoG's or WotC's call, not ours.

**Recommendation: don't pursue Path B.** If GoG (or whoever ends up
holding EoB3 distribution rights) wants a Switch release, *they* apply
to Nintendo, using Thirdeye as the runtime under a bespoke license
grant we'd negotiate then. Not before. Nothing in Path A blocks that
future.

The rest of this doc is Path A only.

## Stack, path A

- **SDL2 on libnx** — mature, well-worn port via devkitPro's package
  index (`devkitpro-pacman -S switch-sdl2`).
- **SDL3 on libnx** — active work, not as battle-tested as SDL2. If
  it's ready by the time we start, use it (one codebase with
  desktop/mobile). If not, keep the Switch build on SDL2 as a branch
  until SDL3 catches up. Ponytail: pick whichever is boring, don't
  fork the world.
- **OpenAL-Soft** — has a libnx audren backend, builds via CMake with
  the devkitPro toolchain file.
- **WildMIDI** — pure C, cross-compiles clean with `switch-gcc`.
- **CLI11** — header-only, fine (though the Switch has no CLI —
  arguments come from `argv[]` seeded by the homebrew menu).
- **GoogleTest** — desktop-only. `if(NOT NSWITCH)` around `runtests`.

## Phase 0 — devkitPro smoke (1 day)

Install devkitPro + `switch-dev` group, cross-compile SDL3 (or SDL2)
+ OpenAL-Soft + WildMIDI as static libs. Confirm they link into a
trivial `.nro` and boot on Ryujinx (emulator, fine for CI) and on a
real CFW Switch (real gate).

## Phase 1 — build system

devkitPro ships a CMake toolchain file (`SwitchCMakeToolchain.cmake`).
Point CMake at it, add an `if(NSWITCH)` branch that:

- Uses `nx_generate_nacp` + `nx_create_nro` from
  `dkp-nx-package.cmake` to bundle icon + metadata + our binary into a
  `.nro`.
- Skips the `.app` bundle, `runtests`, and any SDL3 desktop-only bits.
- Static-links everything — no shared libs on Switch.

Skipped: a separate CMakePresets entry until it hurts. One
`-DCMAKE_TOOLCHAIN_FILE=…` flag is fine.

## Phase 2 — asset handoff

CFW Switches have an SD card. Convention on Switch homebrew is
`/switch/<appname>/`.

- User drops their `data/` tree into `/switch/thirdeye/data/` on the
  SD card. First launch reads from there directly — no copy needed,
  the SD is already writable.
- WildMIDI patches: bundle inside the `.nro` (RomFS), extract to
  `/switch/thirdeye/wildmidi/` on first run.
- Saves + `.TMP`s: `/switch/thirdeye/saves/`. Persists across
  reboots. No Nintendo cloud saves (that's a Path B feature).

RomFS is baked into the `.nro` at build time via
`nx_create_nro(... ROMFS ...)`; use it for the patches and any
metadata, never for game assets.

## Phase 3 — input

Nintendo controllers are a first-class SDL joystick target.

- **Joy-Con / Pro Controller** via SDL3 gamepad API. Map:
  - Left stick — 8-way movement (WASD+QE)
  - D-pad — turn L/R + strafe L/R
  - Right stick — mouse cursor
  - A — click / Enter
  - B — Esc / back
  - X — attack-all (the same "right-thumb attack" the mobile
    overlay exposes)
  - Y — inventory
  - L/R — cycle party member
  - ZL/ZR — page spellbook
  - Plus — pause menu, Minus — map
- **Touchscreen (handheld mode)** — reuse the mobile touch = mouse
  layer. Same code, no fork.
- **USB keyboard** (via hori/keyboard adapters) — SDL3 gives it free.
  Useful for chargen names.
- **Chargen text entry:** libnx's `swkbdCreate` software keyboard,
  wrapped as a `SDL_StartTextInput()` implementation.

## Phase 4 — display

- Docked: 1920×1080. Handheld: 1280×720. Both handled by
  `SDL_SetRenderLogicalPresentation(320, 200, …)` with integer
  letterboxing.
- No cutouts, no safe areas, no orientation lock — landscape always.
- Handle **dock/undock** events (`AppletHookType_OnOperationMode`) to
  reconfigure the render target when the user pops the console in or
  out.

## Phase 5 — distribution (path A)

- **`.nro` sideload:** publish releases on GitHub. Users copy to
  `/switch/thirdeye/thirdeye.nro` on their SD card. That's it.
- **Homebrew App Store** (`hb-appstore` / hbas) — submit the release
  there for one-tap install by the CFW community. Free.
- No signing. No storefront. No treadmill.

Skipped forever (until Path B is real): eShop, retail cart, Nintendo
account integration, cloud saves, achievements.

## Switch 2

Same document, mostly. Differences:

- **CFW status:** as of writing (mid-2026), no public CFW / homebrew
  loader for Switch 2 exists. Path A is *aspirational* on Switch 2 —
  we don't ship a `.nro` for it until the community produces a
  loader. Nothing to do now beyond keeping libnx-2 (or whatever the
  successor is called) on our radar.
- **Compat mode:** early reports say Switch 2 runs Switch 1 `.nro`
  files under its backwards-compat layer on CFW-capable units. If
  true, our Switch 1 build serves Switch 2 users free. Confirm before
  claiming it.
- **Path B:** identical wall, slightly higher. Same recommendation:
  not our fight, GoG's or WotC's.

## Order of work

1. Phase 0 devkitPro smoke — 1 day.
2. Phases 1 + 2 + 4 together — first `.nro` that boots to the title
   screen on Ryujinx. Demo.
3. Phase 3 (gamepad) — the whole point of a Switch build.
4. Phase 3 (touchscreen handheld) — free via the mobile overlay.
5. Phase 5 — GitHub release + hbas submission.

Whole project is probably 2–3 weekends of work *after* mobile ships,
because the mobile touch overlay does most of the handheld-mode input
lifting for free.

## GoG angle — the honest version

Path A on Switch is a gift to the CFW community, not a
storefront-shippable product. If GoG wants Thirdeye on a retail
Switch, they own the Path B conversation with Nintendo and WotC. We
keep our runtime clean, portable, and easy to embed so *if* that
conversation succeeds, the engineering side is already done.
