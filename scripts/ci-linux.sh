#!/bin/sh
# Mirror the ubuntu-26.04 GCC CI job locally (catches GCC-only breaks like a
# missing <cstring> that Apple Clang forgives via transitive includes).
# Usage: scripts/ci-linux.sh   -- first run builds the deps image (~2 min),
# later runs reuse it and the named build volume, so rebuilds are incremental.
set -e
cd "$(dirname "$0")/.."

docker build -q -t thirdeye-ci - <<'EOF'
FROM ubuntu:26.04
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    cmake ninja-build build-essential git ca-certificates \
    libsdl3-dev libopenal-dev libwildmidi-dev \
    && rm -rf /var/lib/apt/lists/*
EOF

# ponytail: native arch (arm64 on Apple Silicon), not CI's amd64 -- GCC header/
# warning breakage is arch-independent; add --platform linux/amd64 if it isn't.
# Source is mounted rw: cmake configure_file()s config.hpp into the SOURCE tree,
# so the container writes container paths (/src) into them -- snapshot the
# host-generated ones and restore on exit, or a later host build (or commit!)
# picks up /src paths.
cfgs=$(ls apps/*/config.hpp 2>/dev/null)
tmp=$(mktemp -d)
for f in $cfgs; do mkdir -p "$tmp/$(dirname "$f")" && cp "$f" "$tmp/$f"; done
trap 'for f in $cfgs; do cp "$tmp/$f" "$f"; done; rm -rf "$tmp"' EXIT

docker run --rm -v "$PWD":/src -v thirdeye-ci-build:/build thirdeye-ci sh -c '
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release &&
  cmake --build /build -- -j"$(nproc)" &&
  ctest --test-dir /build --output-on-failure'
