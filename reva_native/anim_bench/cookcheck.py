#!/usr/bin/env python3
"""cookcheck.py -- a game's manifest, its budgets, its cook (engine plan, Phase 2).

    cookcheck.py rvz                    every file the game lists exists and is within its budget, or this fails
    cookcheck.py rvz --cook             first, cook whatever is missing or older than its source (skate_src/author/cook_game.py)
    cookcheck.py rvz --recook           first, cook everything again, as after changing a cook line
    cookcheck.py rvz --stage            the game's own folder, games/rvz/stage: exactly its files, hard links, no disk used
    cookcheck.py rvz --verify           the proof, after --stage: boot the no-display engine from that folder, and from the repo with the film bodies.
                                        Same hashes, so the cook changed nothing the sim can feel, and what it read is exactly what's listed

11-02: a game loads only what it needs, and budgets hold the frame. games/<game>/game.txt is the list. Run from reva_native.
"""
import argparse, fnmatch, glob, hashlib, io, os, re, shutil, struct, subprocess, sys
from PIL import Image
sys.stdout.reconfigure(encoding="utf-8")
HERE = os.path.dirname(os.path.abspath(__file__))
COOK = os.path.join(HERE, "..", "..", "skate_src", "author", "cook_game.py")


def read_pack(path):
    """the numbers a budget is written in, off the file"""
    b = open(path, "rb").read()
    if b[:4] != b"RPK1":
        return None
    ver, nN, nM, nMat, nC, nT, offN, offM, offMat, offC, offTex, offStr, offSize, total, f14 = struct.unpack_from("<15I", b, 4)
    tris = bones = 0
    for m in range(nM):
        name, node, verts, idx, bc = struct.unpack_from("<5I", b, offM + m * 60)
        tris += idx // 3 if idx else verts // 3
        bones = max(bones, bc)
    tex = 0
    texbytes = 0
    hashes = {}
    for t in range(nT):
        off, ln = struct.unpack_from("<II", b, offTex + t * 16 + 8)
        h = hashlib.sha1(b[off:off + ln]).hexdigest()
        try:
            w, ht = Image.open(io.BytesIO(b[off:off + ln])).size
        except Exception:
            continue
        tex = max(tex, w, ht)
        hashes[h] = w * ht * 4 * 4 // 3
        texbytes += hashes[h]
    if b[-4:] == b"RPK2":   # compressed textures: what the GPU holds is what's in the file
        dirOff, nDir = struct.unpack_from("<II", b, len(b) - 16)
        for d in range(nDir):
            tag, off, size, crc = struct.unpack_from("<4sIII", b, dirOff + d * 16)
            if tag != b"TEXC":
                continue
            for k in range(struct.unpack_from("<I", b, off)[0]):
                slot, fmt, w, ht, levels, flags, o2, sz = struct.unpack_from("<8I", b, off + 4 + k * 32)
                tex = max(tex, w, ht)
                hashes[hashlib.sha1(b[o2:o2 + sz]).hexdigest()] = sz
                texbytes += sz
    return dict(tris=tris, meshes=nM, tex=tex, bones=bones, mb=len(b) / 1e6, clips=nC, gpu=texbytes / 1e6, hashes=hashes)


def parse(game):
    budgets, items, cooks = {}, [], []
    for ln in open(os.path.join("games", game, "game.txt"), encoding="utf-8"):
        ln = ln.split("#")[0].split()
        if not ln:
            continue
        if ln[0] == "budget":
            budgets[ln[1]] = {ln[i]: float(ln[i + 1]) for i in range(2, len(ln), 2)}
        elif ln[0] == "cook":
            src = ln[3] if len(ln) > 3 and ln[2] == "from" else None
            cooks.append((ln[1], src, ln[4:] if src else ln[2:]))
        else:
            items.append((ln[0], ln[1]))
    return budgets, items, cooks


