#!/usr/bin/env python3
"""sfxgen.py -- CAPABILITY 3a: the club has no sound library, so make one.

    python reva_native/scenes/sfxgen.py          # writes scenes/sfx/*.wav

⛔ THE GAP. SEVENTY-FOUR has no sound in it but voices. Act V is *entirely* sound
design -- jacob: "sounds from the outside, screams... until there are no more screams. just
moans" -- and the last beat of the whole film is A KNOCK AT A DOOR, which is a sound, not a
line. There was no way to put one in.

⭐ AND MOST OF WHAT THIS FILM NEEDS IS SYNTHESISABLE. A knock is a damped impulse through a
wooden panel. A hammer is the same, harder and brighter. A sprinkler is a filtered noise
burst on a period. A car alarm is two square tones alternating. None of these need a
recording, none of them need a licence, and all of them are a few lines of arithmetic:

    knock        three taps, 180ms apart -- POLITE, which is the whole point of the ending
    hammer       a single strike, brighter and shorter than a knock
    sprinkler    a tick and a rising hiss; the first sound in the film
    car_alarm    two alternating tones, the thing act III happens over the top of
    glass        a bright noise burst with a long tail
    door_close   a low thud with a latch click on the end
    footstep     one soft step

⛔ WHAT IS NOT HERE, and is not being faked: SCREAMS and MOANS. Those are performances, not
waveforms -- synthesising them would produce something between a kettle and a joke, and act
V lives or dies on them. They need a real source. Written down rather than bodged.

Everything is mono 22050, which is what say_piper writes and therefore what
compile_scene.py can mix without resampling.
"""
import io, math, os, random, struct, sys, wave

SR = 22050
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "sfx")


def write(name, samples):
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, name + ".wav")
    w = wave.open(p, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(SR)
    clipped = [max(-32767, min(32767, int(v * 32767.0))) for v in samples]
    w.writeframes(struct.pack("<%dh" % len(clipped), *clipped))
    w.close()
    print("  %-12s %5.2fs" % (name, len(samples) / float(SR)))


def env(n, attack, decay):
    """attack/decay in seconds -> a per-sample envelope"""
    a = max(1, int(attack * SR))
    out = []
    for i in range(n):
        if i < a:
            out.append(i / float(a))
        else:
            out.append(math.exp(-(i - a) / (decay * SR)))
    return out


def tone(f, n, phase=0.0):
    return [math.sin(2 * math.pi * f * i / SR + phase) for i in range(n)]


def noise(n, rng):
    return [rng.uniform(-1, 1) for _ in range(n)]


def lowpass(x, a):
    y, prev = [], 0.0
    for v in x:
        prev += a * (v - prev)
        y.append(prev)
    return y


def tap(dur, f0, bright, rng, gain=1.0):
    """A damped impulse through a panel: a low body tone plus a short bright click."""
    n = int(dur * SR)
    e = env(n, 0.0008, dur * 0.28)
    body = tone(f0, n)
    body2 = tone(f0 * 1.94, n)
    click = lowpass(noise(n, rng), bright)
    return [gain * e[i] * (0.62 * body[i] + 0.22 * body2[i] + 0.30 * click[i])
            for i in range(n)]


def main():
    rng = random.Random(406)
    print("sfxgen -> %s" % OUT)

    # ⭐ THE KNOCK. The last thing in the film. Three taps, evenly spaced, unhurried --
    # POLITE, because the thing outside still remembers how to knock, and that is the
    # entire horror of it. Not banging. Not scratching. Knocking.
    gap = int(0.34 * SR)
    one = tap(0.40, 96.0, 0.35, rng, 0.85)
    n = gap * 2 + len(one) + int(0.6 * SR)
    k = [0.0] * n
    for j in range(3):
        o = j * gap
        for i, v in enumerate(one):
            if o + i < n:
                k[o + i] += v * (1.0, 0.92, 0.86)[j]
    write("knock", k)

    # a hammer: harder, brighter, shorter than a knock
    write("hammer", tap(0.22, 168.0, 0.62, rng, 0.95))

    # the sprinkler: a tick, then the hiss rising and falling. The FIRST sound in the film.
    n = int(1.5 * SR)
    hiss = lowpass(noise(n, rng), 0.18)
    out = []
    for i in range(n):
        t = i / float(n)
        swell = math.sin(math.pi * min(1.0, t * 1.15)) ** 1.6
        out.append(0.30 * hiss[i] * swell)
    tick = tap(0.10, 640.0, 0.85, rng, 0.55)
    for i, v in enumerate(tick):
        out[i] += v
    write("sprinkler", out)

    # a car alarm: two alternating tones. Act III happens over the top of this.
    n = int(2.4 * SR)
    out = []
    for i in range(n):
        f = 720.0 if (i // int(0.22 * SR)) % 2 == 0 else 960.0
        out.append(0.24 * math.sin(2 * math.pi * f * i / SR))
    e = env(n, 0.02, 4.0)
    write("car_alarm", [out[i] * e[i] for i in range(n)])

    # glass: a bright burst with a long tail
    n = int(1.1 * SR)
    g = noise(n, rng)
    e = env(n, 0.001, 0.22)
    hi = [g[i] - (g[i - 1] if i else 0.0) for i in range(n)]   # crude high-pass
    write("glass", [0.42 * hi[i] * e[i] for i in range(n)])

    # a door closing: a low thud and a latch
    n = int(0.55 * SR)
    e = env(n, 0.001, 0.10)
    body = tone(78.0, n)
    d = [0.7 * e[i] * body[i] for i in range(n)]
    latch = tap(0.09, 900.0, 0.9, rng, 0.35)
    o = int(0.09 * SR)
    for i, v in enumerate(latch):
        if o + i < n:
            d[o + i] += v
    write("door_close", d)

    # one soft footstep
    write("footstep", tap(0.16, 120.0, 0.30, rng, 0.34))

    print("\nNOT GENERATED, and deliberately not faked: screams and moans. Those are")
    print("performances, not waveforms, and act V lives or dies on them. Real source needed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
