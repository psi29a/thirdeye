#!/bin/sh
# Run thirdeye under valgrind headless (SDL dummy drivers) and report/gate on
# memcheck errors. Two modes:
#   default   -- build + run in an ubuntu:26.04 container (reproducible CI env)
#   NATIVE=1  -- use the local valgrind + an existing build (no docker); for
#                the Linux dev box where valgrind is right there
# Writes the full valgrind report to valgrind.log and game stdout to
# thirdeye.log (repo root in both modes).
#
# Usage:
#   scripts/ci-valgrind.sh                    # SAMPLE.RES (in-tree), docker
#   RES=../data/EYE.RES scripts/ci-valgrind.sh --skip-menu
#   RUN_SECS=60 scripts/ci-valgrind.sh        # longer coverage
#   RES=../data/EYE.RES WALK=scripts/walks/eob3-qsp-l1-to-l2.walk \
#     scripts/ci-valgrind.sh                  # replay a recorded session
#   NATIVE=1 BIN=build/thirdeye RES=../data/EYE.RES \
#     WALK=scripts/walks/eob3-qsp-l1-to-l2.walk scripts/ci-valgrind.sh
#
# RES may be a path outside the source tree (e.g. ../data/EYE.RES); in docker
# mode its parent directory is bind-mounted into the container.
#
# WALK is a THIRDEYE_AUTOWALK sequence -- either a literal token string or a
# path to a scripts/walks/*.walk file (# comment lines stripped). Recorded
# walks come from playing a session with THIRDEYE_RECORD=1. With WALK set,
# the game drives itself and normally SELF-EXITS (the recorded session ends
# with an in-game quit), so RUN_SECS becomes a backstop timeout instead of
# the run duration -- the vgdb kill only fires if the walk drifts and never
# reaches its quit.
#
# NATIVE=1 notes: BIN (default build/thirdeye) must already be built; the
# script won't reconfigure your build tree. SDL_VIDEODRIVER/SDL_AUDIODRIVER
# default to dummy but are respected if already set (e.g. watch the walk live
# with SDL_VIDEODRIVER= on wayland).
#
# Exit status: non-zero if valgrind counted memory errors (GATE=0 disables).
set -e
cd "$(dirname "$0")/.."

RES=${RES:-files/SAMPLE.RES}

# Resolve WALK to a flat token string (no whitespace: it rides in an env var).
walk_seq=""
if [ -n "${WALK:-}" ]; then
  if [ -f "$WALK" ]; then
    walk_seq=$(grep -v '^#' "$WALK" | tr -d ' \t\r\n')
  else
    walk_seq=$(printf '%s' "$WALK" | tr -d ' \t\r\n')
  fi
  [ -n "$walk_seq" ] || { echo "WALK resolved to an empty sequence" >&2; exit 1; }
fi
# NB: only set THIRDEYE_AUTOWALK when a walk was asked for -- a set-but-empty
# env var would still arm the autowalk harness (with its hold-forward default).
if [ -n "$walk_seq" ]; then
  RUN_SECS=${RUN_SECS:-3600}
else
  RUN_SECS=${RUN_SECS:-20}
fi

[ -e "$RES" ] || { echo "RES not found: $RES" >&2; exit 1; }

if [ "${NATIVE:-0}" = 1 ]; then
  # --- native mode: local valgrind, existing build ---------------------------
  command -v valgrind >/dev/null || { echo "valgrind not in PATH" >&2; exit 1; }
  BIN=${BIN:-build/thirdeye}
  [ -x "$BIN" ] || {
    echo "BIN not found: $BIN -- build first (cmake --build build --target thirdeye)" >&2
    exit 1
  }
  rm -f valgrind.log thirdeye.log
  echo "[native: valgrind $BIN $RES${walk_seq:+ with walk (}${WALK:-}${walk_seq:+)}]"
  # walk_seq is whitespace-free (stripped above), so the unquoted ${:+}
  # expansion stays a single env assignment.
  env ${walk_seq:+THIRDEYE_AUTOWALK=$walk_seq} \
    SDL_VIDEODRIVER="${SDL_VIDEODRIVER-dummy}" \
    SDL_AUDIODRIVER="${SDL_AUDIODRIVER-dummy}" \
    valgrind \
      --leak-check=full \
      --show-leak-kinds=all \
      --track-origins=yes \
      --num-callers=32 \
      --error-limit=no \
      --errors-for-leak-kinds=none \
      --vgdb=yes \
      --log-file=valgrind.log \
      "$BIN" "$RES" --vm --skip-intro "$@" \
      >thirdeye.log 2>&1 &
  vgpid=$!
  waited=0
  while kill -0 $vgpid 2>/dev/null && [ $waited -lt "$RUN_SECS" ]; do
    sleep 5; waited=$((waited+5))
  done
  if kill -0 $vgpid 2>/dev/null; then
    if [ -n "$walk_seq" ]; then
      echo "[walk did not self-exit within ${RUN_SECS}s -- stopping]"
    fi
    # Prefer a clean quit: SIGINT reaches the guest (SDL turns it into a quit
    # event), so the engine tears down normally and the leak report reflects
    # the real exit path. Only if that stalls, fall back to the vgdb kill.
    kill -INT $vgpid 2>/dev/null || true
    waited=0
    while kill -0 $vgpid 2>/dev/null && [ $waited -lt 60 ]; do
      sleep 2; waited=$((waited+2))
    done
  fi
  if kill -0 $vgpid 2>/dev/null; then
    for _ in $(seq 1 30); do
      ls /tmp/vgdb-pipe-*-from-vgdb-to-*-by-*-on-* >/dev/null 2>&1 && break
      sleep 0.5
    done
    # vgdb monitor output goes to valgrind stdio, not --log-file, so tee it.
    { vgdb --pid=$vgpid leak_check full reachable any
      vgdb --pid=$vgpid v.info all_errors
      vgdb --pid=$vgpid v.kill
    } >>valgrind.log 2>&1 || true
  fi
  wait $vgpid 2>/dev/null || true
  echo "=== valgrind summary ==="
  tail -n 80 valgrind.log
