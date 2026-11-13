#!/usr/bin/env python3
"""netcheck.py -- two to four engines playing one game across a wire, and whether it stayed one game (queue step 29: online play).

Online play is lockstep on the tick: every machine works out the same world from the same dice and the same pads, and all that crosses the wire is pads
(club_net.c's first comment has the whole of it). So the thing to check is one thing: did every machine print the same hash of the rules, tick for tick, whatever
the wire did to the packets? This starts the no-display engine as a host and as players on this machine, each with a tape of its own for hands, on a wire made as
bad as asked, and holds every machine's [simhash] lines against the others'. Then it replays the tape the host recorded, alone and offline, and holds that against
them too: a session is an ordinary tape of four pads, so anything that goes wrong online can be played again with no wire at all.

    python anim_bench/netcheck.py                          the lot, about a minute: every case below
    python anim_bench/netcheck.py --only wire,bad          some of them
    python anim_bench/netcheck.py --exe CMD [--exe2 CMD]   other builds. --exe2 is the players' engine. Each can be a command, so an emulator can go in front:
                                                           a host built for x86-64 and a player built for the Pi, under qemu, in one session
    python anim_bench/netcheck.py --ticks 3600 --keep      a longer game, and leave the logs and tapes where they were made (it says where)
    python anim_bench/netcheck.py --pictures OUT.png       two machines on the engine's own rasterizer with no window, and each one's picture at the same ticks,
                                                           side by side (driver.py buildsoft --game rally first). scenes/everything.py --net is this

The cases:
    wire      two machines on a clean wire
    bad       two machines on a bad one: 60 ms each way, 40 ms of jitter, one packet in five lost and one in ten sent twice
    four      three machines and four pads, one machine bringing two, on a middling wire
    cadence   two machines that draw at different rates, one ticking twice a frame and more
    real      two machines on the real clock for five seconds, and how long anyone waited
    parted    THE CHECK THAT HAS TO BE ABLE TO FAIL: one machine plays one tick with a pad nobody was sent. Both must notice, and say the tick
    gone      a player's machine dies without a word. The host waits, then plays on with that pad unplugged from a tick it names, and its tape still replays
    save      the host and the player have different saves. Both must play the host's; replayed with the player's own save the game is a different one
    stranger  a third party throws broken packets at the host all game; then a third player that joins properly and talks nonsense. The others' game mustn't notice
It exits 1 if any case failed. The engine must be built first: driver.py check --game rally builds crowd_rally_h.exe; off Windows, build_pi.sh headless with GAME=games/rally/rally.c.
"""
import argparse
import math
import os
import random
import shlex
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# ⛔ (11-13) The stand-in player below speaks the wire by hand, and it said protocol 1 with 17-byte seats until the engine had moved to 4 and 26 (11-12): the host
# turned it away, the third pad never came, and that case's session never began. Both numbers are read from the engine's own file now. What a seat has gained is
# at its end and zero means nothing held, so the old seat padded out is a seat.
_wire = open(os.path.join(ROOT, "club_net.c"), encoding="utf-8", errors="replace").read()
PROTO = int(_wire.split("#define NET_PROTO", 1)[1].split()[0])
SEAT = int(_wire.split("#define SEAT_BYTES", 1)[1].split()[0])
ARGS = {"rally": ["0", "packs"], "rocks": ["0", "packs"], "sample": ["12", "packs", "packs/v1_sample.rpack"], "tower": ["10", "packs", "packs/v1_tower.rpack"]}
BITS = lambda v: "%08x" % struct.unpack("<I", struct.pack("<f", v))[0]
ZERO = BITS(0.0)


