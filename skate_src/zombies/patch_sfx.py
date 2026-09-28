"""patch_sfx.py -- CAPABILITY 3b: a scene can carry SOUND, not just voices.

⭐ AND THE ENGINE NEEDS NO CHANGE AT ALL. compile_scene.py already mixes N line-wavs into
one track at time offsets, and the engine already plays that one track and muxes it against
the picture. A sound effect is just another source at another offset. The whole capability
is a parser and a few lines of mixing.

    sfx <t> <name> [gain]      # anywhere in a scene .txt

`t` is scene seconds -- absolute, not relative to a line, because a sound is placed against
the CUT and not against whoever happens to be speaking. Sources come from scenes/sfx/,
written by sfxgen.py.

⛔ AND THE SFX MUST NOT MOVE WHEN THE DIALOGUE DOES. Line offsets are computed by
accumulating durations, so re-voicing a scene shifts every line after the one that changed.
An `sfx` at t=41.2 stays at 41.2 -- which is what makes it possible to place the knock at
the end of act V and have it survive a re-record.
"""
import io, sys

P = r"C:\MyVeryOwnVideoGames\reva\reva_native\scenes\compile_scene.py"
s = io.open(P, encoding="utf-8").read()


def sub(a, b, why):
    global s
    if s.count(a) != 1:
        print("!! anchor x%d for %s" % (s.count(a), why))
        sys.exit(1)
    s = s.replace(a, b)
    print("   ok: %s" % why)


# ---- parse `sfx <t> <name> [gain]` --------------------------------------------------
sub("""        if s.startswith("cast "):""",
    """        # ⭐ A SOUND EFFECT. `sfx <t> <name> [gain]`, in ABSOLUTE scene seconds -- a sound
        # is placed against the cut, not against whoever is speaking, so re-voicing a line
        # cannot drag the knock at the end of act V somewhere else.
        if s.startswith("sfx "):
            bits = s.split()
            if len(bits) >= 3:
                sfx.append((float(bits[1]), bits[2],
                            float(bits[3]) if len(bits) > 3 else 1.0))
            continue
        if s.startswith("cast "):""",
    "parse sfx lines")

sub("""    stage = dict(STAGE0)""",
    """    sfx = []
    stage = dict(STAGE0)""",
    "the sfx list")

# ---- mix them into the same buffer --------------------------------------------------
sub("""    wo = wave.open(os.path.join(here, out + ".wav"), "wb")""",
    """    # ⭐ THE EFFECTS, mixed into the same buffer the voices are in. No engine change is
    # needed for any of this: the engine already plays one wav and muxes it against the
    # picture, so a sound effect is a source at an offset like every line is.
    sdir = os.path.join(here, "sfx")
    for (st, nm, gain) in sfx:
        sp = os.path.join(sdir, nm + ".wav")
        if not os.path.exists(sp):
            print("  !! sfx '%s' not found (%s)" % (nm, sp))
            continue
        sw = wave.open(sp, "rb")
        if (sw.getframerate(), sw.getnchannels(), sw.getsampwidth()) != (sr, nch, 2):
            print("  !! sfx '%s' is %dHz x%d, scene is %dHz x%d -- skipped"
                  % (nm, sw.getframerate(), sw.getnchannels(), sr, nch))
            sw.close()
            continue
        sd = sw.readframes(sw.getnframes())
        sw.close()
        vals = struct.unpack("<%dh" % (len(sd) // 2), sd)
        off = int(st * sr) * nch
        for k, v in enumerate(vals):
            if 0 <= off + k < len(buf):
                buf[off + k] = max(-32768, min(32767, buf[off + k] + int(v * gain)))
        print("  sfx  %6.2fs  %-12s x%.2f" % (st, nm, gain))
    wo = wave.open(os.path.join(here, out + ".wav"), "wb")""",
    "mix the effects in")

io.open(P, "w", encoding="utf-8").write(s)
print("compile_scene.py: scenes carry sound")