else
  # --- docker mode: build + run in ubuntu:26.04 -------------------------------
  # Resolve where RES lives on the host and how the container should see it.
  # If it's inside the source tree, /src already covers it; otherwise mount
  # its parent dir at /data. Bind-mounts are rw so the game can write
  # SAVEGAME/*.TMP -- that's a real-world code path we want under valgrind.
  res_abs=$(cd "$(dirname "$RES")" && pwd)/$(basename "$RES")
  src_abs=$(pwd)
  case "$res_abs" in
    "$src_abs"/*) cres="/src${res_abs#"$src_abs"}"; extra_mount="" ;;
    *) cres="/data/$(basename "$RES")"
       extra_mount="-v $(dirname "$res_abs"):/data" ;;
  esac
  extra_env=""
  [ -n "$walk_seq" ] && extra_env="-e THIRDEYE_AUTOWALK=$walk_seq"

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

  # ponytail: without a WALK the AESOP VM runs an event loop and doesn't
  # self-exit in headless mode -- after RUN_SECS we ask valgrind (via vgdb) for
  # a full leak_check + kill. With a WALK the game quits itself; the loop below
  # just waits for that (RUN_SECS as backstop). Game stdout -> thirdeye.log;
  # valgrind report -> valgrind.log.
  docker run --rm -v "$PWD":/src $extra_mount $extra_env -v thirdeye-valgrind-build:/build thirdeye-valgrind sh -c '
    set -e
    cmake -S /src -B /build -G Ninja -DCMAKE_BUILD_TYPE=Debug &&
    cmake --build /build -- -j"$(nproc)" thirdeye &&
    cd /src &&
    rm -f valgrind.log thirdeye.log
    echo "[running against '"$cres"'${THIRDEYE_AUTOWALK:+ with a walk}]"
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      valgrind \
        --leak-check=full \
        --show-leak-kinds=all \
        --track-origins=yes \
        --num-callers=32 \
        --error-limit=no \
        --errors-for-leak-kinds=none \
        --vgdb=yes \
        --log-file=/src/valgrind.log \
        /build/thirdeye '"$cres"' --vm --skip-intro "$@" \
        >/src/thirdeye.log 2>&1 &
    vgpid=$!
    waited=0
    while kill -0 $vgpid 2>/dev/null && [ $waited -lt '"$RUN_SECS"' ]; do
      sleep 5; waited=$((waited+5))
    done
    # If thirdeye already exited (a WALK quit itself / SAMPLE.RES quits
    # cleanly), valgrind wrote its own leak report -- skip the vgdb dance.
    if kill -0 $vgpid 2>/dev/null; then
      if [ -n "${THIRDEYE_AUTOWALK:-}" ]; then
        echo "[walk did not self-exit within '"$RUN_SECS"'s -- stopping]"
      fi
      # Prefer a clean quit (SIGINT -> SDL quit event -> normal teardown, so
      # the leak report reflects the real exit path); vgdb kill as fallback.
      kill -INT $vgpid 2>/dev/null || true
      waited=0
      while kill -0 $vgpid 2>/dev/null && [ $waited -lt 60 ]; do
        sleep 2; waited=$((waited+2))
      done
    fi
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
  ' sh "$@"
fi

echo
echo "Full report: valgrind.log (game stdout: thirdeye.log)"

# Gate: fail the script (CI job) if memcheck counted actual memory errors.
# Leaks intentionally don't gate yet -- the quit path still leaks by design
# (start self-destroy reset; see CLAUDE.md) -- they're reported above instead.
errs=$(sed -n 's/.*ERROR SUMMARY: \([0-9][0-9]*\) errors.*/\1/p' valgrind.log | tail -1)
lost=$(sed -n 's/.*definitely lost: \([0-9,]*\) bytes.*/\1/p' valgrind.log | tail -1)
echo "memcheck: ${errs:-?} errors, ${lost:-0} bytes definitely lost"
if [ "${GATE:-1}" = 1 ] && [ "${errs:-0}" -gt 0 ] 2>/dev/null; then
  echo "FAIL: valgrind reported memory errors" >&2
  exit 1
fi