def hands(path, seed, ticks, pads=1):
    """a tape of a player's own hands: pad 0 (and pads 1.. when this machine brings more than one), wandering and pressing, from a seed. A press of A early on starts the game"""
    rng = random.Random(seed)
    ph = [rng.uniform(0.0, 6.28) for _ in range(8)]
    rate = [rng.uniform(0.02, 0.06) for _ in range(8)]
    with open(path, "w", newline="\n") as f:
        f.write("# reva input tape v3  seed 12345  written by netcheck.py  (tick lx ly rx ry rt A B X Y LB RB St Bk edge mdx mdy manual kb fpv, then pads 1 to 3: on lx ly rx ry rt held; floats as bits)\n")
        for t in range(1, ticks + 400):
            lx, ly = 0.9 * math.sin(t * rate[0] + ph[0]), 0.9 * math.cos(t * rate[1] + ph[1])
            a = 1 if t % 300 in (40 + seed % 7, 41 + seed % 7) else 0
            line = f"{t} {BITS(lx)} {BITS(ly)} {ZERO} {ZERO} {ZERO} {a} 0 0 0 0 0 0 0 0 {ZERO} {ZERO} 1 1 0"
            for k in (1, 2, 3):
                on = k < pads
                x = 0.9 * math.sin(t * rate[2 * k] + ph[2 * k]) if on else 0.0
                y = 0.9 * math.cos(t * rate[2 * k + 1] + ph[2 * k + 1]) if on else 0.0
                line += f" {int(on)} {BITS(x)} {BITS(y)} {ZERO} {ZERO} {ZERO} 0"
            f.write(line + "\n")


def folder(work, name, game, save=None):
    """a machine's own copy of the game's folder, so each can have its own saves"""
    d = os.path.join(work, name)
    shutil.rmtree(d, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "games", game), d, ignore=shutil.ignore_patterns("*.c", "*.py", "*.bat", "*.sh", "stage", "cooked", "save"))
    if save is not None:
        os.makedirs(os.path.join(d, "save"), exist_ok=True)
        for fn, text in save.items():
            open(os.path.join(d, "save", fn), "w", newline="\n").write(text)
    return d


class Machine:
    def __init__(self, name, cmd, cwd, env):
        self.name, self.cmd, self.cwd, self.env = name, cmd, cwd, env
        self.out = ""
        self.rc = None

    def start(self):
        self.log = open(os.path.join(self.cwd, "_net_%s.log" % self.name), "wb")
        self.p = subprocess.Popen(self.cmd, cwd=self.cwd, env=self.env, stdout=self.log, stderr=subprocess.STDOUT)

    def wait(self, timeout):
        try:
            self.rc = self.p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self.p.kill()
            self.rc = -999
        self.log.close()
        self.out = open(self.log.name, "rb").read().decode("utf-8", "replace")

    def hashes(self):
        """tick -> the exact hash of the rules"""
        h = {}
        for l in self.out.splitlines():
            if l.startswith("[simhash] tick "):
                w = l.split()
                h[int(w[2])] = w[4]
        return h

    def net(self):
        return [l for l in self.out.splitlines() if l.startswith("[net]")]


def session(a, work, tag, n=2, ticks=None, wire=None, extra=None, local=None, saves=None, real=False, game=None, dead=None, timeout=240.0, hostile=None, pads=None):
    """one game across n machines. extra: env for each machine; local: how many pads each brings; saves: each machine's save files; dead: {machine: the tick it dies at, without a word}"""
    game = game or a.game
    ticks = ticks or a.ticks
    port = random.randint(20000, 60000)
    local = local or [1] * n
    pads = pads or sum(local)
    ms = []
    for i in range(n):
        d = folder(work, "%s_m%d" % (tag, i), game, None if saves is None else saves[i])
        tape = os.path.join(d, "_hands.tape")
        hands(tape, 1000 + 17 * i + len(tag), ticks, local[i])
        env = dict(os.environ, REVA_MODE="9", REVA_TIER="pi", REVA_SIMHASH="1", REVA_EXITTICK=str(ticks if not dead or i not in dead else dead[i]), REVA_REPLAY=tape, REVA_RECORD=os.path.join(d, "_session.tape"),
                   REVA_NETLOCAL=str(local[i]), REVA_NET=("host:%d:%d" % (port, pads)) if i == 0 else ("join:127.0.0.1:%d" % port), REVA_NETSEED=str(7 + i), REVA_NETJOINWAIT="30")
        if not real:
            env["REVA_FIXEDSTEP"] = "60"
        if saves is None:
            env["REVA_NOSAVE"] = "1"
        for k, v in (wire or {}).items():
            env[k] = str(v)
        if dead and i in dead:
            env["REVA_NETNOBYE"] = "1"
        for k, v in ((extra or {}).get(i) or {}).items():
            env[k] = str(v)
        if os.name == "nt":
            env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
        cmd = (shlex.split(a.exe2) if (i > 0 and a.exe2) else a.cmd) + ARGS[game]
        ms.append(Machine("m%d" % i, cmd, d, env))
    ms[0].start()
    time.sleep(0.4)
    for m in ms[1:]:
        m.start()
        time.sleep(0.15)
    stop = None
    if hostile:
        stop = threading.Event()
        threading.Thread(target=hostile, args=(port, stop) + ((os.path.join(ms[0].cwd, "_net_m0.log"),) if hostile is joiner else ()), daemon=True).start()
    for m in ms:
        m.wait(timeout)
    if stop:
        stop.set()
    return ms, port


