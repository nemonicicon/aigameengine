#!/usr/bin/env python
"""driver.py -- build, launch and DRIVE the club (reva_native/crowd.exe) headlessly.

The club is a fullscreen GLES2 game with no CLI surface: every answer it has comes out
as PIXELS. This is the handle on it. Every subcommand below leaves a PNG (or an MP4)
on disk that you are expected to LOOK at -- the engine's own law is "pixels or it
didn't happen", and a log line is not a receipt.

    python .claude/skills/run-reva/driver.py <command> [...]

    build                      compile to crowd_new.exe (mingw64 PATH handled)
    promote                    crowd_new.exe -> crowd.exe (after you've looked)
    kill                       kill any running instance (only one may run)
    packs [substr]             list packs, or census one pack's clips/morphs/marks
    shot OUT.png               boot the club and capture a frame
    lab OUT.png                the ANIM LAB with a ctl line (--ctl "...")
    face OUT.png               the FACE RIG (8 angles + mouth/eye cams) + mark gizmo
    scene OUT.mp4              record a conversation scene with its audio muxed in
    bench OUT.png              ONE BODY, FOUR ANGLES x N frames, nothing else in frame
    marks                      PLACE THE FACE MARKS BY CLICKING ON HER (interactive)

Run `... driver.py <command> --help` for the flags. Everything takes --subject
(default r2moves) which names packs/v1_<subject>.rpack.

WHY EACH GUARD IS HERE (all learned the hard way, see SKILL.md Gotchas):
  * gcc needs mingw64 FIRST on PATH -- the shell's own /mingw64/bin entry is dead.
  * only ONE instance may run; a second one fights for the GPU and the ctl file.
  * the engine writes PPM and needs a WINDOWS path for it ("a native exe needs a
    Windows path" is its own error message), so paths are absolutised here.
  * a ctl file is only re-read when seq= CHANGES, so seq is stamped per write.
  * REVA_WINDOWED=1 or it goes fullscreen over whatever you were looking at.
"""
import argparse, os, re, shutil, subprocess, sys, time, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))   # the repo root
NATIVE = os.path.join(ROOT, "reva_native")
AUTHOR = os.path.join(ROOT, "skate_src", "author")

# bare `python` is the Microsoft Store alias: it shadows APPDATA and poisons results.
PY = os.environ.get("CLUB_PYTHON", sys.executable)
MINGW = r"C:\msys64\mingw64\bin"
LF = chr(10)
FFMPEG = shutil.which("ffmpeg") or next(iter(glob.glob(os.path.join(
    os.environ.get("LOCALAPPDATA", os.path.expanduser("~/AppData/Local")),
    "Microsoft", "WinGet", "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe"))), None)   # PATH first (Linux, 09-30), then WinGet

SRC = ["crowd_kms.c", "club_math.c", "club_audio.c", "club_save.c", "holly_fbo.c", "crowd_win_rc.o"]   # the split (2026-09-30): club.h lists the units
LIBS = ["-Dmain=SDL_main", "-lmingw32", "-lSDL2main", "-lSDL2", "-lEGL", "-lGLESv2", "-lm"]

# the face rig's panel rectangles in the saved 1280x720 image (crowd_kms.c, g_labFace):
# eight heads across the top half, then the mouth and the eyes big across the bottom.
PANELS = {"mouth": (0, 360, 640, 720), "eyes": (640, 360, 1280, 720),
          "front": (0, 0, 320, 180), "profile": (640, 0, 960, 180),
          "full": None}


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd, str), **kw)


def win(p):
    """the engine is a native exe: it cannot open /c/... or a relative path"""
    return os.path.abspath(p).replace("/", "\\")


