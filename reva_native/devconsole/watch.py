#!/usr/bin/env python3
"""watch.py -- a plain line a second, for any terminal: the running game if it has a bridge, memory, temperature, and whether the Pi is throttling. Nothing but print, so it works over SSH and costs next to nothing.

usage: watch.py [port]    (default 7766)"""
import json
import socket
import subprocess
import sys
import time


def engine(port):
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.3) as s:
            s.sendall(b'{"cmd":"get_state"}\n')
            s.settimeout(0.5)
            d = json.loads(s.makefile().readline())
        return "tick %d, %d late of %d" % (d["tick"], d["late"], d["flips"])
    except (OSError, ValueError, KeyError):
        return "no game on the bridge"


def mem():
    for ln in open("/proc/meminfo"):
        if ln.startswith("MemAvailable"):
            return "%.2f GB free" % (int(ln.split()[1]) / 1048576)
    return "?"


def vc(what):
    try:
        return subprocess.run(["vcgencmd"] + what, capture_output=True, text=True, timeout=1).stdout.strip().split("=")[-1]
    except (OSError, subprocess.SubprocessError):
        return "?"


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 7766
    while True:
        print("%s | %s | %s | %s | throttled %s" % (time.strftime("%H:%M:%S"), engine(port), mem(), vc(["measure_temp"]), vc(["get_throttled"])), flush=True)
        time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