def replay(a, work, tag, tape, game=None, save=None, ticks=None):
    """the session's tape played back alone, with no wire: what every machine's hashes are held against"""
    game = game or a.game
    d = folder(work, tag, game, save)
    env = dict(os.environ, REVA_MODE="9", REVA_TIER="pi", REVA_SIMHASH="1", REVA_EXITTICK=str(ticks or a.ticks), REVA_REPLAY=tape, REVA_FIXEDSTEP="60")
    if save is None:
        env["REVA_NOSAVE"] = "1"
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    m = Machine("replay", a.cmd + ARGS[game], d, env)
    m.start()
    m.wait(240.0)
    return m


def same(x, y, upto):
    """the first tick two machines' hashes part at, or None. Both must have every tick up to `upto`"""
    for t in range(1, upto + 1):
        if t not in x or t not in y:
            return "tick %d is missing from one of them" % t
        if x[t] != y[t]:
            return "tick %d" % t
    return None


def stat(m):
    s = [l for l in m.net() if "held for the wire" in l and "packets out" in l]
    return s[-1][6:] if s else "no closing line"


def joiner(port, stop, hostlog):
    """a player that joins properly, plays a few seconds, and talks nonsense the whole time: every kind of packet, from an address the host has let in, with bodies
    that are wrong in every way a body can be. Then it stops answering. The host must come to no harm, and the other player's game mustn't change by a bit"""
    rng = random.Random(4)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.002)
    to = ("127.0.0.1", port)
    M = 0x314E5652
    nonce = 0x1234567
    me = session = None
    relseq = 0
    began = go = False
    delay = 2
    sent_ready = False
    t0 = time.time()
    tick = 0
    last_tick_at = 0.0
    mine = {}       # this player's pads, by tick
    host_has = 0    # the last tick of them the host says it holds

    def pk(typ, body, sess=None, frm=None):
        return struct.pack("<IIBBH", M, session if sess is None else sess, typ, (me if me is not None else 255) if frm is None else frm, len(body)) + body

    def noise(n):
        return bytes(rng.randrange(256) for _ in range(n))

    while not stop.is_set() and time.time() - t0 < 60.0:
        if me is None:
            s.sendto(struct.pack("<IIBBH", M, 0, 1, 255, 36) + struct.pack("<HBBII", PROTO, 1, 0, 2166136261, nonce) + b"RALLY".ljust(24, b"\0"), to)
        try:
            d, _ = s.recvfrom(2000)
        except OSError:
            d = b""
        if len(d) >= 12:
            magic, sess, typ, frm, ln = struct.unpack("<IIBBH", d[:12])
            body = d[12:]
            if typ == 2 and me is None and len(body) >= 8:      # WELCOME
                me, session = body[4], sess
            elif typ == 5 and len(body) >= 3:                   # REL: say it's here, as a real player would
                seq, kind = struct.unpack("<HB", body[:3])
                s.sendto(pk(6, struct.pack("<H", seq)), to)
                if kind == 1:
                    began = True
                if kind == 4 and len(body) >= 4:
                    go, delay = True, body[3]
            elif typ == 7 and len(body) >= 16 and me is not None:   # the host's pads: and how many of this player's it has
                host_has = max(host_has, struct.unpack("<4I", body[:16])[me])
            elif typ == 8 and len(body) >= 4:                   # PING
                s.sendto(pk(9, body[:4] + struct.pack("<I", int(time.time() * 1000) & 0xffffffff)), to)
        if me is None:
            continue
        if began and not sent_ready:                            # what the host's own files add up to, off its log: a player with the same game
            try:
                for l in open(hostlog, "rb").read().decode("utf-8", "replace").splitlines():
                    if "adds up to" in l:
                        w = l.split()
                        sig, n = int(w[w.index("to") + 1].rstrip(","), 16), int(w[w.index("to") + 2])
                        s.sendto(pk(5, struct.pack("<HB", relseq, 3) + struct.pack("<QIH", sig, n, 0)), to)
                        relseq += 1
                        sent_ready = True
            except (OSError, ValueError):
                pass
        if go and time.time() - last_tick_at > 0.004 and tick < 260:
            last_tick_at = time.time()
            tick += 1
            # pads a real player would send, so the game goes on, with nonsense on every side of them: every tick the host hasn't said it has, as the engine does
            mine[tick + delay] = struct.pack("<7hHB", rng.randrange(-32767, 32767), rng.randrange(-32767, 32767), 0, 0, 0, 0, 0, rng.randrange(128), 1).ljust(SEAT, b"\0")
            first = max(host_has, delay) + 1
            ts = [t for t in range(first, min(first + 30, tick + delay + 1)) if t in mine]
            head = struct.pack("<4IIQI", 0, 0, 0, 0, 0, 0, tick)
            if ts and ts == list(range(ts[0], ts[0] + len(ts))):
                s.sendto(pk(7, head + bytes([1]) + struct.pack("<BBBBI", me, 1, len(ts), 0, ts[0]) + b"".join(mine[t] for t in ts)), to)
        # and the nonsense: every kind, from this address the host has let in
        k = rng.randrange(9)
        if k == 0:
            s.sendto(pk(7, noise(rng.choice((0, 3, 44, 45, 46, 60, 200, 1100)))), to)
        elif k == 1:    # pads for ticks that are years off, or long gone, for seats it hasn't got, in blocks that say they're longer than they are
            head = struct.pack("<4IIQI", *[rng.choice((0, 5, 0x7fffffff, 0xffffffff)) for _ in range(4)], 0 if tick < 200 else rng.choice((0, 20, 200, 0xffffffff)), rng.getrandbits(64), rng.choice((0, tick, 0xffffffff)))
            blocks = b"".join(struct.pack("<BBBBI", rng.choice((0, 1, 2, 3, me, 200)), rng.choice((0, 1, 2, 4, 9)), rng.choice((0, 1, 60, 255)), 0, rng.choice((0, 1, tick, tick + 300, 0x7fffffff, 0xffffffff))) + noise(rng.choice((0, 17, 34, 400))) for _ in range(rng.randrange(1, 5)))
            s.sendto(pk(7, head + bytes([rng.choice((1, 2, 4, 5, 255))]) + blocks), to)
        elif k == 2 and sent_ready:    # the in-order channel: every kind, with noise for a body. Half of them in their right place, so the host reads them through
            right = rng.random() < 0.5
            kind = rng.choice((0, 1, 2, 3, 4, 5, 7, 8) if tick < 200 else (0, 1, 2, 3, 4, 5, 6, 7, 8))   # a goodbye made of noise only once it's played a while
            s.sendto(pk(5, struct.pack("<HB", relseq if right else rng.choice((relseq + 1, relseq + 7, 0, 65535)), kind) + noise(rng.choice((0, 1, 8, 70, 600, 1000)))), to)
            relseq += right
        elif k == 3:
            s.sendto(pk(6, noise(rng.choice((0, 1, 2, 5)))), to)
        elif k == 4:
            s.sendto(pk(rng.choice((8, 9)), noise(rng.choice((0, 3, 4, 8)))), to)
        elif k == 5:    # claiming to be someone else, or another session
            s.sendto(pk(7, noise(60), frm=rng.choice((0, 1, 2, 3))), to)
            s.sendto(pk(7, noise(60), sess=rng.getrandbits(32)), to)
        elif k == 6:
            s.sendto(pk(rng.choice((1, 2, 3, 4)), noise(rng.choice((0, 8, 36, 68)))), to)
    s.close()


