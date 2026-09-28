"""patch_shootfix.py -- the take must END.

⛔ THE ENGINE ONLY EXITS WHEN IT HITS ITS FRAME QUOTA. That is written down in CLAUDE.md
in my own words -- "the engine NEVER EXITS once its quota is met, so a harness must poll
and kill it" -- and the shot list still asked for MORE SECONDS THAN EACH SCENE RUNS:

    porch        43.3s of scene, 45s asked for
    denial      116.9s of scene, 119s asked for      ... all seven

Frame writing is armed by `capArm = REVA_SCENEFPS && g_scnT >= 0`, so when the scene ends
the frames stop -- at 2,682 of a requested 2,700 -- and `taken >= burst` never becomes
true. The engine sat there, and `subprocess.run(timeout=1800)` sat with it. Seven takes
would have been three and a half hours of waiting for nothing.

TWO FIXES, because either alone still leaves a way to hang:

  1. `driver.py scene` POLLS instead of blocking. It watches the frame count, and stops
     the engine when the count reaches the target OR stops growing for a few seconds --
     which also covers a scene that ends early for any other reason.
  2. `shoot.py` READS THE LENGTH out of the `.scn` header instead of me typing it. The
     scene knows how long it is; nobody should be guessing.
"""
import io, sys

CR, LF = chr(13), chr(10)
CRLF = CR + LF


def load(p):
    s = io.open(p, encoding="utf-8", newline="").read()
    was = CRLF in s
    return (s.replace(CRLF, LF) if was else s), was


def save(p, s, was):
    io.open(p, "w", encoding="utf-8", newline="").write(s.replace(LF, CRLF) if was else s)


def sub(s, a, b, why):
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    print("   ok: %s" % why)
    return s.replace(a, b)


# ------------------------------------------------------------------ driver.py
P = r"C:\MyVeryOwnVideoGames\reva\.claude\skills\run-reva\driver.py"
s, was = load(P)

s = sub(s, """    r = _run(dict(REVA_LABCTL="anim_bench/lab_ctl.txt", REVA_AUTOMODE="11",
                  REVA_ANIMSUBJ=a.subject, REVA_ANIMSTATE="0",
                  REVA_SCENEFPS=str(a.fps), REVA_FIXEDSTEP=str(a.fps),
                  REVA_SHOTN=str(frames),
                  REVA_SHOT_PATH=win(os.path.join(tmp, "f.ppm"))),
             "crowd_new.exe" if a.new else "crowd.exe", ["400", "packs", a.subject],
             timeout=1800)""",
        """    # ⛔ POLL AND KILL; DO NOT BLOCK. The engine exits only when `taken >= burst`, and
    # frame writing is armed by `REVA_SCENEFPS && g_scnT >= 0` -- so a take that asks for
    # more seconds than the scene runs stops writing at the end of the dialogue and never
    # reaches its quota. It then sits there forever, and a blocking wait sits with it.
    # Watch the frames instead: done when the count reaches the target, or when it has
    # stopped growing for STALL seconds, which covers every other way a scene can end.
    STALL = 8.0
    kill()
    exe = os.path.join(NATIVE, "crowd_new.exe" if a.new else "crowd.exe")
    env = dict(os.environ, REVA_WINDOWED="1",
               REVA_LABCTL="anim_bench/lab_ctl.txt", REVA_AUTOMODE="11",
               REVA_ANIMSUBJ=a.subject, REVA_ANIMSTATE="0",
               REVA_SCENEFPS=str(a.fps), REVA_FIXEDSTEP=str(a.fps),
               REVA_SHOTN=str(frames),
               REVA_SHOT_PATH=win(os.path.join(tmp, "f.ppm")))
    pr = subprocess.Popen([exe, "400", "packs", a.subject], cwd=NATIVE, env=env,
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    have, last_change, t0 = 0, time.time(), time.time()
    while True:
        if pr.poll() is not None:
            break
        n = len(glob.glob(os.path.join(tmp, "*.ppm")))
        now = time.time()
        if n != have:
            have, last_change = n, now
        if n >= frames:
            break
        if have > 0 and now - last_change > STALL:
            print("   frames stopped at %d/%d (the scene ended) -- wrapping the take" % (n, frames))
            break
        if now - t0 > 3600:
            print("   take exceeded an hour; stopping"); break
        time.sleep(1.0)
    time.sleep(0.7)
    kill()""",
        "scene polls and kills instead of blocking")
save(P, s, was)
print("driver.py patched")

# ------------------------------------------------------------------ shoot.py
P2 = r"C:\MyVeryOwnVideoGames\reva\skate_src\film\shoot.py"
t, was2 = load(P2)

t = sub(t, """    rows = [r for r in SHOTS if not a.only or r[0] == a.only]
    total = sum(r[7] for r in rows)""",
        """    # ⛔ THE SCENE KNOWS HOW LONG IT IS. Typing a length into the shot list is how every
    # row ended up asking for two seconds more than the dialogue runs, which is the one
    # thing that stops the engine ever reaching its frame quota. Read the .scn header.
    def scnlen(nm, fallback):
        p = os.path.join(NATIVE, "scenes", nm + ".scn")
        try:
            head = open(p).readline().split()
            return float(head[3])
        except Exception:
            return float(fallback)
    rows = [(n, act, su, pa, st, sg, hr, scnlen(n, sec), cm)
            for (n, act, su, pa, st, sg, hr, sec, cm) in SHOTS
            if not a.only or n == a.only]
    total = sum(r[7] for r in rows)""",
        "shoot reads the true length from the .scn")

t = sub(t, """        cmd = [PY, DRIVER, "scene", out, "--name", name, "--seconds", str(secs),""",
        """        cmd = [PY, DRIVER, "scene", out, "--name", name, "--seconds", "%.2f" % secs,""",
        "pass the measured length")

t = sub(t, """        print("\\n=== act %-3s %-12s %3ds  set=%-12s %s ==="
              % (act, name, secs, setname, hour))""",
        """        print("\\n=== act %-3s %-12s %5.1fs  set=%-12s %s ==="
              % (act, name, secs, setname, hour))
        sys.stdout.flush()""",
        "log the measured length, and flush")

t = sub(t, """    print("%d take(s), %d:%02d of film, %d fps  (~%d frames)"
          % (len(rows), total // 60, total % 60, a.fps, total * a.fps))""",
        """    print("%d take(s), %d:%02d of film, %d fps  (~%d frames)"
          % (len(rows), int(total) // 60, int(total) % 60, a.fps, int(total * a.fps)))
    sys.stdout.flush()""",
        "totals from the measured lengths")
save(P2, t, was2)
print("shoot.py patched")
