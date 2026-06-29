#!/usr/bin/env bash
# Build thirdeye with AddressSanitizer + UndefinedBehaviourSanitizer, then run
# the tests under it. Catches use-after-free, OOB reads/writes, signed overflow,
# misaligned loads, etc. -- the runtime equivalent of the Coverity static
# checks, but with concrete stack traces.
#
# Usage:
#   tools/sanitize.sh                  # configure + build + ctest
#   tools/sanitize.sh build            # build only
#   tools/sanitize.sh -- <args...>     # build then run thirdeye with <args>
#                                      # (any flag works, e.g. --skip-intro)
#
# Output goes to build-asan/ to keep it separate from the normal build/.
# Override with SAN_BUILD_DIR=...
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${SAN_BUILD_DIR:-build-asan}

# -fno-omit-frame-pointer keeps backtraces readable; the two sanitizers stack
# cleanly. -fsanitize=address,undefined picks up both at once.
SAN_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g"

# Helpful runtime knobs; users can override by exporting before invoking.
# detect_leaks is intentionally off: LSan isn't on macOS-clang's ASan -- use
# tools/leaks.sh instead. On Linux, set detect_leaks=1 to enable.
: "${ASAN_OPTIONS:=abort_on_error=1:strict_string_checks=1:check_initialization_order=1:detect_stack_use_after_return=1}"
: "${UBSAN_OPTIONS:=print_stacktrace=1:halt_on_error=1}"
export ASAN_OPTIONS UBSAN_OPTIONS

# On macOS the sdl2-compat shim dlopens libSDL3.dylib at runtime, and Homebrew's
# /opt/homebrew/lib isn't in dyld's default fallback search list. Without this
# the test binary aborts with "Failed loading SDL3 library" -- which looks like
# a sanitizer find but isn't.
if [ "$(uname -s)" = "Darwin" ] && [ -d /opt/homebrew/lib ]; then
  export DYLD_FALLBACK_LIBRARY_PATH=${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}
fi

if [ ! -f "$BUILD_DIR/build.ninja" ] && [ ! -f "$BUILD_DIR/Makefile" ]; then
  cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_C_FLAGS="$SAN_FLAGS" \
    -DCMAKE_CXX_FLAGS="$SAN_FLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$SAN_FLAGS"
fi

cmake --build "$BUILD_DIR"

case "${1:-test}" in
  build)
    ;;
  --)
    shift
    # macOS APPLE_BUNDLE_ENABLED puts the binary inside the .app; the
    # non-bundle config (and Linux) drops it in the build root.
    exe="$BUILD_DIR/thirdeye.app/Contents/MacOS/thirdeye"
    [ -x "$exe" ] || exe="$BUILD_DIR/thirdeye"
    [ -x "$exe" ] || { echo "thirdeye binary not found under $BUILD_DIR" >&2; exit 1; }
    exec "$exe" "$@"
    ;;
  test|"")
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    ;;
  *)
    echo "usage: $0 [build|test|-- <thirdeye args>]" >&2
    exit 2
    ;;
esac
