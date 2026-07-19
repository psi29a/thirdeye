# iOS port — plan

Goal: **Thirdeye on iPad (primarily) and iPhone (secondarily)** — as the
way EoB3 is played on Apple mobile. TestFlight builds for v1, App Store
viable as v2 so a GoG bundle can point at us.

Non-goal: shipping game assets. User (or a GoG-provided archive) drops
`EYE.RES` in via the Files app.

## Stack, confirmed

- **SDL3** — first-class iOS backend, ships an Xcode template.
- **OpenAL-Soft** — CoreAudio backend on iOS, builds via CMake for
  `arm64` device + `arm64` simulator. Apple's system OpenAL is
  deprecated; we ignore it, link Soft statically.
- **WildMIDI** — pure C, cross-compiles clean. Patches ship as bundle
  resources.
- **CLI11** — header-only, fine.
- **GoogleTest** — desktop-only. `if(NOT IOS)` around `runtests`.

## Phase 0 — Xcode/SDK smoke (½ day)

Cross-compile SDL3 + OpenAL-Soft + WildMIDI for `iphoneos` and
`iphonesimulator`. Nothing linked to Thirdeye yet. Confirms
signing/entitlement plumbing before we care about it.

## Phase 1 — build system

Take SDL3's iOS Xcode template, drop our CMake tree in as an external
build.

- `ios/` at repo root: `Thirdeye.xcodeproj`, `Info.plist`,
  `AppDelegate.m` (subclass of `SDL_UIKitAppDelegate`, ~10 lines).
- CMake generates an Xcode project for our sources; the top-level
  Xcode workspace embeds it. `if(IOS)` guards: no `.app` bundle
  install rules (Xcode owns bundling), no `runtests`, no CLI-only
  scale flag defaults (we always fit-to-screen).
- Architectures: `arm64` device + `arm64` simulator only. Ship a
  universal `.xcarchive`.
- Deployment target: **iOS 14** — gets us pointer/mouse events, the
  Files app APIs we want, and matches what SDL3 targets cleanly.

Skipped: CocoaPods / SPM. CMake already owns our deps; adding a
second package manager is complexity for no gain.

## Phase 2 — asset handoff

User provides `EYE.RES`. Apple's answer is the **Files app** +
`UIDocumentPickerViewController`.

- First launch: document picker at folder scope → copy the whole
  `data/` tree once into the app's `Documents/` directory
  (`NSFileManager.default.urls(for: .documentDirectory)`).
  Everything downstream reads a normal POSIX path. `res.hpp` doesn't
  change.
- Enable **file sharing** (`UIFileSharingEnabled=YES`) and **open
  in-place** (`LSSupportsOpeningDocumentsInPlace=YES`) in `Info.plist`
  so `Documents/` shows in the Files app — user can drop files there
  from Finder over USB, iCloud, or a thumb drive.
- WildMIDI patches: ship inside the app bundle as resources, extract
  to `Documents/` on first run alongside a generated `wildmidi.cfg`.
- Saves + `.TMP`s: `Documents/saves/`. Backed up by iCloud
  automatically unless we opt out (we don't).

Skipped: writing a `UIDocument`-based VFS. The one-time copy is fine.

## Phase 3 — input

Same rungs as [Android](android.md#phase-3--input), same code — the
touch-overlay is an SDL3 renderer layer, so iOS and Android share it.

1. **Free tier — Bluetooth/Smart Keyboard.** SDL3 gives it. Perfect
   for iPad users with a Magic Keyboard.
2. **Touch = mouse.** Tap/drag → `SDL_EVENT_MOUSE_*` at that pixel.
   Existing hit-testing works unchanged.
3. **Virtual thumb overlay:** left = 8-way movement, right = attack-all.
   Same overlay as Android, no fork.
4. **iPadOS pointer/trackpad** (iOS 13.4+) — SDL3 exposes it as mouse
   events. Free from step 2.
5. **Soft keyboard** via `SDL_StartTextInput()` for chargen names.

## Phase 4 — display

- 320×200 native → `SDL_SetRenderLogicalPresentation` with integer
  scale where it fits, letterbox the rest.
- **Safe area** handling: read `UIWindow.safeAreaInsets` (SDL3
  exposes it), letterbox inside the safe area so notch/Dynamic
  Island/home indicator never eat gameplay. Same letterbox math as
  Android cutouts.
- `UISupportedInterfaceOrientations = landscape only`.
- iPad: full-screen, no split-view. `UIRequiresFullScreen=YES`.

## Phase 5 — signing & distribution

iOS has no "sideload APK" equivalent for general users. The real
distribution rungs are:

- **Personal dev build:** free Apple ID, 7-day cert, install to your
  own device via Xcode. Fine for you.
- **TestFlight:** requires **Apple Developer Program** ($99/yr), gets
  us up to 10k external testers on 90-day builds. This is v1's real
  target.
- **App Store:** same paid account, App Review gate. See Phase 6.

Certificates + provisioning profiles live in Xcode's automatic
signing. The `.p12` export lives outside the repo. GoG or any other
distributor would need their own paid team; the codebase doesn't care.

## Phase 6 — App Store (viable, not v1)

Kept viable so nothing earlier has to be redone:

- **GPLv3 vs App Store** — historically fraught (VLC was pulled once),
  but ScummVM ships on the Store now. The load-bearing requirement is
  that the App Store's DRM/usage terms don't further restrict what
  GPLv3 grants. Our position: we're the copyright holder, we grant
  the additional permission. Add a written exception if a lawyer
  wants belt+suspenders.
- **User provides EoB3** — same posture as Android. A GoG installer
  bundle satisfies this by putting `data/` in the Documents dir on
  first run (deep-link + document provider extension).
- **No proprietary game assets in the IPA, ever.**
- **Deployment-target treadmill** — Apple bumps the minimum Xcode
  yearly, less painful than Android's `targetSdk` gate but real.
- **Privacy manifest** (`PrivacyInfo.xcprivacy`) — we collect nothing,
  declare nothing, done.

## Order of work

1. Phase 0 smoke — 1 day.
2. Phases 1 + 2 + 4 together — real `EYE.RES` on-device from Files
   app, Smart Keyboard input. Demo.
3. Phase 3.2 (touch = mouse) — same afternoon as Android.
4. Phase 3.3 (thumb overlay) — shared with Android, done once.
5. Phase 5 TestFlight — when there's something worth testing.
6. Phase 6 — only if the App Store is the goal.

## GoG angle — same as Android

Thirdeye vs "DOSBox + iOS wrapper" wins on the same axes: native res,
real save UI, portable across desktop/Android/iOS from one source
tree, iPad-first UI that a DOSBox wrapper can't match. Keep the
`--asset-dir` / `--skip-intro` CLI conventions stable so a GoG launcher
shells out identically on every platform.
