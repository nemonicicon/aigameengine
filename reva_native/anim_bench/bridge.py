#!/usr/bin/env python3
"""bridge.py -- talk to a running engine (engine/bridge.inc). Start the engine with REVA_BRIDGE=7766, then:

    bridge.py discover                 what it can do
    bridge.py get_state                tick, mode, game, flips, late, the hero
    bridge.py step 60                  hold the clock, run 60 ticks, come back when they're done
    bridge.py run | hold               let the clock go, or stop it
    bridge.py press a 10               hold a button (a b xb yb start) for 10 ticks
    bridge.py stick 1 0 30             push the stick right for 30 ticks (x and y, -1..1, up and right positive)
    bridge.py --pad 1 press a 10       the same on another player's pad, 0 to 3
    bridge.py screenshot out.ppm       the next frame
    bridge.py hash | rng_state         the sim's hash, the dice
    bridge.py get hero_x | set hero_x 1.5
    bridge.py quit

--port picks another port. The reply is one line of JSON, printed as it came, and the exit code says whether it was ok."""
import json
import socket
import sys


def main():
    a = sys.argv[1:]
    port = 7766
    if "--port" in a:
        i = a.index("--port")
        port = int(a[i + 1])
        del a[i:i + 2]
    pad = None
    if "--pad" in a:
        i = a.index("--pad")
        pad = int(a[i + 1])
        del a[i:i + 2]
    if not a or a[0] in ("-h", "--help"):
        print(__doc__)
        return 0
    c = a[0]
    if c == "press":
        msg = {"cmd": "input", a[1]: 1, "n": int(a[2]) if len(a) > 2 else 2}
    elif c == "stick":
        msg = {"cmd": "input", "x": float(a[1]), "y": float(a[2]), "n": int(a[3]) if len(a) > 3 else 1}
    elif c == "step":
        msg = {"cmd": "step", "n": int(a[1])}
    elif c == "screenshot":
        msg = {"cmd": "screenshot", "path": a[1]}
    elif c == "get":
        msg = {"cmd": "get", "name": a[1]}
    elif c == "set":
        msg = {"cmd": "set", "name": a[1], "v": float(a[2])}
    else:
        msg = {"cmd": c}
    if pad is not None:
        msg["pad"] = pad
    with socket.create_connection(("127.0.0.1", port), timeout=120) as s:
        s.sendall((json.dumps(msg) + "\n").encode())
        got = b""
        while b"\n" not in got:
            d = s.recv(65536)
            if not d:
                break
            got += d
    line = got.decode("utf-8", "replace").strip()
    print(line)
    try:
        return 0 if json.loads(line).get("ok") else 1
    except ValueError:
        return 1


if __name__ == "__main__":
    sys.exit(main())
