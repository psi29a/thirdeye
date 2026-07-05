# Launcher — plan

A small Qt6 front-end that finds `EYE.RES`, remembers a few launch flags, and
runs `thirdeye`. One window, tabs allowed but not required.

## Goal

Non-technical user double-clicks a single icon, gets pointed at their game
files if they don't have them yet, tweaks scale / fullscreen, hits **Play**.
No shell required.

## Non-goals

- **Not a game-store front-end.** No auto-download of copyrighted bits (see
  [Open question 1](#open-questions) below).
- **Not a settings editor for every `THIRDEYE_*` env var.** Those are for us,
  not the player. Launcher exposes only the QoL flags below.
- **Not a save manager, not a mod manager.** Both are Phase-4 material at
  earliest; if we build them they live elsewhere.
- **Not cross-integrated with the engine.** Launcher spawns `thirdeye` via
  `QProcess` and forgets it. No shared state, no IPC.

## What ships (MVP)

One window, one Play button. Tabs behind it iff the flat layout gets crowded.

### Fields

| Field | Widget | Persisted as |
|---|---|---|
| Path to `EYE.RES` | `QLineEdit` + Browse | `QSettings` `gamePath` |
| Scale | `QComboBox` (1×–5×, default 3×) | `scale` |
| Skip intro | `QCheckBox` (default on) | `skipIntro` |
| Skip menu | `QCheckBox` (default off) | `skipMenu` |
| Disable sound | `QCheckBox` (default off) | `nosound` |
| VM debug trace | `QCheckBox` (default off) | `debug` |

Fullscreen deferred — engine has no `--fullscreen` flag yet; add it to
`apps/thirdeye/main.cpp` and surface a checkbox as a follow-up.

`QSettings` is Qt-stdlib — no config file format to invent. Writes to
`~/Library/Preferences/…`, `~/.config/…`, or the registry per platform.

### Layout sketch

```text
┌─ Thirdeye Launcher ─────────────────────────┐
│ Game folder: [/path/to/eob3       ] [Browse]│
│              ✓ Found EYE.RES                │
│                                             │
│ Video ─ Scale: [3× ▾]                       │
│                                             │
│ Boot  ─ ☑ Skip intro cinematic              │
│         ☐ Skip title menu                   │
│         ☐ Disable sound                     │
│         ☐ VM debug trace (verbose stdout)   │
│                                             │
│ [ Where do I get the game? ]    [ Play ▶ ]  │
└─────────────────────────────────────────────┘
```

Single page. Add tabs (Video / Gameplay / Debug) only if a Phase-2 addition
makes this feel cramped.

### "Where do I get the game?"

Opens a small dialog with two options and clear language about what each is:

- **Buy it from GOG** —
  <https://www.gog.com/game/forgotten_realms_the_archives_collection_two>
  (EOB3 ships inside this collection alongside EOB1 and EOB2). Opens in the
  system browser. Purchase-only; GOG has no anonymous download API.
- **Download from the Internet Archive** —
  <https://archive.org/details/eye-of-the-beholder-3>. Abandonware upload,
  not officially licensed. Dialog says so verbatim; the user clicks through
  knowing that's on them.
- **"I already have the files"** — closes the dialog, focus goes back to the
  path picker.

Phase 1 launcher fetches no bytes itself. Phase 3 optionally automates the
archive.org path — see below.

### Path validation

On path change, check for `EYE.RES` (case-sensitive — see CLAUDE.md gotcha).
Green check if present, red X + one-line reason otherwise. Play button
disabled until green.

### Launch

Resolve the engine beside the launcher via
`QCoreApplication::applicationDirPath() + "/thirdeye"` (`+ ".exe"` on
Windows), then `QProcess::startDetached(exe, args)`. Args built from the
checkboxes. Launcher exits after spawn — no "running" state to babysit.

## Tech choices (short)

- **Qt 6.7+**, CMake, C++20, single new subdir `apps/launcher/`. Add to the
  top-level `CMakeLists.txt` behind `option(THIRDEYE_BUILD_LAUNCHER ON)` so a
  headless CI still builds without Qt.
- **QSettings** for persistence. Not JSON, not TOML, not our own.
- **QProcess** to launch. `startDetached` — no lifecycle to manage.
- **QDesktopServices::openUrl** for the "where do I get it" link.
- **No custom widgets.** Everything is stock Qt.
- **No .ui XML files** unless the layout genuinely warrants Designer. A
  ~150-line `QWidget` subclass is smaller than an XML round-trip.
- Bundle: `macdeployqt` / `windeployqt` / linuxdeploy — Qt-stdlib scripts.

## Phased rollout

### Phase 1 — the actual launcher (MVP above)

Enough to replace the current "open a terminal and type flags" ritual. This
is the whole product for most users.

- [x] `apps/launcher/CMakeLists.txt` with Qt6::Widgets + AUTOMOC
      (`BUILD_LAUNCHER=ON` already existed in top-level; kept OFF by default
      so CI without Qt still builds)
- [x] `MainWindow` with fields above
- [x] `QSettings` load/save (native store per platform)
- [x] Path validation (looks for `EYE.RES` in picked folder)
- [x] Play button → `QProcess::startDetached(applicationDirPath()+"/thirdeye"[+".exe" on Win], args)`
- [x] "Where do I get it?" dialog with GOG + Internet Archive buttons
- [ ] **Deferred:** Fullscreen checkbox — engine has no `--fullscreen` flag
      yet. Add a flag to `apps/thirdeye/main.cpp` + wire it through the SDL
      window setup, then surface the checkbox. Follow-up.
- [ ] **Deferred:** macOS `.app` bundle for the launcher itself. Right now
      the launcher binary lives *inside* `thirdeye.app/Contents/MacOS/` next
      to the engine — invokable but not double-clickable. Add a separate
      `thirdeye-launcher.app` with its own `Info.plist` when we do proper
      packaging.
- [ ] **Deferred:** `macdeployqt`/`windeployqt`/`linuxdeploy` runs — needed
      before distributable builds, not for local dev.

### Phase 2 — zip/arj unpack (optional, gated on Open question 1)

Only if we decide it's OK to *touch* archive files the user provides. Launcher
still doesn't download.

- [ ] "I have a `.zip`" flow → user picks a file → we extract
- [ ] Zip via shell-out to system `unzip` (mac/linux/`tar -xf` on Win 10+) —
      no library dep
- [ ] `.arj` via `arj`/`7z` if present on PATH; otherwise a "please install
      arj / 7-zip" nudge with copy-pasteable install commands

`ponytail:` shell-out over bundling libarchive; swap if a Windows user hits a
`tar`-can't-read-arj wall.

### Phase 3 — auto-download from archive.org

The user has to say yes each time — the dialog spells out that the source is
an abandonware mirror and clicking "Download" is their call, not ours.
Thirdeye still ships zero copyrighted bytes; we just automate what the user
would otherwise do manually.

- [ ] Consent dialog (checkbox: "I understand this is not an official
      source"). No download without it. No hidden default that skips it.
- [ ] `QNetworkAccessManager` fetches `EOB3_Disk{1..4}.zip` from
      `https://archive.org/download/eye-of-the-beholder-3/`. Progress bar,
      resume on 206 if the server allows it, cancel button.
- [ ] Extract + arj-unpack per Phase 2 → drop `EYE.RES` etc. into the folder
      the user picked → auto-fill the game-path field.

**GOG automation is out of scope.** GOG has no anonymous download API;
community CLIs like `lgogdownloader` need the user's login and 2FA. Bundling
that is a big surface (credential handling, GOG-ToS territory) for a feature
most GOG buyers can do faster with GOG Galaxy. Recommendation: don't build
it — the "Buy on GOG" link in the help dialog is enough.

Kept last so the shippable launcher doesn't wait on this.

### Later — nice-to-have

- Auto-detect GOG / Steam install paths (registry on Windows, plist on mac)
- Save-slot browser (needs the save format work in [roadmap.md](roadmap.md)
  Phase 3 to land first)
- Update check against the GitHub releases API

## Open questions

1. ~~Auto-download from archive.org — yes / no?~~ **Resolved 2026-07-05:**
   yes, gated behind an explicit consent dialog. Thirdeye still ships no
   copyrighted bytes; if the user accepts the archive.org path, that's their
   call. GOG stays as a "Buy it" link only — no automation (see Phase 3).
2. ~~One binary or two?~~ **Resolved 2026-07-05:** two binaries.
   `thirdeye-launcher` alongside `thirdeye`. Launcher spawns engine via
   `QProcess::startDetached`; power users keep invoking `thirdeye` directly.
3. ~~Linux launcher?~~ **Resolved 2026-07-05:** yes, full cross-platform —
   Linux, macOS, Windows. Same CMake target on all three; packaging per
   platform (`linuxdeploy`/AppImage, `macdeployqt`/`.app`, `windeployqt`/
   installer).

## What we're deliberately not building

- A config editor for every `THIRDEYE_*` env var — those are debug tools,
  not player settings.
- A custom settings-file format — `QSettings` is fine.
- A `.ui` Designer file — the layout is a dozen widgets in a grid.
- Our own zip/arj library — shell out to what's on the box.
- A crash reporter, telemetry, update notifier — YAGNI until someone asks.