def kill(_a=None):
    for exe in ("crowd.exe", "crowd_new.exe"):
        subprocess.run(["taskkill", "/IM", exe, "/F"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return 0


def build(a):
    """⛔ THE BUILD DIES WITHOUT MINGW64 ON PATH. The login shell's PATH carries a
    /mingw64/bin entry that does not resolve, so gcc is simply absent; the repo also
    warns it can fail SILENTLY with exit 1 and no output. Either way the fix is this
    prepend, so it is not optional and not left to the caller."""
    env = dict(os.environ, PATH=MINGW + os.pathsep + os.environ["PATH"])
    out = "crowd_new.exe"
    # ⛔ On Windows, subprocess resolves the EXE from the PARENT's PATH -- the env=
    # you hand it is only inherited by the child. Passing "gcc" here fails with
    # WinError 2 even though env["PATH"] starts with mingw64. Name it absolutely; the
    # env PATH still matters, because the linker looks up its DLLs through it.
    cmd = [os.path.join(MINGW, "gcc.exe"), "-O2", "-o", out] + SRC + LIBS
    print("build: " + " ".join(cmd))
    r = subprocess.run(cmd, cwd=NATIVE, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    errs = [l for l in r.stdout.splitlines() if " error" in l or l.startswith("cc1")]
    if r.returncode != 0 or errs:
        print("\n".join(errs[:40]) or r.stdout[-4000:])
        print("build FAILED rc=%d" % r.returncode)
        return 1
    p = os.path.join(NATIVE, out)
    print("build OK -> %s (%d bytes)" % (p, os.path.getsize(p)))
    return 0


def promote(_a):
    """crowd_new.exe is what you test; crowd.exe is what dance.bat and the live window
    run. The repo's law is: verify on pixels FIRST, then promote."""
    shutil.copy2(os.path.join(NATIVE, "crowd_new.exe"), os.path.join(NATIVE, "crowd.exe"))
    print("promoted crowd_new.exe -> crowd.exe")
    return 0


def _rx(pat):
    try:
        return re.compile(pat, re.I)
    except re.error:
        return re.compile(re.escape(pat), re.I)


def _ctl(text):
    """a ctl file is only re-read when seq= CHANGES, so stamp a fresh one every write"""
    path = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
    seq = int(time.time()) % 100000
    # ⛔ AND NEVER THE SAME ONE TWICE. A seconds clock repeats inside a second, and a live
    # window is written to twice in one (the ctl, then the shot) -- the second write was lost.
    try:
        old = re.search(r"seq=(\d+)", open(path).read())
        if old and int(old.group(1)) >= seq: seq = int(old.group(1)) + 1
    except Exception:
        pass
    body = "seq=%d %s\n" % (seq, text.strip())
    open(path, "w").write(body)
    return path, body


def _run(env_extra, exe, args, timeout=300, quiet=True):
    kill()
    exe_path = os.path.join(NATIVE, exe)
    if not os.path.exists(exe_path):
        print("no %s -- run `driver.py build` first" % exe); return None
    env = dict(os.environ, REVA_WINDOWED="1", **env_extra)
    r = subprocess.run([exe_path] + args, cwd=NATIVE, env=env, timeout=timeout,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    if not quiet:
        print(r.stdout[-3000:])
    return r


def _ppm2png(ppm, png, crop=None, scale=None):
    from PIL import Image
    if not os.path.exists(ppm):
        print("NO FRAME WRITTEN at %s" % ppm); return False
    im = Image.open(ppm).convert("RGB")
    if crop: im = im.crop(crop)
    if scale: im = im.resize((int(im.size[0] * scale), int(im.size[1] * scale)), Image.LANCZOS)
    im.save(png)
    os.remove(ppm)
    print("wrote %s  %dx%d   <-- LOOK AT IT" % (png, im.size[0], im.size[1]))
    return True


def _capture(a, ctl_text, env_extra, crop=None, scale=None, argv3=None):
    exe = "crowd_new.exe" if a.new else "crowd.exe"
    if ctl_text is not None:
        _, body = _ctl(ctl_text)
        print("ctl: " + body.strip())
        env_extra["REVA_LABCTL"] = "anim_bench/lab_ctl.txt"
    ppm = os.path.splitext(os.path.abspath(a.out))[0] + ".ppm"
    os.makedirs(os.path.dirname(os.path.abspath(a.out)) or ".", exist_ok=True)
    env_extra.update(REVA_SHOT=str(a.frame), REVA_SHOTN="1", REVA_SHOT_PATH=win(ppm))
    for kv in (a.env or []):
        k, _, v = kv.partition("="); env_extra[k] = v
    r = _run(env_extra, exe, ["400", "packs", argv3 or a.subject], quiet=not a.verbose)
    if r is None: return 1
    if a.grep:
        for line in r.stdout.splitlines():
            # ⛔ THIS WAS A PLAIN SUBSTRING MATCH, so `--grep "cloth|set]"` silently
            # matched NOTHING and read as "the engine printed nothing". Anyone typing
            # --grep expects a regex; fall back to a literal if it will not compile.
            if _rx(a.grep).search(line): print("  | " + line)
    return 0 if _ppm2png(ppm, a.out, crop, scale) else 1


LIVE = os.path.join(NATIVE, "anim_bench", "live.json")
LIVELOG = os.path.join(NATIVE, "anim_bench", "live.log")


def _pid_alive(pid):
    r = subprocess.run(["tasklist", "/FI", "PID eq %d" % int(pid), "/NH"],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding="utf-8", errors="replace")
    return str(int(pid)) in r.stdout


def _live_rec():
    import json
    try:
        rec = json.load(open(LIVE))
    except Exception:
        return None
    return rec if _pid_alive(rec.get("pid", -1)) else None


def _wait_file(p, timeout, pid=None):
    """a frame is written when its size stops changing; give up if the engine died"""
    t0, last, n = time.time(), -1, 0
    while time.time() - t0 < timeout:
        n += 1
        if pid is not None and n % 12 == 0 and not _pid_alive(pid):
            return False
        if os.path.exists(p):
            sz = os.path.getsize(p)
            if sz > 0 and sz == last:
                return True
            last = sz
        time.sleep(0.25)
    return False


def up(a):
    """ONE WINDOW, KEPT UP. jacob, watching the engine open and close for every shot: "why do
    you have to open and close reva everytime?" -- it never had to. ctl `shot=<path>` has always
    written the next frame of a RUNNING window and kept going (the LIVE SHOT branch of the frame
    loop); the driver just never used it, so every lab/face shot paid a ~1 minute boot through
    140+ packs. `up` boots once; `lab --live` / `face --live` steer that window through the ctl
    file and grab frames from it; `down` closes it.
    ⛔ A running window keeps the packs it BOOTED with. --live relaunches on its own, and says
    why, when any pack on disk is newer than the boot, or the subject, exe or --env differ.
    ⛔ `bench` and `scene` still boot their own engine (their captures are armed by env vars at
    launch) and `kill` every window first, the live one included."""
    import json
    kill()
    exe = "crowd_new.exe" if a.new else "crowd.exe"
    exe_path = os.path.join(NATIVE, exe)
    if not os.path.exists(exe_path):
        print("no %s -- run `driver.py build` first" % exe); return 1
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ=a.subject, REVA_ANIMSTATE="0", REVA_FACEDBG="1")
    for kv in (a.env or []):
        k, _, v = kv.partition("="); env[k] = v
    flags = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) | getattr(subprocess, "DETACHED_PROCESS", 0)
    logf = open(LIVELOG, "w")
    pr = subprocess.Popen([exe_path, "400", "packs", a.subject], cwd=NATIVE, env=env,
                          stdout=logf, stderr=subprocess.STDOUT, creationflags=flags)
    logf.close()
    rec = dict(pid=pr.pid, exe=exe, subject=a.subject, t0=time.time(), env=a.env or [], sets={})
    json.dump(rec, open(LIVE, "w"))
    # BOOTED = IT ANSWERS A SHOT. The ctl is only polled once the lab loop runs, so the probe
    # frame arriving is the boot finishing -- no frame number guessed against a load time.
    probe = os.path.join(NATIVE, "anim_bench", "_live_probe.ppm")
    if os.path.exists(probe): os.remove(probe)
    t0 = time.time()
    _ctl("face=0 grid=0 dqs=0 clip=z_upright partner=off shot=%s" % win(probe))
    if not _wait_file(probe, 240, pr.pid):
        print("live: the window never answered -- log tail:")
        print("".join(open(LIVELOG, errors="replace").readlines()[-15:]))
        return 1
    os.remove(probe)
    print("live: %s up with %s in %.0fs (pid %d). Steer it: lab/face --live. Close it: driver.py down"
          % (exe, a.subject, time.time() - t0, pr.pid))
    return 0


def down(_a):
    kill()
    if os.path.exists(LIVE): os.remove(LIVE)
    print("live: down")
    return 0


def _live_shot(a, ctl_text, crop=None, scale=None):
    import json
    rec = _live_rec()
    exe = "crowd_new.exe" if a.new else "crowd.exe"
    why = None
    if rec is None:
        why = "no live window"
    elif rec["subject"] != a.subject or rec["exe"] != exe:
        why = "the live window is %s on %s" % (rec["subject"], rec["exe"])
    elif (a.env or []) != rec.get("env", []):
        why = "this shot's --env differs from the live window's"
    else:
        newer = [f for f in glob.glob(os.path.join(NATIVE, "packs", "*.rpack")) if os.path.getmtime(f) > rec["t0"]]
        if newer:
            why = "%s changed on disk after the live window booted" % os.path.basename(newer[0])
    if why:
        print("live: %s -- booting one" % why)
        if up(a) != 0: return 1
        rec = _live_rec()
        if rec is None: return 1
    ctl_text = (ctl_text or "").strip()
    # ⛔ A REWRITTEN SET NEEDS set=off FIRST: the engine loads a set only when its NAME changes,
    # so a live window would keep drawing the old _bench.set forever.
    m = re.search(r"\bset=(\S+)", ctl_text)
    if m and m.group(1) != "off":
        sp = os.path.join(NATIVE, "sets", m.group(1) + ".set")
        mt = os.path.getmtime(sp) if os.path.exists(sp) else 0.0
        if rec["sets"].get(m.group(1)) != mt:
            _ctl(re.sub(r"\bset=\S+", "set=off", ctl_text)); time.sleep(0.6)
            rec["sets"][m.group(1)] = mt
            json.dump(rec, open(LIVE, "w"))
    logpos = os.path.getsize(LIVELOG) if os.path.exists(LIVELOG) else 0
    _, body = _ctl(ctl_text); print("ctl: " + body.strip())
    time.sleep(a.settle)                      # sets load, lenses and bodies ease in
    ppm = os.path.splitext(os.path.abspath(a.out))[0] + ".ppm"
    os.makedirs(os.path.dirname(ppm) or ".", exist_ok=True)
    if os.path.exists(ppm): os.remove(ppm)
    _ctl(ctl_text + " shot=%s" % win(ppm))
    if not _wait_file(ppm, 60, rec["pid"]):
        print("live: no frame came back (engine log: %s)" % LIVELOG); return 1
    if a.grep:
        with open(LIVELOG, errors="replace") as f:
            f.seek(logpos)
            for line in f.read().splitlines():
                if _rx(a.grep).search(line): print("  | " + line)
    return 0 if _ppm2png(ppm, a.out, crop, scale) else 1


def shot(a):
    """boot the club proper (mode select -> variant -> character -> play) and shoot it"""
    # ⛔ argv[3] is the FLOW, not a body. The club boots mode select -> variant ->
    # character select, and that flow is named "select"; handing it a pack name here
    # loads that body as the hero and the menus never run, so REVA_SHOT fires on a
    # frame the capture path never reaches and NO FILE IS WRITTEN.
    env = {}
    if a.mode is not None:
        env.update(REVA_AUTOMODE=str(a.mode), REVA_AUTOVAR=str(a.var), REVA_AUTOPICK=str(a.pick))
    # ⭐ --ctl lets a GAME DOOR load a set (`set=parsons`) the way the lab does. Without it a
    # door boots onto the bare club floor, and "walk the player into a house" cannot be
    # tested in the mode that actually has a player.
    return _capture(a, a.ctl, env, argv3=a.flow)


def lab(a):
    """the ANIM LAB (mode 11): one body, a ctl line, and a camera you control"""
    if getattr(a, "live", False):
        return _live_shot(a, a.ctl)
    return _capture(a, a.ctl, dict(REVA_AUTOMODE="11", REVA_ANIMSUBJ=a.subject,
                                   REVA_ANIMSTATE="0"))


def face(a):
    """THE FACE RIG: eight angles round the head plus a mouth and an eye camera, with
    the face marks drawn in (cyan lips / green mouth corners / yellow eyes / magenta
    chin / red jaw hinge). --panel crops one lens; --jaw parks the mouth open."""
    ctl = "face=10 dqs=0 clip=%s partner=%s %s look=%d jaw=%s" % (
        a.clip, a.partner, ("pclip=%s" % a.pclip) if a.partner != "off" else "",
        1 if a.look else 0, a.jaw)
    env = dict(REVA_AUTOMODE="11", REVA_ANIMSUBJ=a.subject, REVA_ANIMSTATE="0",
               REVA_FACEDBG="1")
    if getattr(a, "live", False):
        return _live_shot(a, ctl, crop=PANELS[a.panel], scale=a.zoom)
    return _capture(a, ctl, env, crop=PANELS[a.panel], scale=a.zoom)


def scene(a):
    """Record a conversation scene WITH ITS AUDIO. REVA_SCENEFPS makes the scene clock
    fixed-step and starts the capture on the scene's own first frame, so frame N is
    scene second N/fps exactly and the wav muxes in blind.

    ⛔ REVA_FIXEDSTEP MUST GO WITH IT. REVA_SCENEFPS only steps the SCENE clock -- the
    bodies' clip phases still advance on `dt`, which during a capture is however long
    the frame took to write 2.7MB to disk. So the picture stayed in sync with the wav
    while the ANIMATION ran at capture speed: a walk cycle sprinting under a woman
    strolling away. It went unnoticed while both bodies stood in z_upright and showed up
    the moment a scene played a real clip."""
    if not FFMPEG:
        print("no ffmpeg found"); return 1
    frames = int(a.seconds * a.fps)
    tmp = os.path.abspath(a.tmp or os.path.join(os.path.dirname(os.path.abspath(a.out)), "_capframes"))
    os.makedirs(tmp, exist_ok=True)
    for f in glob.glob(os.path.join(tmp, "*.ppm")): os.remove(f)
    # ⛔ NO clip=/pclip= HERE. A scene's own per-line staging resolves both, and writing
    # them into the ctl overwrote that resolution on every poll -- which is how kaeli spent
    # a whole take crawling through her get-up-off-the-floor loop. The engine ignores them
    # during a scene now too; this just stops sending them.
    ctl = ("face=0 grid=0 dqs=0 partner=%s pyaw=%s pgap=%s pside=%s "
           "jaw=-1 look=%d scene=%s" % (a.partner, a.pyaw, a.pgap,
                                        a.pside, 1 if a.look else 0, a.name))
    # A SCENE CAN HAPPEN IN THE WORLD. The set renderer and the scene system coexist --
    # `set=` draws the street, `stage=` stands the pair in it, and the scene camera keeps
    # working because it frames the two HEAD POSITIONS, which move with them.
    if a.partner2:
        ctl += " partner2=%s" % a.partner2
    if getattr(a, "partner3", ""):
        ctl += " partner3=%s" % a.partner3
    if a.set:
        ctl += " set=%s" % a.set
        if a.stage: ctl += " stage=%s" % a.stage
        if a.sun:   ctl += " sun=%s" % a.sun
        if a.sky:   ctl += " sky=%s" % a.sky
        # ⛔ A DAY SET SHOT AT NIGHT NEEDS ITS LAMPS TAKEN DOWN. other_side.set's practicals
        # were authored for ep 4 at tod=10.0; at ep 5's tod=21.5 the same porch light delivers
        # ~30x the sky ambient and the street comes back in FULL DAYLIGHT with sun=/sky=
        # correctly set and visibly doing nothing. The engine reports it at set load now;
        # this is the dimmer. 1.0 (default) leaves the set exactly as authored.
        if a.lamps is not None: ctl += " lamps=%s" % a.lamps
        # A WIDE PINS ITS OWN LENS. The conversation camera frames the two heads, which is
        # right for coverage and wrong for act IV, where the pair are off-camera and the
        # shot is the street. cam= overrides it for that one kind of shot.
        if a.cam:   ctl += " cam=%s" % a.cam
    _, body = _ctl(ctl); print("ctl: " + body.strip())
    print("capturing %d frames at %d fps (this takes a few minutes)..." % (frames, a.fps))
    # ⛔ POLL AND KILL; DO NOT BLOCK. The engine exits only when `taken >= burst`, and
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
    # ⭐ --env on a SCENE too (09-11): the arms-up A/B wanted REVA_NOWMS=1 on a take and the
    # flag only existed on lab/bench/up, so the take never ran and the strip came back black.
    for kv in (a.env or []):
        k, _, v = kv.partition("="); env[k] = v
    # ⭐ KEEP THE ENGINE'S WORD. `[stage] hclip 'x' NOT FOUND`, `[cue]`, `[step]` and
    # `[scene]` all come out on stdout, and a take recorded with it thrown away cannot say
    # why a clip was missing. It lands beside the mp4.
    logp = os.path.splitext(os.path.abspath(a.out))[0] + ".log"
    logf = open(logp, "w")
    # ⭐ ep 12: --res WxH is the engine's argv[4] (it always forced the window size; the driver never passed it),
    # so a scene can be filmed at 1080p. Frames are PPM until the mux: 6.2 MB each at 1920x1080 -- put --tmp on D:.
    pr = subprocess.Popen([exe, "400", "packs", a.subject] + ([a.res] if getattr(a, "res", "") else []),
                          cwd=NATIVE, env=env, stdout=logf, stderr=subprocess.STDOUT)
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
    kill()
    n = len(glob.glob(os.path.join(tmp, "*.ppm")))
    logf.close()
    print("captured %d frames   (engine log: %s)" % (n, logp))
    if n < 10:
        print("too few frames -- is scenes/%s.scn present?" % a.name); return 1
    wav = os.path.join(NATIVE, "scenes", "%s.wav" % a.name)
    vf = []
    if a.crop:   # the conversation camera aims at the SPEAKER's head, so it lands dead
                 # centre -- a centre crop is a free close-up that always frames whoever
                 # is talking, with no tracking.
        vf.append("crop=560:315:360:150,scale=1280:720:flags=lanczos,eq=brightness=0.06:contrast=1.12")
    elif a.grade:
        vf.append("eq=brightness=0.02:contrast=1.07:saturation=1.04")
    cmd = [FFMPEG, "-y", "-framerate", str(a.fps), "-start_number", "0",
           "-i", os.path.join(tmp, "f_%05d.ppm")]
    if os.path.exists(wav): cmd += ["-i", wav]
    if vf: cmd += ["-vf", ",".join(vf)]
    cmd += ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "19", "-preset", "veryfast"]
    if os.path.exists(wav): cmd += ["-c:a", "aac", "-b:a", "192k", "-shortest"]
    cmd += [os.path.abspath(a.out)]
    rc = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
    if rc.returncode != 0:
        print(rc.stderr[-2000:]); return 1
    for f in glob.glob(os.path.join(tmp, "*.ppm")): os.remove(f)
    try: os.rmdir(tmp)
    except OSError: pass
    print("wrote %s (%.1f MB)   <-- WATCH IT" % (a.out, os.path.getsize(a.out) / 1e6))
    return 0


def packs(a):
    """the roster, or one pack's census -- clips, morphs and which face marks it has"""
    import struct
    sys.path.insert(0, AUTHOR)
    from rpack import Rpack, HDR
    pd = os.path.join(NATIVE, "packs")
    hits = sorted(f for f in os.listdir(pd)
                  if f.endswith(".rpack") and (not a.name or a.name in f))
    if not a.name or len(hits) > 1:
        for f in hits: print("  " + f)
        print("%d pack(s)%s" % (len(hits), "" if not a.name else " matching %r" % a.name))
        return 0
    p = Rpack(os.path.join(pd, hits[0]))
    nm = p.node_names()
    print("%s: v%d nodes %d meshes %d clips %d tex %d  %.1f MB"
          % (hits[0], p.ver, p.nNodes, p.nMeshes, p.nClips, p.nTex, len(p.buf) / 1e6))
    print("  face marks: %s" % (", ".join(n for n in nm if n.startswith("face_")) or "NONE"))
    for mi in range(p.nMeshes):
        r = p.MESH.unpack_from(p.buf, p.offMeshes + mi * 60)
        print("  mesh%d '%s' verts %d bones %d" % (mi, p.sname(r[0]), r[2], r[4]))
    offM = HDR.unpack_from(p.buf, 4)[14]
    if offM:
        n = struct.unpack_from("<I", p.buf, offM)[0]
        print("  morphs: " + ", ".join(p.sname(struct.unpack_from("<4I", p.buf, offM + 4 + i * 16)[1])
                                       for i in range(n)))
    print("  clips: " + ", ".join(p.clip_names()))
    return 0


def bench(a):
    """ONE BODY, FOUR ANGLES, ACROSS TIME -- the isolation read.

    jacob, watching me judge a garment off a wide two-shot with a second character in
    it: "you need to isolate kaelie and the pajamas to get a clear read on them. use the
    multi and framing we have been using. comeo n now, this is our test bench."

    He was right and it settled six rounds of guessing in one sheet. A body judged beside
    another body in a wide is judged at maybe 200 pixels tall; alone, from four sides,
    across eight moments, it cannot hide. That sheet found five bugs in a row -- ending
    with a bone map that had been wrong since the file was first written.

    anim_bench/ridecheck.py IS the sheet. It wants a SET with the subject placed as an
    ACTOR at the origin, and hand-authoring that set is the whole reason the read did not
    get taken sooner -- so this writes it.
    ⛔ The subject is an ACTOR, not the hero: the lab parks its hero at z -150 while the
    bench's four cameras all look at the origin, so a subject loaded as the hero is not
    in any of the frames.
    ⭐ --env REVA_NOCLOTH=1 is THE CONTROL RUN. Shooting the same body with the solver
    off is what separates "the sim is wrong" from "the mesh is wrong", and it answered in
    one run what four rounds of tuning had not.
    """
    setp = os.path.join(NATIVE, "sets", "_bench.set")
    open(setp, "w").write(LF.join([
        "# _bench.set -- WRITTEN BY driver.py bench, overwritten every run.",
        "# One body at the origin facing +x, on a strip of road for scale and contact.",
        "prop road_01     -14  0.00  14   0  1.0",
        "prop grass_00    -14 -0.02  42   0  1.0  0.30 0.44 0.20",
        "actor v1_%s  %s   0  0 0  90  %g" % (a.subject, a.clip, a.scale),
        ""]))
    print("wrote sets/_bench.set  (actor v1_%s clip %s)" % (a.subject, a.clip))
    cmd = [PY, os.path.join(NATIVE, "anim_bench", "ridecheck.py"),
           "--set", "_bench", "--body", a.host, "--secs", str(a.secs),
           "--cols", str(a.cols), "--orbit", str(a.orbit),
           "--out", os.path.abspath(a.out)]
    for kv in (a.env or []):
        cmd += ["--env", kv]
    r = subprocess.run(cmd, cwd=ROOT)
    if r.returncode == 0:
        print("wrote %s  (%s x %d frames)   <-- LOOK AT IT"
              % (a.out, ("%d angles" % a.orbit) if a.orbit else "4 angles", a.cols))
    return r.returncode


def marks(a):
    """Place a body's face marks by pointing at them.

    jacob: "man i could place them for you eaasier" -- and he can. Placing a face mark is
    something a person does by LOOKING, and the automatic tools genuinely cannot do it on
    kaeli: geometry picks her FRINGE, and face_calibrate.py reads eyes as the darkest texels
    and reports "no symmetric dark pair" because of her liner.

    ⛔ THE FIRST VERSION OF THIS WAS FOUR ENVIRONMENT VARIABLES AND A WALKING SUBJECT. In the
    anim lab she walks and the camera director wanders, so he would have been clicking a
    small moving figure through a moving lens. Testing rather than assuming is what caught
    it: the log said "STATE 2/7 WALK".
    ⭐ Now the command stages it. A short headless pass asks the engine where her head is,
    then the interactive launch PINS her and parks the lens on that head.

        1 2 3 4      eye_l / eye_r / mouth / chin
        LEFT CLICK   place the selected mark
        ESC          done -- the file is written on every click, nothing to save
    """
    import re as _re
    subj = a.subject
    ctlp = os.path.join(NATIVE, "anim_bench", "lab_ctl.txt")
    base = "face=0 grid=0 dqs=0 partner=off clip=%s" % a.clip

    # ---- pass 1: where is her head? ------------------------------------------------
    print("finding %s's head..." % subj)
    open(ctlp, "w").write("seq=%d %s\n" % (int(time.time()) % 100000, base))
    env = dict(os.environ, REVA_WINDOWED="1", REVA_LABCTL="anim_bench/lab_ctl.txt",
               REVA_AUTOMODE="11", REVA_ANIMSUBJ=subj, REVA_ANIMSTATE="0",
               REVA_MARKEDIT="1", REVA_FIXEDSTEP="30")
    kill()
    pr = subprocess.Popen([os.path.join(NATIVE, "crowd.exe"), "400", "packs", subj],
                          cwd=NATIVE, env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    head = None
    t0 = time.time()
    while time.time() - t0 < 90:
        ln = pr.stdout.readline()
        if not ln:
            break
        m = _re.search(r"\[mark\] head at ([-\d.]+) ([-\d.]+) ([-\d.]+)", ln)
        if m:
            head = tuple(float(m.group(i)) for i in (1, 2, 3))
            break
    pr.terminate()
    try: pr.wait(timeout=10)
    except Exception: pr.kill()
    if not head:
        print("could not find the head -- does %s have a face rig? (packs %s)" % (subj, subj))
        return 1
    print("  head at %.3f %.3f %.3f" % head)

    # ---- pass 2: interactive, pinned, lens on her face -----------------------------
    cam = "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d" % (
        head[0] + 0.14, head[1] + 0.06, head[2] + a.dist,
        head[0], head[1] - 0.02, head[2], a.fov)
    open(ctlp, "w").write("seq=%d %s cam=%s\n" % (int(time.time()) % 100000, base, cam))
    print()
    print("  1 2 3 4     eye_l / eye_r / mouth / chin")
    print("  LEFT CLICK  place the selected mark")
    print("  ESC         done. anim_bench/marks_%s.txt is written on EVERY click." % subj)
    print()
    env["REVA_NOMOUSEGRAB"] = "1"
    env.pop("REVA_FIXEDSTEP", None)
    subprocess.run([os.path.join(NATIVE, "crowd.exe"), "400", "packs", subj],
                   cwd=NATIVE, env=env)
    f = os.path.join(NATIVE, "anim_bench", "marks_%s.txt" % subj)
    if os.path.exists(f):
        print()
        print(open(f).read().rstrip())
        print()
        print("bake it in:")
        print("  %s skate_src/author/face_marks.py reva_native/packs/v1_%s.rpack --from %s"
              % ("python", subj, os.path.relpath(f, ROOT).replace("\\", "/")))
    else:
        print("no marks file -- nothing was placed")
    return 0


def main():
    # ⛔ argparse writes help to a cp1252 stdout here, so ONE non-ascii glyph in a help
    # string takes the whole command down with a UnicodeEncodeError. Same trap packcheck
    # hit on the ninjato pack name.
    # ⛔ AND LINE-BUFFERED. Redirected to a file, python block-buffers stdout, so an
    # interactive command that blocks (marks) shows the user NOTHING until it exits --
    # its instructions arrive after they are needed.
    try: sys.stdout.reconfigure(encoding='utf-8', errors='replace', line_buffering=True)
    except Exception: pass
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(sp, out_default=None):
        sp.add_argument("out", nargs="?" if out_default else None, default=out_default)
        sp.add_argument("--subject", default="r2moves", help="packs/v1_<subject>.rpack")
        sp.add_argument("--frame", type=int, default=420,
                        help="capture frame; boot takes ~250 frames, so keep it >300")
        sp.add_argument("--new", action="store_true", help="use crowd_new.exe")
        sp.add_argument("--env", action="append", help="extra REVA_* var, K=V")
        sp.add_argument("--grep", help="echo matching stdout lines")
        sp.add_argument("--verbose", action="store_true")
        sp.add_argument("--live", action="store_true",
                        help="steer the window `driver.py up` left running instead of booting one")
        sp.add_argument("--settle", type=float, default=2.5,
                        help="--live: seconds between the ctl line and the grab")

    sub.add_parser("kill").set_defaults(fn=kill)
    sp = sub.add_parser("up"); sp.add_argument("--subject", default="r2moves")
    sp.add_argument("--new", action="store_true"); sp.add_argument("--env", action="append")
    sp.set_defaults(fn=up)
    sub.add_parser("down").set_defaults(fn=down)
    sub.add_parser("build").set_defaults(fn=build)
    sub.add_parser("promote").set_defaults(fn=promote)

    sp = sub.add_parser("packs"); sp.add_argument("name", nargs="?"); sp.set_defaults(fn=packs)

    sp = sub.add_parser("shot"); common(sp)
    sp.add_argument("--mode", type=int, help="auto-drive mode select to this door")
    sp.add_argument("--var", type=int, default=0); sp.add_argument("--pick", type=int, default=0)
    sp.add_argument("--flow", default="select", help='argv[3]: "select" for the menus')
    sp.add_argument("--ctl", default=None, help="a lab ctl line for the door, e.g. 'set=parsons'")
    sp.set_defaults(fn=shot)

    sp = sub.add_parser("lab"); common(sp)
    sp.add_argument("--ctl", default="face=0 grid=0 dqs=0 clip=z_upright partner=off")
    sp.set_defaults(fn=lab)

    sp = sub.add_parser("face"); common(sp)
    sp.add_argument("--panel", choices=list(PANELS), default="mouth")
    sp.add_argument("--zoom", type=float, default=None)
    sp.add_argument("--jaw", default="-1", help="0..1 parks the mouth open; -1 = the scene")
    sp.add_argument("--clip", default="idle")
    sp.add_argument("--partner", default="off"); sp.add_argument("--pclip", default="idle")
    sp.add_argument("--look", action="store_true")
    sp.set_defaults(fn=face)

    sp = sub.add_parser("marks")
    sp.add_argument("--subject", default="kmoves")
    sp.add_argument("--clip", default="z_upright",
                    help="a STATIC pose. \u26d4 not `idle` -- on kaeli that tilts her head at "
                         "the floor, and marks judged on a tilted head are a projection, "
                         "not a position. That cost three rounds by hand.")
    sp.add_argument("--dist", type=float, default=1.72, help="lens distance, club units")
    sp.add_argument("--fov", type=int, default=30)
    sp.set_defaults(fn=marks)

    sp = sub.add_parser("bench"); sp.add_argument("out")
    sp.add_argument("--scale", type=float, default=1.0,
                    help="⛔ THE BENCH CAMERAS ARE FIXED and were framed for a ~1.75 body. "
                         "A pack with a different load scale fills the frame and you shoot its "
                         "knees -- hina came back as two legs. Scale the actor to fit.")
    sp.add_argument("--orbit", type=int, default=8,
                    help="⭐ N evenly-spaced yaws in ONE sheet. jacob: \"why are you "
                         "doing one angle at a time. you should be able to do like 8 at "
                         "once\" -- and shooting singles by hand is exactly the loop this "
                         "bench exists to replace. 0 = the four named framings.")
    sp.add_argument("--subject", default="r2moves", help="the body to isolate: packs/v1_<subject>.rpack")
    sp.add_argument("--clip", default="tpose",
                    help="STATIC by default. jacob: \"this animation you are using on reva "
                         "while trying to calibrate the clothing is preventing you from being "
                         "able to do so. you need a stable test bench.\" He is right -- a "
                         "garment judged on a body mid-crouch is unreadable, and one look at a "
                         "T-pose showed it was a CATSUIT, which six rounds of animated benches "
                         "had not. Same mistake as judging eye marks on a tilted head.")
    sp.add_argument("--secs", type=float, default=6.0)
    sp.add_argument("--cols", type=int, default=8, help="time columns per angle")
    sp.add_argument("--host", default="r2moves",
                    help="the lab hero, parked off-camera at z -150; NOT the subject")
    sp.add_argument("--env", action="append", help="extra REVA_* var, e.g. REVA_NOCLOTH=1")
    sp.set_defaults(fn=bench)

    sp = sub.add_parser("scene"); sp.add_argument("out")
    sp.add_argument("--name", default="club_night"); sp.add_argument("--seconds", type=float, default=60.0)
    sp.add_argument("--fps", type=int, default=30); sp.add_argument("--subject", default="r2moves")
    sp.add_argument("--partner", default="kmoves"); sp.add_argument("--pclip", default="idle")
    # ⭐ the scene system takes three bodies now; slot 2 is loaded the same way slot 1 is
    sp.add_argument("--partner2", default="")
    sp.add_argument("--partner3", default="")   # ⭐ ep 7: the fourth body (SCN_MAXCAST 5)
    sp.add_argument("--clip", default="idle")
    sp.add_argument("--pyaw", default="185"); sp.add_argument("--pgap", default="1.05")
    sp.add_argument("--pside", default="0.12"); sp.add_argument("--look", action="store_true", default=True)
    sp.add_argument("--crop", action="store_true", help="2x centre crop = a close-up on the speaker")
    sp.add_argument("--grade", action="store_true", default=True)
    sp.add_argument("--raw", dest="grade", action="store_false", help="no colour grade")
    sp.add_argument("--set", default="", help="play the scene inside this .set")
    sp.add_argument("--stage", default="", help="x,z,yawDeg -- where the pair stands in it")
    sp.add_argument("--sun", default="", help="elev,azim,intensity[,r,g,b]")
    sp.add_argument("--sky", default="", help="r,g,b")
    sp.add_argument("--lamps", default=None,
                    help="scale the SET's own `light` practicals (1.0 = as authored). A set lit "
                         "for daylight blows out at night: its lamps do not scale with the sun.")
    sp.add_argument("--cam", default="", help="ex,ey,ez,ax,ay,az,fov -- pin the lens (a wide)")
    sp.add_argument("--tmp"); sp.add_argument("--new", action="store_true")
    sp.add_argument("--res", default="", help="WxH render size, e.g. 1920x1080 (the engine's argv[4]; ep 12)")
    sp.add_argument("--env", action="append", help="extra REVA_* var, K=V (a control take)")
    sp.set_defaults(fn=scene)

    a = ap.parse_args()
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
