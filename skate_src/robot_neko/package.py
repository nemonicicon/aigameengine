#!/usr/bin/env python3
"""package.py -- turn a recorded sketch into an EPISODE of ROBOT NEKO.

    python skate_src/robot_neko/package.py --ep 2 --title CONTINUITY \
        --in out/robot_neko/ep02_raw.mp4 --out out/robot_neko/ep02_continuity.mp4

jacob, 2026-09-10, on the first sketch: "thats episode of robot neko" -- and then: "this
should be the kind of cartoon that gets better every time". A show has an identity every
episode shares, so this is a TOOL rather than a one-off: the same card, the same sting,
the episode's number and title on it.

  * THE TITLE CARD BOILS ON TWOS. Everything else in the show is exposed at twelve a second,
    so the card is too: the layer is set down a couple of pixels and a fraction of a degree
    off on every exposure and held for two frames. The logo pops in on an overshoot.
  * THE STING IS SYNTHESISED -- a square-wave arpeggio, a chord stab, a pitch-bent "meow" and
    two robot blips. No asset, no licence, and it can change with the show.
  * AN END CARD, then everything is concatenated at the episode's own size and rate.
"""
import argparse, glob, math, os, random, shutil, struct, subprocess, sys, tempfile, wave
import shutil
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS, SR = 1280, 720, 24, 48000
FF = shutil.which("ffmpeg") or next(iter(glob.glob(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet",
                                      "Packages", "Gyan.FFmpeg*", "ffmpeg*", "bin", "ffmpeg.exe"))), None)   # PATH first (Linux, 09-30), then WinGet


def font(names, size):
    for n in names:
        p = os.path.join(r"C:\Windows\Fonts", n)
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


BIG = ["impact.ttf", "ariblk.ttf", "arialbd.ttf"]
SMALL = ["bahnschrift.ttf", "arialbd.ttf", "arial.ttf"]


# ---------------------------------------------------------------------------- picture
def background(seed):
    im = Image.new("RGB", (W, H))
    px = im.load()
    for y in range(H):
        for x in range(0, W, 4):
            dx, dy = (x - W / 2) / W, (y - H / 2) / H
            r = math.sqrt(dx * dx + dy * dy)
            k = max(0.0, 1.0 - r * 1.35)
            c = (int(18 + 50 * k), int(10 + 16 * k), int(34 + 70 * k))
            for xx in range(x, min(W, x + 4)):
                px[xx, y] = c
    return im


def logo(d, cx, cy, s):
    """a robot cat head: steel head, two ears, a visor with two eyes, an antenna"""
    steel, rim, visor, eye, bulb = (58, 56, 78), (190, 140, 255), (14, 12, 22), (125, 249, 255), (255, 95, 162)
    ear = 0.62 * s
    for sgn in (-1, 1):
        d.polygon([(cx + sgn * 0.78 * s, cy - 0.35 * s), (cx + sgn * 0.98 * s, cy - 1.35 * s),
                   (cx + sgn * 0.22 * s, cy - 0.78 * s)], fill=steel, outline=rim, width=max(2, int(s * 0.05)))
    d.rounded_rectangle([cx - s, cy - 0.9 * s, cx + s, cy + 0.85 * s], radius=int(0.42 * s),
                        fill=steel, outline=rim, width=max(3, int(s * 0.06)))
    d.line([(cx, cy - 0.9 * s), (cx + 0.18 * s, cy - 1.55 * s)], fill=rim, width=max(2, int(s * 0.05)))
    d.ellipse([cx + 0.18 * s - 0.13 * s, cy - 1.55 * s - 0.13 * s, cx + 0.18 * s + 0.13 * s, cy - 1.55 * s + 0.13 * s], fill=bulb)
    d.rounded_rectangle([cx - 0.78 * s, cy - 0.35 * s, cx + 0.78 * s, cy + 0.12 * s], radius=int(0.2 * s), fill=visor)
    for sgn in (-1, 1):
        d.ellipse([cx + sgn * 0.38 * s - 0.13 * s, cy - 0.23 * s - 0.1 * s, cx + sgn * 0.38 * s + 0.13 * s, cy - 0.23 * s + 0.12 * s], fill=eye)
    # whiskers, because it is a cat
    for sgn in (-1, 1):
        for k in (-1, 0, 1):
            d.line([(cx + sgn * 0.55 * s, cy + 0.42 * s + k * 0.1 * s), (cx + sgn * 1.25 * s, cy + 0.36 * s + k * 0.2 * s)],
                   fill=rim, width=max(1, int(s * 0.025)))