def junk(port, stop):
    """a stranger on the wire: broken packets at the host for as long as the game lasts"""
    rng = random.Random(99)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    to = ("127.0.0.1", port)
    magic = struct.pack("<I", 0x314E5652)
    while not stop.is_set():
        k = rng.randrange(6)
        if k == 0:
            d = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 1400)))
        elif k == 1:   # the right first four bytes, and nothing right after them
            d = magic + bytes(rng.randrange(256) for _ in range(rng.randrange(0, 60)))
        else:          # a whole header that adds up, of every kind there is, claiming to be each player, and a body of noise
            body = bytes(rng.randrange(256) for _ in range(rng.choice((0, 1, 5, 17, 45, 200, 900, 1188))))
            d = magic + struct.pack("<IBBH", rng.choice((0, 1, 0xffffffff, rng.getrandbits(32))), rng.randrange(0, 12), rng.choice((0, 1, 2, 3, 255)), len(body)) + body
        try:
            s.sendto(d, to)
        except OSError:
            pass
        time.sleep(0.002)
    s.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default="rally")
    ap.add_argument("--exe")
    ap.add_argument("--exe2", help="the players' engine, as a command: 'qemu-aarch64 /build/a64/crowd_headless'")
    ap.add_argument("--ticks", type=int, default=1200)
    ap.add_argument("--only")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--pictures", help="OUT.png: two machines on the engine's own rasterizer, with no window, each one's picture at the same three ticks")
    ap.add_argument("--soft", help="the build on the engine's own rasterizer, for --pictures")
    a = ap.parse_args()
    a.exe = a.exe or os.path.join(ROOT, "crowd_%s_h.exe" % a.game if os.name == "nt" else "crowd_headless")
    a.cmd = shlex.split(a.exe) if (" " in a.exe and not os.path.exists(a.exe)) else [os.path.abspath(a.exe)]   # a command, so an emulator can go in front of the host too
    if not os.path.exists(a.cmd[-1]):
        sys.exit("no %s: build the no-display engine with this game first (driver.py check --game %s)" % (a.cmd[-1], a.game))
    work = tempfile.mkdtemp(prefix="reva_net_")
    if a.pictures:
        # two machines on the engine's own rasterizer, with no window: each one's picture at the same ticks, side by side. The world is the same in both; what
        # differs is what a game may draw differently on each machine, here which bat is lit as this machine's own
        from PIL import Image, ImageDraw, ImageFont
        try:
            font = ImageFont.truetype("DejaVuSans.ttf" if os.name != "nt" else "consola.ttf", 18)
        except OSError:
            font = ImageFont.load_default()
        soft = os.path.abspath(a.soft or os.path.join(ROOT, "crowd_%s_soft.exe" % a.game if os.name == "nt" else "crowd_soft"))
        if not os.path.exists(soft):
            sys.exit("no %s: build the game on the engine's own rasterizer first (driver.py buildsoft --game %s)" % (soft, a.game))
        a.cmd, at = [soft], (300, 600, 900)
        extra = {i: dict(REVA_NOWINDOW="1", REVA_SHOT=str(at[0]), REVA_SHOTN=str(len(at)), REVA_SHOTEVERY=str(at[1] - at[0]), REVA_SHOT_PATH=os.path.join(work, "pictures_m%d" % i, "shot.ppm")) for i in (0, 1)}
        ms, _ = session(a, work, "pictures", ticks=at[-1] + 20, extra=extra, wire=dict(REVA_NETLAG=20, REVA_NETJITTER=10))
        p = same(ms[0].hashes(), ms[1].hashes(), at[-1])
        print("pictures: two machines, 20 ms each way: %s" % ("the same hash every tick" if p is None else "\u26d4 they parted at %s" % p))
        k, tiles = 2, []
        for i, who in enumerate(("the host's screen", "the player's screen")):
            for j, t in enumerate(at):
                f = os.path.join(work, "pictures_m%d" % i, "shot_%05d.ppm" % j)
                if os.path.exists(f):
                    im = Image.open(f).convert("RGB")
                    im = im.resize((im.size[0] * k, im.size[1] * k), Image.NEAREST)
                    tile = Image.new("RGB", (im.size[0], im.size[1] + 30), (16, 16, 20))
                    tile.paste(im, (0, 0))
                    ImageDraw.Draw(tile).text((8, im.size[1] + 5), "%s, tick %d" % (who, t), fill=(235, 235, 235), font=font)
                    tiles.append((i, j, tile))
        if tiles:
            w, h = tiles[0][2].size
            sheet = Image.new("RGB", (w * len(at) + 4 * (len(at) - 1), h * 2 + 4), (0, 0, 0))
            for i, j, tile in tiles:
                sheet.paste(tile, (j * (w + 4), i * (h + 4)))
            sheet.save(a.pictures)
            print("wrote %s   <-- LOOK AT IT: the same world on both rows, and each machine's own bat the bright one" % a.pictures)
        else:
            print("\u26d4 no pictures were written: the logs are in", work)
            sys.exit(1)
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
        sys.exit(0 if p is None else 1)
    cases = a.only.split(",") if a.only else ["wire", "bad", "four", "cadence", "real", "parted", "gone", "save", "stranger"]
    bad = 0
    T = a.ticks

    def say(name, ok, what):
        nonlocal bad
        bad += not ok
        print("%-9s %s  %s" % (name, "ok " if ok else "\u26d4 ", what), flush=True)

    def all_same(ms, upto, tag, also_replay=True):
        """every machine against the host, and the host's tape replayed alone against the host"""
        h0 = ms[0].hashes()
        for m in ms[1:]:
            p = same(h0, m.hashes(), upto)
            if p:
                return False, "%s and the host parted at %s" % (m.name, p)
        if also_replay:
            r = replay(a, work, tag + "_replay", os.path.join(ms[0].cwd, "_session.tape"), ticks=upto)
            p = same(h0, r.hashes(), upto)
            if p:
                return False, "the host's tape, replayed alone, parted from the session at %s" % p
        return True, ""

    if "wire" in cases:
        ms, _ = session(a, work, "wire")
        ok, why = all_same(ms, T, "wire")
        t0 = [open(os.path.join(m.cwd, "_session.tape"), "rb").read() for m in ms]
        if ok and t0[0] != t0[1]:
            ok, why = False, "the two machines recorded different tapes"
        say("wire", ok, why or "2 machines, %d ticks, the same hash every tick, and the host's tape replays alone to the same. Host: %s" % (T, stat(ms[0])))
    if "bad" in cases:
        ms, _ = session(a, work, "bad", wire=dict(REVA_NETLAG=60, REVA_NETJITTER=40, REVA_NETLOSS=20, REVA_NETDUP=10))
        ok, why = all_same(ms, T, "bad")
        say("bad", ok, why or "60 ms each way, 40 of jitter, 1 packet in 5 lost, 1 in 10 twice: the same hash every tick. Host: %s" % stat(ms[0]))
    if "four" in cases:
        ms, _ = session(a, work, "four", n=3, local=[1, 2, 1], wire=dict(REVA_NETLAG=15, REVA_NETJITTER=10, REVA_NETLOSS=5))
        ok, why = all_same(ms, T, "four")
        say("four", ok, why or "3 machines and 4 pads, one machine bringing two, 5%% of packets lost: the same hash every tick. Host: %s" % stat(ms[0]))
    if "cadence" in cases:
        ms, _ = session(a, work, "cadence", extra={0: dict(REVA_CADENCE="1,1,1,1,2"), 1: dict(REVA_CADENCE="2,2,3", REVA_TIER="high", REVA_READING="large")}, wire=dict(REVA_NETLAG=10))
        ok, why = all_same(ms, T, "cadence")
        say("cadence", ok, why or "one machine drawing every tick or so, the other every second or third, at another rung and reading size: the same hash every tick")
    if "real" in cases:
        ms, _ = session(a, work, "real", ticks=300, real=True, wire=dict(REVA_NETLAG=25, REVA_NETJITTER=10, REVA_NETLOSS=3))
        ok, why = all_same(ms, 300, "real")
        say("real", ok, why or "the real clock, 5 seconds, 25 ms each way: the same hash every tick. Host: %s. Player: %s" % (stat(ms[0]), stat(ms[1])))
    if "parted" in cases:
        ms, _ = session(a, work, "parted", extra={1: dict(REVA_NETBREAK=400)})
        told = [[l for l in m.net() if "parted" in l and "by tick 400" in l] for m in ms]
        p = same(ms[0].hashes(), ms[1].hashes(), 399)
        ok = bool(told[0]) and bool(told[1]) and p is None
        why = "" if ok else ("the two were different before the break, at %s" % p if p else "a machine played a pad nobody was sent, and %s" % ("nobody noticed" if not told[0] and not told[1] else "only one machine noticed"))
        say("parted", ok, why or "one machine alone played tick 400 with a pad nobody was sent. Both said so: \"%s\"" % told[0][0][6:150])
    if "gone" in cases:
        ms, _ = session(a, work, "gone", dead={1: T // 3}, extra={0: dict(REVA_NETTIMEOUT="1.5")})
        gone = [l for l in ms[0].net() if "unplugged from tick" in l]
        p = same(ms[0].hashes(), ms[1].hashes(), T // 3)
        ok = bool(gone) and p is None and T in ms[0].hashes()
        why = "" if ok else ("the two parted while both were there, at %s" % p if p else "the host never said the player was gone" if not gone else "the host didn't play on to the end")
        if ok:
            r = replay(a, work, "gone_replay", os.path.join(ms[0].cwd, "_session.tape"))
            p = same(ms[0].hashes(), r.hashes(), T)
            ok, why = p is None, ("the host's tape, replayed alone, parted from what the host played at %s" % p) if p else ""
        say("gone", ok, why or "the player's machine died at tick %d without a word. The host: \"%s\", played on to tick %d, and its tape replays alone to the same" % (T // 3, gone[0][6:110], T))
    if "save" in cases:
        hs, ps = {"rally_best.txt": "version 1\nrally=37\n"}, {"rally_best.txt": "version 1\nrally=5\n"}
        ms, _ = session(a, work, "save", saves=[hs, ps], game="rally")
        p = same(ms[0].hashes(), ms[1].hashes(), T)
        tape = os.path.join(ms[0].cwd, "_session.tape")
        rh, rp = replay(a, work, "save_host", tape, game="rally", save=hs), replay(a, work, "save_player", tape, game="rally", save=ps)
        ph, pp = same(ms[0].hashes(), rh.hashes(), T), same(ms[0].hashes(), rp.hashes(), T)
        mine = open(os.path.join(ms[1].cwd, "save", "rally_best.txt")).read()
        ok = p is None and ph is None and pp is not None and "rally=5" in mine
        why = "" if ok else ("the two machines parted at %s" % p if p else "replayed with the host's save it parted at %s" % ph if ph else "the player's own save was written over" if "rally=5" not in mine
                             else "replayed with the PLAYER'S save it was the same game, so this check can't tell whose save was read")
        say("save", ok, why or "the host's save says 37 and the player's 5. Both played the host's: the same hash every tick, the same as the tape replayed with the host's save, and not the same as with the player's (it parts at %s). The player's own file wasn't touched" % pp)
    if "stranger" in cases:
        ms, port = session(a, work, "stranger", hostile=junk, wire=dict(REVA_NETLAG=5))
        ok, why = all_same(ms, T, "stranger")
        say("stranger", ok, why or "broken packets thrown at the host all game: the same hash every tick. Host: %s" % stat(ms[0]))
        # and one the host lets in: three pads, two machines of ours and a third player that's a script talking nonsense, which then goes quiet
        ms, port = session(a, work, "joiner", pads=3, hostile=joiner, extra={0: dict(REVA_NETSAY="1", REVA_NETTIMEOUT="1.5")}, wire=dict(REVA_NETLAG=3))
        ok, why = all_same(ms, T, "joiner")
        gone = [l for l in ms[0].net() if "player 3" in l and ("unplugged from tick" in l)]
        if ok and not gone:
            ok, why = False, "the host never let go of the player that stopped making sense"
        if ok and int(gone[0].split("unplugged from tick")[1].split()[0]) < 100:
            ok, why = False, "the third player was gone by tick %s: it never really played, so this proved nothing about its pads" % gone[0].split("unplugged from tick")[1].split()[0]
        say("joiner", ok, why or "a third player that joined and talked nonsense, then went quiet: the two real machines had the same hash every tick, and the host said \"%s\"" % gone[0][6:120])
    if a.keep or bad:
        print("the logs and tapes are in", work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    print("all the same" if not bad else "\u26d4 %d case(s) failed" % bad)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
