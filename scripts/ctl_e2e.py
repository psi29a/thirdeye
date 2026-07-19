#!/usr/bin/env python3
"""Control-channel e2e (docs/control_channel.md Testing): boot with
THIRDEYE_CTL + --load-save, script ping -> party -> key -> party, assert the
pose changed. Registered with ctest; exits 77 (skip) when game data is absent.

Usage: ctl_e2e.py <thirdeye-binary> [data-dir]
Data dir defaults to $THIRDEYE_TEST_DATA_DIR.
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

SKIP = 77


def main():
    if len(sys.argv) < 2:
        print("usage: ctl_e2e.py <thirdeye-binary> [data-dir]")
        return 2
    binary = sys.argv[1]
    data = sys.argv[2] if len(sys.argv) > 2 else os.environ.get(
        "THIRDEYE_TEST_DATA_DIR", "")
    res = os.path.join(data, "EYE.RES") if data else ""
    if not res or not os.path.exists(res):
        print("skip: no EYE.RES (set THIRDEYE_TEST_DATA_DIR)")
        return SKIP
    if not os.path.exists(os.path.join(data, "SAVEGAME", "ITEMS_01.BIN")):
        print("skip: no save slot 1")
        return SKIP

    sock_path = os.path.join(tempfile.mkdtemp(prefix="te_ctl_"), "ctl.sock")
    env = dict(os.environ, THIRDEYE_CTL=sock_path, THIRDEYE_MUTE="1",
               SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    proc = subprocess.Popen(
        [binary, res, "--skip-intro", "--skip-menu", "--load-save", "1",
         "--nosound", "--scale=1"],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def talk(cmd, timeout=10.0):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect(sock_path)
        s.sendall(cmd.encode() + b"\n")
        buf = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            buf += chunk
            lines = buf.decode(errors="replace").splitlines()
            if lines and (lines[-1] == "ok" or lines[-1].startswith("err")):
                break
        s.close()
        return buf.decode(errors="replace")

    def pose(reply):
        for ln in reply.splitlines():
            if ln.startswith("pose"):
                return ln
        return None

    try:
        deadline = time.time() + 30
        while not os.path.exists(sock_path):
            if time.time() > deadline:
                print("FAIL: control socket never appeared")
                return 1
            time.sleep(0.2)
        time.sleep(5)  # let the engine settle into gameplay

        assert talk("ping").strip() == "ok", "ping failed"
        p1 = pose(talk("party"))
        assert p1, "no pose in party reply (not in game?)"
        talk("key 4900")  # turn right -- always succeeds, walls don't block it
        time.sleep(2)
        p2 = pose(talk("party"))
        assert p2, "no pose in second party reply"
        print(f"before: {p1}\nafter:  {p2}")
        if p1 == p2:
            print("FAIL: pose unchanged after turn")
            return 1
        print("OK")
        return 0
    finally:
        proc.send_signal(2)
        try:
            proc.wait(5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