class Game:
    def __init__(self, name):
        self.name = name
        self.cooked = os.path.join("games", name, "cooked")
        self.stage = os.path.join("games", name, "stage")
        self.budgets, self.items, self.cooks = parse(name)

    def real(self, p):
        """a listed path, as it is on this disk: the cooked one wins"""
        c = os.path.join(self.cooked, p)
        return c if os.path.exists(c) else p

    def files(self, complain):
        out = []
        for kind, pat in self.items:
            got = sorted({os.path.relpath(p, self.cooked).replace("\\", "/") for p in glob.glob(os.path.join(self.cooked, pat))} | {p.replace("\\", "/") for p in glob.glob(pat)})
            if not got:
                complain("MISSING: %s %s" % (kind, pat))
            out += [(kind, p) for p in got]
        return out

    def recipe(self, p):
        for pat, src, args in self.cooks:
            if fnmatch.fnmatch(p, pat):
                return src or p, args
        return None

    def cook(self, again=False):
        self.again = again
        n = 0
        for kind, p in self.files(lambda s: None) or []:
            r = self.recipe(p)
            if r:
                n += self.cook_one(p, r)
        for pat, src, args in self.cooks:   # a cooked file nothing has made yet can't be globbed
            if src and not os.path.exists(os.path.join(self.cooked, pat)):
                n += self.cook_one(pat, (src, args))
        print("cooked %d" % n)

    def cook_one(self, p, r):
        src, args = r
        dst = os.path.join(self.cooked, p)
        if not os.path.exists(src):
            print("⛔ no source for %s: %s" % (p, src))
            return 0
        if os.path.exists(dst) and os.path.getmtime(dst) >= os.path.getmtime(src) and not self.again:
            return 0
        print("cook %s" % p, flush=True)
        rc = subprocess.run([sys.executable, COOK, src, dst] + args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        print("  " + (rc.stdout.strip().splitlines() or ["?"])[-1])
        if rc.returncode:
            raise SystemExit(rc.stdout)
        return 1


def check(g):
    bad = []
    files = g.files(lambda s: bad.append(s))
    seen = {}
    for kind, p in files:
        if p in seen:
            bad.append("LISTED TWICE: %s" % p)
        seen[p] = kind
    tot = {}
    uniq = {}
    for kind, p in files:
        t = tot.setdefault(kind, dict(n=0, tris=0, mb=0.0, gpu=0.0))
        rp = g.real(p)
        t["n"] += 1
        t["mb"] += os.path.getsize(rp) / 1e6
        if not p.endswith(".rpack"):
            continue
        r = read_pack(rp)
        if r is None:
            bad.append("NOT A PACK: %s" % p)
            continue
        t["tris"] += r["tris"]
        t["gpu"] += r["gpu"]
        uniq.update(r["hashes"])
        B = g.budgets.get(kind)
        if B is None:
            bad.append("NO BUDGET for kind %r: %s" % (kind, p))
            continue
        for key, lim in B.items():
            if r[key] > lim:
                bad.append("OVER BUDGET: %s %s = %g, its %s budget is %g" % (p, key, r[key], kind, lim))
    print("%-6s %5s %10s %9s %9s" % ("kind", "files", "triangles", "disk MB", "image MB"))
    for k, t in tot.items():
        print("%-6s %5d %10d %9.1f %9.1f" % (k, t["n"], t["tris"], t["mb"], t["gpu"]))
    print("%-6s %5d %10d %9.1f %9.1f   (image MB is what the GPU holds, an image file as RGBA with mips; %.1f MB once each image is loaded only once)" % ("total", sum(t["n"] for t in tot.values()), sum(t["tris"] for t in tot.values()), sum(t["mb"] for t in tot.values()), sum(t["gpu"] for t in tot.values()), sum(uniq.values()) / 1e6))
    return files, bad


def stage(g, files):
    if os.path.isdir(g.stage):
        shutil.rmtree(g.stage)
    for kind, p in files:
        d = os.path.join(g.stage, p)
        os.makedirs(os.path.dirname(d) or ".", exist_ok=True)
        os.link(g.real(p), d)
    os.makedirs(os.path.join(g.stage, "save"), exist_ok=True)   # where the game writes, the only thing here that isn't listed
    print("staged %d files in %s" % (len(files), g.stage))


def hashes(exe, cwd, hero, ticks, openlog=None):
    env = dict(os.environ, REVA_MODE="9", REVA_AUTOVAR="1", REVA_TIER="pi", REVA_FIXEDSTEP="60", REVA_SEED="12345", REVA_SIMHASH="60", REVA_EXITTICK=str(ticks), REVA_NOHOLLY="1")
    if os.name == "nt":
        env["PATH"] = r"C:\msys64\mingw64\bin;" + env["PATH"]
    if openlog:
        env["REVA_OPENLOG"] = openlog
    r = subprocess.run([exe, "100", "packs_rvz", hero], cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", timeout=900)
    return [l for l in r.stdout.splitlines() if l.startswith("[simhash]")], r.stdout


def verify(g, listed):
    """the film game from the repo against the cooked game from its own folder"""
    exe = os.path.abspath("crowd_headless.exe" if os.name == "nt" else "crowd_headless")
    hero = next(p for k, p in listed if k == "hero")
    film = g.recipe(hero)[0]
    log = os.path.abspath(os.path.join("games", g.name, "open.txt"))
    a, _ = hashes(exe, ".", film, 3600)
    b, out = hashes(exe, g.stage, hero, 3600, log)
    bad = []
    if not a or len(a) != len(b):
        bad.append("the runs didn't finish alike: %d and %d hash lines" % (len(a), len(b)))
        print(out[-3000:])
    for x, y in zip(a, b):
        if x != y:
            bad.append("THE COOK REACHED THE SIM: film %s | cooked %s" % (x, y))
            break
    if not bad:
        print("same hashes, %d checkpoints to %s" % (len(a), a[-1].split()[2]))
    read = [l.strip().replace("\\", "/") for l in open(log, encoding="utf-8", errors="replace") if l.strip()]
    names = {p for k, p in listed}
    for p in read:
        if p not in names:
            bad.append(("READ FROM OUTSIDE THE GAME: %s" if ":" in p or p.startswith("/") else "READ BUT NOT LISTED: %s") % p)
    for p in sorted(names - set(read) - {p for k, p in listed if k == "test"}):
        bad.append("LISTED BUT NEVER READ: %s" % p)
    if "NOT FOUND" in out or "MISSING" in out:
        for l in out.splitlines():
            if "NOT FOUND" in l or "MISSING" in l:
                bad.append("the run said: " + l[:160])
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("game")
    ap.add_argument("--cook", action="store_true")
    ap.add_argument("--recook", action="store_true")
    ap.add_argument("--stage", action="store_true")
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()
    g = Game(a.game)
    if a.cook or a.recook:
        g.cook(a.recook)
    files, bad = check(g)
    if a.stage and not bad:
        stage(g, files)
    if a.verify and not bad:
        bad += verify(g, files)
    for s in bad:
        print("⛔ " + s)
    if bad:
        print("⛔ %d problem(s)" % len(bad))
        return 1
    print("ok: %s is within its budgets" % a.game)
    return 0


if __name__ == "__main__":
    sys.exit(main())
