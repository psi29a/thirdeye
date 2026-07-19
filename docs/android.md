# Android port — plan

Goal: **make Thirdeye the way EoB3 is played on Android** — signed APK
sideload for v1, Play Store viable as a v2 so GoG can ship Thirdeye as
their primary EoB3 executable if they want to.

Non-goal: shipping game assets. User (or GoG's installer) provides
`../data/`; Thirdeye is the runtime.

## Stack, confirmed

- **SDL3** — first-class Android backend, ships a template project.
- **OpenAL-Soft** — already what we link. Has AAudio/OpenSL ES backends.
  Phase 0 becomes a smoke build, not a swap.
- **WildMIDI** — pure C, cross-compiles clean. Needs a GUS patch set
  (`freepats` or shipped `wildmidi.cfg`) bundled as an APK asset.
- **CLI11** — header-only, fine.
- **GoogleTest** — desktop-only. `if(NOT ANDROID)` around `runtests`.

## Phase 0 — NDK smoke (½ day)

Cross-compile SDL3 + OpenAL-Soft + WildMIDI with the NDK for
`arm64-v8a`. Nothing linked to Thirdeye yet. If any of the three fights
the NDK, everything after this is theater.

## Phase 1 — build system

Take the SDL3 Android template verbatim, drop our CMake tree in as a
subdirectory. Don't invent our own Gradle setup.

- `android/` at repo root: `build.gradle`, `AndroidManifest.xml`,
  `src/main/java/…/ThirdeyeActivity.java` (subclass of `SDLActivity`,
  ~15 lines).
- Our `CMakeLists.txt` gets `if(ANDROID)` guards: skip the `.app`
  bundle, skip WildMIDI's install rules, skip `runtests`.
- ABIs: `arm64-v8a` only for v1. Add `armeabi-v7a` if someone asks.
  `x86_64` for the emulator only, never shipped.
- `minSdk 26` (Android 8) — gets us AAudio + scoped storage without
  legacy paths.

Skipped: a separate `build-android/` tree in-repo. Gradle owns its own
build dir under `android/`.

## Phase 2 — asset handoff

User provides `EYE.RES`. On modern Android that means **Storage Access
Framework**, not `/sdcard/data/`.

- First launch: `ACTION_OPEN_DOCUMENT_TREE` picker → copy the whole
  `data/` tree once into the app's private files dir
  (`Context.getFilesDir()`). Everything downstream reads a normal POSIX
  path. `res.hpp` doesn't change.
- WildMIDI patches: ship inside the APK as assets, extract to files dir
  on first run alongside a generated `wildmidi.cfg`. Same one-time-copy
  pattern.
- Saves + `.TMP`s: app-private files dir. Automatic Android backup with
  `allowBackup="true"`.

Skipped: wrapping SAF as a VFS. Weeks of work for zero user-visible
benefit — the one-time copy is fine.

## Phase 3 — input

Most of EoB3's UI is already mouse-clickable regions (buttons, spell
scrolls, inventory slots, compass, spinner arrows) — a finger *is* a
mouse there. Build the touch layer in rungs, and stop at whichever one
feels right.

1. **Free tier — Bluetooth keyboard.** SDL3 gives it to us. Works day
   one for tablet-with-keyboard users.
2. **Touch = mouse.** Every tap/drag inside the render surface becomes
   an `SDL_EVENT_MOUSE_*` at that coordinate. Existing hit-testing
   code doesn't know the difference. Handles menus, chargen, inventory,
   spells, compass, dungeon-view clicks (open doors, pick up items) —
   probably 80% of the game.
3. **Virtual thumb overlay** for the parts a finger-as-mouse doesn't
   cover:
   - **Left thumb:** 8-way movement pad (WASD + QE strafe).
   - **Right thumb:** "attack all" button — one tap fires every armed
     party member. Later: split into per-slot attack quadrants if
     that feels better than one big button.
   - Draw it as an SDL3 renderer overlay, not native Android views —
     one codebase, and a `--touch-overlay` flag can turn it on for
     desktop testing.
4. **Soft keyboard** for chargen name entry via `SDL_StartTextInput()`.
   Skipped: rolling our own virtual keyboard.

## Phase 4 — display

- Native 320×200. `--scale=N` becomes "fit to screen, integer scale
  where possible, letterbox the rest" via
  `SDL_SetRenderLogicalPresentation`.
- Notches/cutouts: `android:windowLayoutInDisplayCutoutMode="shortEdges"`,
  letterbox eats the unsafe area.
- `android:screenOrientation="landscape"`. This is a dungeon crawler.

## Phase 5 — signing & distribution

- **Debug APK for sideload:** `./gradlew assembleDebug`. Testers get
  this.
- **Release APK/AAB:** `keytool -genkey -v -keystore
  thirdeye-upload.jks …`, password in `~/.gradle/gradle.properties`
  (never in the repo), `./gradlew bundleRelease`.
- Keystore backup lives outside the repo. Losing it = losing app
  identity on Play forever.

Skipped: CI-built APKs. Add when someone other than the maintainer
needs to build them.

## Phase 6 — Play Store (kept viable, not v1)

Not blocking v1, but every earlier phase should stay Play-compatible so
we don't have to redo work if GoG comes knocking:

- `minSdk 26` and current `targetSdk` — Play forces a `targetSdk` bump
  yearly, plan on it.
- GPLv3 is compatible with Play distribution (ScummVM/DOSBox both
  ship there). Source-offer link in the store listing.
- Store listing must state clearly: user provides their own EoB3
  install. Same posture ScummVM/DOSBox use — a GoG bundle satisfies
  this trivially by dropping `data/` alongside the app on first run.
- Content rating + privacy policy (we collect nothing).
- No proprietary game assets in the APK, ever.

## Order of work

1. Phase 0 smoke — 1 day.
2. Phases 1 + 2 + 4 together — real `EYE.RES` on-device, BT keyboard.
   That's the demo.
3. Phase 3.2 (touch = mouse) — probably one afternoon, unlocks most of
   the game.
4. Phase 3.3 (thumb overlay) — after playing with 3.2 to see what
   actually feels bad.
5. Phase 5 release signing — when there's something worth signing.
6. Phase 6 — only if GoG (or anyone else) makes it worth the treadmill.

## GoG angle — what makes us the obvious choice

If GoG ships EoB3 today it's DOSBox + hand-tuned config. Thirdeye
beats that on:

- **Native res + scaling** — no DOSBox blur.
- **Real save/load UI** — no F5/F9 slot hunting.
- **Portable** — same runtime on desktop and Android from one source
  tree.
- **Maintained** — DOSBox drift is their problem, not ours.

Keep the CLI (`--skip-intro`, `--scale=N`) and asset-directory
convention stable so a GoG installer can shell out to us the same way
on every platform.