def card(t, dur, ep, title, seed, end=False):
    """one frame of a card at time t, exposed on twos"""
    expo = int(t * 12)                                  # which exposure -- held for two frames
    rnd = random.Random(seed * 1000 + expo)
    bg = background(seed)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    te = expo / 12.0
    if not end:
        pop = min(1.0, te / 0.42)
        sc = pop * (1.0 + 0.18 * math.sin(pop * math.pi))      # overshoot, then settle
        logo(d, W / 2, H * 0.36, max(1, 118 * sc))
        if te > 0.33:
            f = font(BIG, 128)
            txt = "ROBOT NEKO"
            tw = d.textlength(txt, font=f)
            y = H * 0.58 + max(0.0, 0.5 - te) * 90
            for o in ((6, 6), (-3, 0), (3, 0), (0, -3), (0, 3)):
                d.text((W / 2 - tw / 2 + o[0], y + o[1]), txt, font=f, fill=(20, 8, 36))
            d.text((W / 2 - tw / 2, y), txt, font=f, fill=(250, 236, 255))
        if te > 0.9:
            f2 = font(SMALL, 44)
            sub = "EPISODE %d  \u00b7  %s" % (ep, title.upper())
            sw = d.textlength(sub, font=f2)
            d.text((W / 2 - sw / 2, H * 0.82), sub, font=f2, fill=(125, 249, 255))
    else:
        logo(d, W / 2, H * 0.42, 70)
        f = font(BIG, 72); f2 = font(SMALL, 30)
        for txt, ff, y, col in (("ROBOT NEKO", f, H * 0.60, (250, 236, 255)),
                                ("FIRESTRIDER STUDIOS", f2, H * 0.74, (190, 140, 255))):
            tw = d.textlength(txt, font=ff)
            d.text((W / 2 - tw / 2, y), txt, font=ff, fill=col)
    # the animator's hand: the whole layer lands a little off, per exposure
    ang = rnd.uniform(-0.8, 0.8)
    layer = layer.rotate(ang, resample=Image.BICUBIC, center=(W / 2 + rnd.uniform(-3, 3), H / 2 + rnd.uniform(-3, 3)))
    bg.paste(layer, (0, 0), layer)
    return bg


# ---------------------------------------------------------------------------- sound
def tone(buf, t0, dur, f0, f1=None, amp=0.3, shape="sq", vib=0.0, decay=None):
    n0, n = int(t0 * SR), int(dur * SR)
    ph = 0.0
    for i in range(n):
        t = i / SR
        f = f0 if f1 is None else f0 + (f1 - f0) * (t / dur)
        f *= 1.0 + vib * math.sin(2 * math.pi * 6.0 * t)
        ph += 2 * math.pi * f / SR
        s = math.sin(ph)
        if shape == "sq":
            s = 1.0 if s >= 0 else -1.0
        env = min(1.0, t / 0.006) * (math.exp(-t / decay) if decay else min(1.0, (dur - t) / 0.03))
        if 0 <= n0 + i < len(buf):
            buf[n0 + i] += s * amp * env


def sting(dur):
    b = [0.0] * int(dur * SR)
    notes = [523.25, 659.25, 783.99, 1046.5]
    for k, f in enumerate(notes):                                   # the arpeggio
        tone(b, 0.12 + k * 0.11, 0.10, f, amp=0.16)
    for f in (523.25, 659.25, 783.99):                              # the stab
        tone(b, 0.60, 0.45, f, amp=0.10, decay=0.18)
    tone(b, 1.20, 0.30, 700, 980, amp=0.30, shape="sin", vib=0.02)  # me-
    tone(b, 1.50, 0.42, 980, 520, amp=0.30, shape="sin", vib=0.03)  # -ow
    tone(b, 2.15, 0.07, 1400, amp=0.14)                             # robot
    tone(b, 2.27, 0.10, 900, amp=0.14)                              # blip
    return b


def end_bleep(dur):
    b = [0.0] * int(dur * SR)
    tone(b, 0.25, 0.08, 1200, amp=0.12)
    tone(b, 0.36, 0.14, 760, amp=0.12, decay=0.08)
    return b


def write_wav(p, b):
    peak = max(1e-6, max(abs(x) for x in b))
    g = min(1.0, 0.7 / peak)
    with wave.open(p, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, x * g)) * 32767)) for x in b))


