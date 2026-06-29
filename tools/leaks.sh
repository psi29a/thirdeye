#!/usr/bin/env bash
# macOS-only: run the existing build/thirdeye (or any binary the build target
# produces) under `leaks -atExit`, which dumps any blocks that were still
# allocated when the process exited. Complementary to tools/sanitize.sh:
# AddressSanitizer catches uninitialised reads and OOB at the moment they
# happen; `leaks` summarises what was simply never freed by exit.
#
# Usage:
#   tools/leaks.sh                              # runs thirdeye with --skip-intro
#   tools/leaks.sh -- <thirdeye args>           # runs with your args
#   tools/leaks.sh runtests                     # runs the gtest binary
#   tools/leaks.sh daesoptests                  # runs the daesop gtest binary
#   tools/leaks.sh arc -- <arc args>            # runs the arc compiler
#   tools/leaks.sh daesop -- <daesop args>      # runs daesop
#
# Targets a build that already exists; build it first via the normal
# `cmake --build build` flow. Set LEAKS_BUILD_DIR to point elsewhere.
set -euo pipefail
cd "$(dirname "$0")/.."

if [ "$(uname -s)" != "Darwin" ]; then
  echo "leaks(1) is macOS-only. On Linux, use tools/sanitize.sh (ASan with" >&2
  echo "detect_leaks=1 does the same job)." >&2
  exit 1
fi

BUILD_DIR=${LEAKS_BUILD_DIR:-build}
APP=$BUILD_DIR/thirdeye.app/Contents/MacOS

target=thirdeye
case "${1:-}" in
  thirdeye|runtests|daesoptests|arc|daesop)
    target=$1
    shift
    ;;
esac

if [ "${1:-}" = "--" ]; then
  shift
fi

# MallocStackLogging gives leaks(1) the alloc backtrace; without it you get a
# block size with no call site, which is useless. MallocScribble fills freed
# memory so any use-after-free shows up as 0x55... instead of stale data.
export MallocStackLogging=1
export MallocStackLoggingNoCompact=1
export MallocScribble=1

# sdl2-compat dlopens libSDL3 at runtime; Homebrew's lib isn't in dyld's
# default fallback search path.
if [ -d /opt/homebrew/lib ]; then
  export DYLD_FALLBACK_LIBRARY_PATH=${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}
fi

# Default args for thirdeye if no extras passed: skip the intro so leaks is
# observable within seconds rather than waiting on a full cinematic.
if [ "$target" = "thirdeye" ] && [ "$#" -eq 0 ]; then
  set -- --skip-intro --skip-menu
fi

exec leaks -atExit -- "$APP/$target" "$@"
