#!/bin/sh
# Build thirdeye in an ubuntu:26.04 container and run it under valgrind
# headless (SDL dummy drivers). Writes the full valgrind report to
# valgrind.log and game stdout to thirdeye.log.
#
# Usage:
#   scripts/ci-valgrind.sh                    # SAMPLE.RES (in-tree)
#   RES=../data/EYE.RES scripts/ci-valgrind.sh --skip-menu
#   RUN_SECS=60 scripts/ci-valgrind.sh        # longer coverage
#
# RES may be a path outside the source tree (e.g. ../data/EYE.RES);
# its parent directory is bind-mounted into the container.
set -e
cd "$(dirname "$0")/.."

RES=${RES:-files/SAMPLE.RES}
RUN_SECS=${RUN_SECS:-20}

# Resolve where RES lives on the host and how the container should see it.
# If it's inside the source tree, /src already covers it; otherwise mount
# its parent dir at /data. Bind-mounts are rw so the game can write
# SAVEGAME/*.TMP -- that's a real-world code path we want under valgrind.
[ -e "$RES" ] || { echo "RES not found: $RES" >&2; exit 1; }
res_abs=$(cd "$(dirname "$RES")" && pwd)/$(basename "$RES")
src_abs=$(pwd)
case "$res_abs" in
  "$src_abs"/*) cres="/src${res_abs#$src_abs}"; extra_mount="" ;;
  *) cres="/data/$(basename "$RES")"
     extra_mount="-v $(dirname "$res_abs"):/data" ;;
esac

docker build -q -t thirdeye-valgrind - <<'EOF'
FROM ubuntu:26.04
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
    cmake ninja-build build-essential git ca-certificates valgrind \
    libsdl3-dev libopenal-dev libwildmidi-dev \
    && rm -rf /var/lib/apt/lists/*
EOF

# See ci-linux.sh: cmake configure_file()s config.hpp into the source tree.
cfgs=$(ls apps/*/config.hpp 2>/dev/null || true)
tmp=$(mktemp -d)
for f in $cfgs; do mkdir -p "$tmp/$(dirname "$f")" && cp "$f" "$tmp/$f"; done
trap 'for f in $cfgs; do cp "$tmp/$f" "$f"; done; rm -rf "$tmp"' EXIT

# ponytail: the AESOP VM runs an event loop, so it doesn't self-exit in
# headless mode -- after RUN_SECS we ask valgrind (via vgdb) for a full
# leak_check + kill. Game stdout -> thirdeye.log; valgrind report -> valgrind.log.
docker run --rm -v "$PWD":/src $extra_mount -v thirdeye-valgrind-build:/build thirdeye-valgrind sh -c '
  set -e
  cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
  cmake --build /build -- -j"$(nproc)" thirdeye &&
  cd /src &&
  rm -f valgrind.log thirdeye.log
  echo "[running against '"$cres"']"
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    valgrind \
      --leak-check=full \
      --show-leak-kinds=all \
      --track-origins=yes \
      --num-callers=32 \
      --error-limit=no \
      --vgdb=yes \
      --log-file=/src/valgrind.log \
      /build/thirdeye '"$cres"' --vm --skip-intro '"$*"' \
      >/src/thirdeye.log 2>&1 &
  vgpid=$!
  sleep '"$RUN_SECS"'
  # If thirdeye already exited (SAMPLE.RES now quits cleanly), skip vgdb.
  if kill -0 $vgpid 2>/dev/null; then
    for _ in $(seq 1 30); do
      ls /tmp/vgdb-pipe-*-from-vgdb-to-*-by-*-on-* >/dev/null 2>&1 && break
      sleep 0.5
    done
    # vgdb monitor output goes to valgrind stdio, not --log-file, so tee it.
    { vgdb --pid=$vgpid leak_check full reachable any
      vgdb --pid=$vgpid v.info all_errors
      vgdb --pid=$vgpid v.kill
    } >>/src/valgrind.log 2>&1 || true
  fi
  wait $vgpid 2>/dev/null || true
  echo "=== valgrind summary ==="
  tail -n 80 /src/valgrind.log
'

echo
echo "Full report: valgrind.log (game stdout: thirdeye.log)"