# ---------------------------------------------------------------------------- assembly
def clip(tmp, name, dur, ep, title, seed, end, audio):
    d = os.path.join(tmp, name); os.makedirs(d, exist_ok=True)
    n = int(round(dur * FPS))
    last = None
    for i in range(n):
        t = i / FPS
        if int(t * 12) != last or i == 0:                           # render once per exposure
            im = card(t, dur, ep, title, seed, end); last = int(t * 12)
        im.save(os.path.join(d, "f%04d.png" % i))
    wav = os.path.join(tmp, name + ".wav"); write_wav(wav, audio)
    out = os.path.join(tmp, name + ".mp4")
    subprocess.run([FF, "-y", "-framerate", str(FPS), "-i", os.path.join(d, "f%04d.png"), "-i", wav,
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", "-c:a", "aac", "-b:a", "192k",
                    "-shortest", out], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return out


def probe_fps(path):
    """the take's own frame rate, as an integer; None if it cannot be read"""
    fp = shutil.which("ffprobe") or (os.path.join(os.path.dirname(FF), "ffprobe.exe") if FF else None)
    try:
        out = subprocess.run([fp, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=r_frame_rate",
                              "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip()
        n, _, d = out.partition("/")
        return max(1, int(round(float(n) / float(d or 1))))
    except Exception:
        return None


def probe_size(path):
    """the take's own width and height; None if it cannot be read (ROBOT NEKO ep 13)"""
    fp = shutil.which("ffprobe") or (os.path.join(os.path.dirname(FF), "ffprobe.exe") if FF else None)
    try:
        out = subprocess.run([fp, "-v", "error", "-select_streams", "v:0", "-show_entries", "stream=width,height",
                              "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip()
        w, h = [int(x) for x in out.split(",")[:2]]
        return (w, h) if w > 0 and h > 0 else None
    except Exception:
        return None


def main():
    global FPS
    ap = argparse.ArgumentParser()
    ap.add_argument("--ep", type=int, required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--in", dest="src", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--card", type=float, default=2.8)
    ap.add_argument("--endcard", type=float, default=1.6)
    ap.add_argument("--fps", type=int, default=0, help="output rate; default = the take's own")
    a = ap.parse_args()
    assert FF, "no ffmpeg"
    # ⛔ THE PACKAGE MUST KEEP THE TAKE'S RATE (ROBOT NEKO ep 9). FPS was a constant 24 and the concat
    # filter forced it, so ep 8 -- the first episode filmed at 60, after jacob's "we need to be filming
    # at 60fps, not 24" -- was delivered at 24, and so was ep 9's first package (eps 1-7 were shot at 24).
    # The cards expose on twelves at any rate (an exposure is 5 frames at 60).
    FPS = a.fps or probe_fps(a.src) or 24
    # ⛔ AND THE TAKE'S SIZE (ep 13). W/H were 1280x720 constants and the concat filter scaled every take to
    # them: ep 12 was filmed at 1080p and delivered at 720p in silence, and so was ep 13's first package. The
    # cards still draw at 1280x720 (their layout is in pixels); ffmpeg scales them up to the take.
    OW, OH = probe_size(a.src) or (W, H)
    print("packaging at %d fps, %dx%d (the take's own rate and size)" % (FPS, OW, OH))
    tmp = tempfile.mkdtemp(prefix="neko_")
    try:
        t = clip(tmp, "title", a.card, a.ep, a.title, a.ep, False, sting(a.card))
        e = clip(tmp, "end", a.endcard, a.ep, a.title, a.ep + 50, True, end_bleep(a.endcard))
        norm = ",".join(["scale=%d:%d:force_original_aspect_ratio=decrease" % (OW, OH),
                         "pad=%d:%d:(ow-iw)/2:(oh-ih)/2" % (OW, OH), "setsar=1", "fps=%d" % FPS])
        fc = ";".join(["[%d:v]%s[v%d];[%d:a]aresample=%d,aformat=channel_layouts=stereo[a%d]" % (k, norm, k, k, SR, k)
                       for k in range(3)]) + ";[v0][a0][v1][a1][v2][a2]concat=n=3:v=1:a=1[v][a]"
        subprocess.run([FF, "-y", "-i", t, "-i", a.src, "-i", e, "-filter_complex", fc, "-map", "[v]", "-map", "[a]",
                        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "19", "-preset", "veryfast",
                        "-c:a", "aac", "-b:a", "192k", a.out], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        # a still of the card, to LOOK at before anyone watches the episode
        card(2.0, a.card, a.ep, a.title, a.ep).save(os.path.splitext(a.out)[0] + "_card.png")
        print("wrote %s (%.1f MB)  + %s_card.png" % (a.out, os.path.getsize(a.out) / 1e6, os.path.splitext(a.out)[0]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
