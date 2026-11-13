"""foley_game.py -- the GAME's sound effects, procedural, into the same shelf foley.py fills.

    python skate_src/robot_neko/foley_game.py            # all of them
    python skate_src/robot_neko/foley_game.py shot hit   # some

The engine's mixer (09-11) plays these by name from reva_native/scenes/sfx: shot, swing,
hit, kill, pickup, hurt, ui_move, ui_confirm, banner. Same format and writer as foley.py
(22050 Hz mono 16-bit, normalised to -1 dBFS; the mixer resamples and the play call scales).
"""
import math, random, sys
from foley import write, SR


def _env(t, a, d):
    return min(1.0, t / max(1e-6, a)) * math.exp(-t / d)


def _noise(rnd, n):
    return [rnd.uniform(-1, 1) for _ in range(n)]


def _lp(xs, alpha):
    y, out = 0.0, []
    for x in xs:
        y += alpha * (x - y)
        out.append(y)
    return out


def shot():
    rnd = random.Random(1)
    n = int(SR * 0.22)
    nz = _lp(_noise(rnd, n), 0.35)
    out = []
    for i in range(n):
        t = i / SR
        crack = nz[i] * _env(t, 0.001, 0.028)
        thump = math.sin(2 * math.pi * (95 - 40 * t) * t) * _env(t, 0.002, 0.07) * 0.9
        out.append(crack * 1.1 + thump)
    return out


def swing():
    rnd = random.Random(2)
    n = int(SR * 0.30)
    raw = _noise(rnd, n)
    out, y = [], 0.0
    for i in range(n):
        t = i / SR
        a = 0.04 + 0.30 * math.sin(math.pi * t / 0.30)   # the filter opens as the arc passes
        y += a * (raw[i] - y)
        out.append(y * math.sin(math.pi * t / 0.30) ** 1.5)
    return out


def hit():
    rnd = random.Random(3)
    n = int(SR * 0.16)
    nz = _lp(_noise(rnd, n), 0.5)
    return [math.sin(2 * math.pi * 120 * (i / SR)) * _env(i / SR, 0.002, 0.06) * 0.9
            + nz[i] * _env(i / SR, 0.001, 0.012) * 0.8 for i in range(n)]


def kill():
    rnd = random.Random(4)
    n = int(SR * 0.28)
    nz = _lp(_noise(rnd, n), 0.25)
    out = []
    for i in range(n):
        t = i / SR
        f = 420 * math.exp(-t / 0.12) + 60
        saw = (2 * ((f * t) % 1.0) - 1) * 0.5
        out.append(saw * _env(t, 0.003, 0.11) + nz[i] * _env(t, 0.001, 0.05) * 0.5)
    return out


def pickup():
    def tone(f, dur):
        return [(math.sin(2 * math.pi * f * (i / SR)) + 0.3 * math.sin(4 * math.pi * f * (i / SR)))
                * _env(i / SR, 0.004, dur * 0.5) for i in range(int(SR * dur))]
    return tone(660.0, 0.08) + tone(990.0, 0.14)


def hurt():
    rnd = random.Random(5)
    n = int(SR * 0.22)
    nz = _lp(_noise(rnd, n), 0.3)
    out = []
    for i in range(n):
        t = i / SR
        thud = math.sin(2 * math.pi * 70 * t) * _env(t, 0.002, 0.10)
        chirp = math.sin(2 * math.pi * (320 - 900 * t) * t) * _env(t, 0.002, 0.05) * 0.5
        out.append(thud + chirp + nz[i] * _env(t, 0.001, 0.03) * 0.6)
    return out


def ui_move():
    n = int(SR * 0.05)
    return [math.sin(2 * math.pi * 2200 * (i / SR)) * _env(i / SR, 0.0005, 0.012) for i in range(n)]


def ui_confirm():
    def tone(f, dur):
        return [math.sin(2 * math.pi * f * (i / SR)) * _env(i / SR, 0.003, dur * 0.45) for i in range(int(SR * dur))]
    return tone(880.0, 0.06) + tone(1320.0, 0.12)


def banner():
    n = int(SR * 0.35)
    return [(math.sin(2 * math.pi * 523.25 * (i / SR)) + 0.2 * math.sin(2 * math.pi * 1046.5 * (i / SR)))
            * _env(i / SR, 0.004, 0.14) * 0.6 for i in range(n)]


MAKERS = {"shot": shot, "swing": swing, "hit": hit, "kill": kill, "pickup": pickup,
          "hurt": hurt, "ui_move": ui_move, "ui_confirm": ui_confirm, "banner": banner}

if __name__ == "__main__":
    for nm in (sys.argv[1:] or list(MAKERS)):
        write(nm, MAKERS[nm]())
