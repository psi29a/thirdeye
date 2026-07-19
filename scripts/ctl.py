#!/usr/bin/env python3
"""Tiny control-channel client (docs/control_channel.md). One shot:

    ctl.py /tmp/te_ctl.sock "party" "cell 10 24" "key 4900"

Each arg is one command; replies print in order. Exit 1 if any reply is err.
"""
import socket
import sys


def talk(sock_path, cmd, timeout=10.0):
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


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    bad = False
    for cmd in sys.argv[2:]:
        r = talk(sys.argv[1], cmd)
        print(f"> {cmd}\n{r}", end="")
        if r.splitlines() and r.splitlines()[-1].startswith("err"):
            bad = True
    sys.exit(1 if bad else 0)
