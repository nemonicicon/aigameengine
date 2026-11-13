#!/usr/bin/env python3
"""tvfont_more.py -- the TV font past ASCII (queue step 26): accented Latin, Greek and Cyrillic, drawn in the font's own hand.

    python skate_src/author/tvfont_more.py [--sheet OUT.png]      writes reva_native/engine/tvfont2.inc, and a sheet of every glyph to look at

The TV font (engine/tvfont.inc) is ours: an 8 x 8 cell, stems two pixels wide because one is past composite video's bandwidth, capitals seven rows tall, small letters five
with their tails in the eighth. This file is the rest of it, in the same hand, and the source of engine/tvfont2.inc. Three ways a glyph is made:
  DRAWN     a letter with a shape of its own (a thorn, a Cyrillic zhe, a Greek xi): eight rows of art, below
  THE SAME  a letter that is another's shape (Cyrillic A is A, Greek omicron is o): named, never redrawn
  COMPOSED  a letter with a mark: the letter from the font, the mark laid over it. A small letter has two free rows above it for the mark. A capital hasn't, so it's
            pressed to five rows first (the rows it loses are the ones that repeat), which is how small-cell fonts have always done it
What it writes: every code point it covers, sorted, eight bytes of rows each; and the table that puts a letter and a combining mark after it together into one code point.
The engine turns the rows into rectangles at boot, as it does the first sheet's (tvFontInit).

⛔ Look at the sheet after changing anything here: a composed capital is only as good as the rows it lost.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE = os.path.normpath(os.path.join(HERE, "..", "..", "reva_native", "engine"))


def ascii_sheet():
    """the first sheet, out of engine/tvfont.inc: {character: 8 rows of 8}"""
    src = open(os.path.join(ENGINE, "tvfont.inc"), encoding="utf-8").read()
    rows = re.findall(r'^\s*"([.# ]{143})",', src, re.M)
    assert len(rows) == 48, "tvfont.inc has %d rows of art, not 48" % len(rows)
    out = {}
    for c in range(96):
        out[chr(32 + c)] = [rows[(c // 16) * 8 + y][(c % 16) * 9:(c % 16) * 9 + 8] for y in range(8)]
    return out


def art(text):
    """eight rows of up to eight, as written below"""
    rows = [r.strip() for r in text.strip().split("/")]
    assert len(rows) <= 8, text
    rows = [(r + "........")[:8] for r in rows] + ["........"] * (8 - len(rows))
    assert all(set(r) <= set("#.") for r in rows), text
    return rows


E = "........"
# ---------------------------------------------------------------- marks, for a small letter: rows 0 and 1, over a body in columns 0 to 5
MARK = {
    "grave": art("..##..../...##..."), "acute": art("...##.../..##...."), "circ": art("..###.../.##.##.."), "caron": art(".##.##../..###..."),
    "tilde": art(".###.#../.#.###.."), "diaer": art("##..##.."), "ring": art(".####.../.#..#..."), "macron": art(".####..."), "breve": art(".#..#.../..##...."),
    "dot": art("..##...."), "dacute": art("..##.##./.##.##.."), "dtonos": art("#..##.#./..##...."),
    "tick": art(".....##./....##.."),                                    # the caron of a tall letter (d, l, t), beside its top
    "upcomma": art("..##..../..##...."),                                 # Latvian's small g: its comma can't go under a tail, so it stands over the letter
    "cedilla": [E] * 7 + ["..##...."], "ogonek": [E] * 7 + ["....##.."], "comma": [E] * 7 + ["..##...."],
}

# ---------------------------------------------------------------- drawn: shapes of their own
DRAWN = {
    # Latin-1's signs
    0xA2: "...##.../.#####../##.##.../##.##.../##.##.../.#####../...##...", 0xA3: "..####../.##..##./.##...../#####.../.##...../.##...../#######.",
    0xA4: "......../##...##./.#####../.##.##../.#####../##...##.", 0xA5: "##..##../##..##../.####.../######../..##..../######../..##....",
    0xA6: "..##..../..##..../..##..../......../..##..../..##..../..##....", 0xA7: ".####.../##....../.####.../##..##../.####.../....##../.####...",
    0xA8: ".##.##..", 0xA9: ".#####../#.....#./#.###.#./#.#...#./#.###.#./#.....#./.#####..", 0xAA: ".####.../##..##../.#####../......../######..",
    0xAB: "......../..##.##./.##.##../##.##.../.##.##../..##.##.", 0xAC: "......../......../......../######../....##../....##..",
    0xAE: ".#####../#.....#./#.##..#./#.#.#.#./#.##..#./#.#.#.#./.#####..", 0xAF: "######..", 0xB0: ".###..../##.##.../.###....",
    0xB1: "......../..##..../######../..##..../......../######..", 0xB2: ".###..../...##.../..##..../.####...", 0xB3: ".###..../..##..../...##.../.###....",
    0xB4: "...##.../..##....", 0xB5: "......../......../##..##../##..##../##..##../##..##../#####.#./##......",
    0xB6: ".######./###.##../###.##../.##.##../..#.##../..#.##../..#.##..", 0xB7: "......../......../......../..##....", 0xB8: "......../......../......../......../......../......../...#..../..##....",
    0xB9: "..##..../.###..../..##..../.####...", 0xBA: ".###..../##.##.../.###..../......../#####...",
    0xBB: "......../##.##.../.##.##../..##.##./.##.##../##.##...",
    0xBC: ".#...#../##..#.../.#.#..../..#.#.#./.#..###./#.....#./......#.", 0xBD: ".#...#../##..#.../.#.#..../..#.##../.#....#./#...##../....###.",
    0xBE: "##...#../.##.#.../##.#..../..#.#.#./.#..###./#.....#./......#.",
    0xD7: "......../##...##./.##.##../..###.../.##.##../##...##.", 0xF7: "......../..##..../......../######../......../..##....",
    # Latin letters with shapes of their own
    0xC6: ".######./##.##.../##.##.../######../##.##.../##.##.../##.####.", 0xE6: "......../......../##.###../..##.##./.######./##.##.../.##.###.",
    0xD0: "#####.../##..##../##...##./####.##./##...##./##..##../#####...", 0xF0: ".##.#.../..###.../.#.##.../.####.../##..##../##..##../.####...",
    0xD8: ".#####../##..###./##..###./##.#.##./###..##./###..##./.#####..", 0xF8: "......../......../.####.#./##.###../###.##../##..##../#.####..",
    0xDE: "##....../######../##...##./##...##./######../##....../##......", 0xFE: "##....../##....../#####.../##..##../##..##../#####.../##....../##......",
    0xDF: ".####.../##..##../##.##.../##.###../##...##./##...##./##.###..",
    0x110: "#####.../##..##../##...##./####.##./##...##./##..##../#####...", 0x111: "....##../..#####./....##../.#####../##..##../##..##../.#####..",
    0x126: "##...##./#######./##...##./#######./##...##./##...##./##...##.", 0x127: "##....../####..../##....../#####.../##..##../##..##../##..##..",
    0x131: "......../......../.###..../..##..../..##..../..##..../.####...", 0x138: "......../......../##..##../##.##.../####..../##.##.../##..##..",
    0x13F: "##....../##....../##....../##..##../##....../##....../#######.", 0x140: ".###..../..##..../..##..../..##.##./..##..../..##..../.####...",
    0x141: "##....../##....../##.##.../####..../###...../##....../#######.", 0x142: ".###..../..##..../..###.../.###..../..##..../..##..../.####...",
    0x149: "#......./#......./..####../..##.##./..##.##./..##.##./..##.##.", 0x14A: "##...##./###..##./####.##./##.####./##..###./##...##./##...##./....##..",
    0x14B: "......../......../#####.../##..##../##..##../##..##../##..##../...##...",
    0x152: ".######./##.##.../##.##.../##.####./##.##.../##.##.../.######.", 0x153: "......../......../.#####../#.##.##./#.#####./#.##..../.#####..",
    0x166: "######../..##..../..##..../.####.../..##..../..##..../..##....", 0x167: ".##...../.##...../#####.../.##...../####..../.##.##../..###...",
    0x17F: "..###.../.##...../.##...../.##...../.##...../.##...../.##.....",
    0x132: "##..##../##..##../##..##../##..##../##..##../##..##../##.###..", 0x133: "##.##.../......../##.##.../##.##.../##.##.../##.##.../##.##.../..##....",
    # punctuation past Latin-1
    0x2013: "......../......../......../######..", 0x2014: "......../......../......../########", 0x2018: "..##..../.##...../.##.....", 0x2019: ".##...../.##...../##......",
    0x201A: "......../......../......../......../......../.##...../.##...../##......", 0x201C: ".##.##../##.##.../##.##...", 0x201D: "##.##.../##.##.../.#.#....",
    0x201E: "......../......../......../......../......../##.##.../##.##.../.#.#....", 0x2022: "......../......../..##..../.####.../..##....",
    0x2026: "......../......../......../......../......../......../#.#.#...", 0x20AC: "..####../.##...../#####.../.##...../#####.../.##...../..####..",
    0x2122: "###.#.#./.#..###./.#..#.#.",
    # Greek capitals
    0x393: "#######./##....../##....../##....../##....../##....../##......", 0x394: "...#..../..###.../..###.../.##.##../.##.##../##...##./#######.",
    0x398: ".#####../##...##./##...##./#######./##...##./##...##./.#####..", 0x39B: "..###.../..###.../.##.##../.##.##../##...##./##...##./##...##.",
    0x39E: "#######./......../......../.#####../......../......../#######.", 0x3A0: "#######./##...##./##...##./##...##./##...##./##...##./##...##.",
    0x3A3: "#######./.##...../..##..../...##.../..##..../.##...../#######.", 0x3A6: "...#..../.#####../##.#.##./##.#.##./##.#.##./.#####../...#....",
    0x3A8: "##.#.##./##.#.##./##.#.##./.#####../...#..../...#..../...#....", 0x3A9: ".#####../##...##./##...##./##...##./.##.##../.##.##../###.###.",
    # Greek small letters
    0x3B1: "......../......../.###.##./##.###../##..##../##.###../.###.##.", 0x3B2: ".####.../##..##../#####.../##..##../##..##../#####.../##....../##......",
    0x3B3: "......../......../##..##../##..##../.####.../..##..../..##..../..##....", 0x3B4: ".#####../.##...../..###.../.##.##../##..##../##..##../.####...",
    0x3B5: "......../......../.####.../##....../.###..../##....../.####...", 0x3B6: "######../...##.../..##..../.##...../##....../.####.../....##../...##...",
    0x3B7: "......../......../#####.../##..##../##..##../##..##../##..##../....##..", 0x3B8: ".####.../##..##../##..##../######../##..##../##..##../.####...",
    0x3B9: "......../......../.##...../.##...../.##...../.##.##../..###...", 0x3BB: "###...../..##..../..##..../.####.../.##.##../##..##../##..##..",
    0x3BD: "......../......../##..##../##..##../##..##../.####.../..##....", 0x3BE: ".####.../##....../.###..../##....../##....../.####.../....##../...##...",
    0x3C0: "......../......../#######./.##.##../.##.##../.##.##../.##.##..", 0x3C1: "......../......../.####.../##..##../##..##../#####.../##....../##......",
    0x3C2: "......../......../.####.../##....../##....../.####.../....##../...##...", 0x3C3: "......../......../.######./##.##.../##..##../##..##../.####...",
    0x3C4: "......../......../######../..##..../..##..../..##.#../...##...", 0x3C5: "......../......../##..##../##..##../##..##../##..##../.####...",
    0x3C6: "......../...#..../.#####../##.#.##./##.#.##./.#####../...#..../...#....", 0x3C7: "......../......../##...##./.##.##../..###.../.##.##../##...##.",
    0x3C8: "......../...#..../##.#.##./##.#.##./##.#.##./.#####../...#..../...#....", 0x3C9: "......../......../.#...#../##...##./##.#.##./##.#.##./.##.##..",
    # Cyrillic capitals
    0x402: "######../.##...../.##...../.#####../.##..##./.##..##./.##.##..", 0x404: ".#####../##...##./##....../#####.../##....../##...##./.#####..",
    0x409: ".###..../#.##..../#.##..../#.####../#.##.##./#.##.##./#.####..", 0x40A: "#.##..../#.##..../#.##..../######../#.##.##./#.##.##./#.####..",
    0x40B: "######../.##...../.##...../.#####../.##..##./.##..##./.##..##.", 0x40F: "##...##./##...##./##...##./##...##./##...##./#######./...#....",
    0x411: "######../##....../##....../######../##...##./##...##./######..", 0x414: "..####../.##.##../.##.##../.##.##../.##.##../#######./##...##.",
    0x416: "##.#.##./##.#.##./.#####../..###.../.#####../##.#.##./##.#.##.", 0x417: ".#####../##...##./.....##./..####../.....##./##...##./.#####..",
    0x418: "##...##./##..###./##.####./####.##./###..##./##...##./##...##.", 0x41B: "..#####./.##..##./.##..##./.##..##./.##..##./.##..##./##...##.",
    0x423: "##...##./##...##./##...##./.######./.....##./##...##./.#####..", 0x426: "##..##../##..##../##..##../##..##../##..##../#######./.....##.",
    0x427: "##...##./##...##./##...##./.######./.....##./.....##./.....##.", 0x428: "##.#.##./##.#.##./##.#.##./##.#.##./##.#.##./##.#.##./#######.",
    0x429: "##.#.#../##.#.#../##.#.#../##.#.#../##.#.#../#######./......#.", 0x42A: "###...../.##...../.##...../.#####../.##..##./.##..##./.#####..",
    0x42B: "##...##./##...##./##...##./####.##./##.#.##./##.#.##./####.##.", 0x42C: "##....../##....../##....../######../##...##./##...##./######..",
    0x42D: ".#####../##...##./.....##./..#####./.....##./##...##./.#####..", 0x42E: "#..###../#.##.##./#.##.##./####.##./#.##.##./#.##.##./#..###..",
    0x42F: ".######./##...##./##...##./.######./..##.##./.##..##./##...##.", 0x490: ".....##./#######./##....../##....../##....../##....../##......",
    # Cyrillic small letters
    0x431: "..####../.##...../#####.../##..##../##..##../##..##../.####...", 0x432: "......../......../#####.../##..##../#####.../##..##../#####...",
    0x433: "......../......../######../##....../##....../##....../##......", 0x434: "......../......../..####../.##.##../.##.##../#######./##...##.",
    0x436: "......../......../##.#.##./.#####../..###.../.#####../##.#.##.", 0x437: "......../......../.####.../....##../..###.../....##../#####...",
    0x438: "......../......../##..##../##.###../######../###.##../##..##..", 0x43B: "......../......../..####../.##.##../.##.##../.##.##../##..##..",
    0x43C: "......../......../##...##./###.###./#######./##.#.##./##...##.", 0x43D: "......../......../##..##../##..##../######../##..##../##..##..",
    0x43F: "......../......../######../##..##../##..##../##..##../##..##..", 0x442: "......../......../######../..##..../..##..../..##..../..##....",
    0x446: "......../......../##.##.../##.##.../##.##.../######../....##..", 0x447: "......../......../##..##../##..##../.#####../....##../....##..",
    0x448: "......../......../##.#.##./##.#.##./##.#.##./##.#.##./#######.", 0x449: "......../......../##.#.#../##.#.#../##.#.#../#######./......#.",
    0x44A: "......../......../###...../.##...../.####.../.##.##../.####...", 0x44B: "......../......../##...##./##...##./####.##./##.#.##./####.##.",
    0x44C: "......../......../##....../##....../#####.../##..##../#####...", 0x44D: "......../......../.####.../....##../..####../....##../.####...",
    0x44E: "......../......../#..###../#.##.##./####.##./#.##.##./#..###..", 0x44F: "......../......../.#####../##..##../.#####../.##.##../##..##..",
    0x452: ".##...../#####.../.##...../.####.../.##.##../.##.##../.##.##../....##..", 0x454: "......../......../.####.../##....../####..../##....../.####...",
    0x459: "......../......../.###..../#.##..../#.####../#.##.##./#.####..", 0x45A: "......../......../#.##..../#.##..../######../#.##.##./#.####..",
    0x45B: ".##...../#####.../.##...../.####.../.##.##../.##.##../.##.##..", 0x45F: "......../......../##..##../##..##../##..##../######../..##....",
    0x491: "......../....##../######../##....../##....../##....../##......",
}
# ---------------------------------------------------------------- the same shape as another
SAME = {
    0xA0: " ", 0xAD: "-", 0x2010: "-", 0x2011: "-", 0x2012: "-", 0x2212: "-", 0x2032: "'", 0x2033: '"', 0x2039: "<", 0x203A: ">", 0x3BC: 0xB5, 0x3BA: 0x138, 0x43A: 0x138, 0x444: 0x3C6,
    0x391: "A", 0x392: "B", 0x395: "E", 0x396: "Z", 0x397: "H", 0x399: "I", 0x39A: "K", 0x39C: "M", 0x39D: "N", 0x39F: "O", 0x3A1: "P", 0x3A4: "T", 0x3A5: "Y", 0x3A7: "X", 0x3BF: "o",
    0x405: "S", 0x406: "I", 0x408: "J", 0x410: "A", 0x412: "B", 0x415: "E", 0x41A: "K", 0x41C: "M", 0x41D: "H", 0x41E: "O", 0x420: "P", 0x421: "C", 0x422: "T", 0x425: "X",
    0x413: 0x393, 0x41F: 0x3A0, 0x424: 0x3A6,
    0x430: "a", 0x435: "e", 0x43E: "o", 0x440: "p", 0x441: "c", 0x443: "y", 0x445: "x", 0x455: "s", 0x456: "i", 0x458: "j",
}
# ---------------------------------------------------------------- upside down: Spanish opens a question and a shout with the mark turned over
FLIPPED = {0xA1: "!", 0xBF: "?"}
# ---------------------------------------------------------------- composed: a letter and a mark. Capitals first, then small letters
COMPOSED = {}


def comp(chars, mark, base=None):
    for k, ch in enumerate(chars):
        COMPOSED[ord(ch)] = ((base[k] if base else None), mark)


# the base of each composed letter is found from its Unicode name's own decomposition, below; these lists only say which exist and with which mark
comp("ÀÈÌÒÙàèìòùǸǹѐѝЀЍ", "grave")
comp("ÁÉÍÓÚÝáéíóúýĆćĹĺŃńŔŕŚśŹźάέήίόύώΆΈΉΊΌΎΏЃѓЌќ", "acute")
comp("ÂÊÎÔÛâêîôûĈĉĜĝĤĥĴĵŜŝŴŵŶŷ", "circ")
comp("ÃÑÕãñõĨĩŨũ", "tilde")
comp("ÄËÏÖÜäëïöüÿŸϊϋΪΫЁёЇї", "diaer")
comp("Åå Ůů".replace(" ", ""), "ring")
comp("ĀāĒēĪīŌōŪū", "macron")
comp("ĂăĔĕĞğĬĭŎŏŬŭЙйЎў", "breve")
comp("ĊċĖėĠġİŻż", "dot")
comp("ŐőŰű", "dacute")
comp("ČčĎĚěŇňŘřŠšŤŽž", "caron")
comp("ďľť", "tick")
comp("Ľ", "tick")
comp("ÇçŞşŢţ", "cedilla")
comp("ĄąĘęĮįŲų", "ogonek")
comp("ĢĶķĻļŅņŖŗȘșȚț", "comma")
comp("ģ", "upcomma")
comp("ΐΰ", "dtonos")
BASE = {  # what each composed letter is a mark on, where its own name doesn't say plainly
    "ά": "α", "έ": "ε", "ή": "η", "ί": "ι", "ό": "ο", "ύ": "υ", "ώ": "ω", "Ά": "Α", "Έ": "Ε", "Ή": "Η", "Ί": "Ι", "Ό": "Ο", "Ύ": "Υ", "Ώ": "Ω", "ϊ": "ι", "ϋ": "υ", "Ϊ": "Ι", "Ϋ": "Υ", "ΐ": "ι", "ΰ": "υ",
    "Ѓ": "Г", "ѓ": "г", "Ќ": "К", "ќ": "к", "Ё": "Е", "ё": "е", "Ї": "І", "ї": "ı", "Й": "И", "й": "и", "Ў": "У", "ў": "у", "ѐ": "е", "ѝ": "и", "Ѐ": "Е", "Ѝ": "И",
    "ï": "ı", "í": "ı", "ì": "ı", "î": "ı", "ĩ": "ı", "ī": "ı", "ĭ": "ı", "į": "i", "ĵ": "j", "ģ": "g", "İ": "I",
}


def glyph_of(cp, sheet, seen=()):
    """the eight rows of a code point, however it's made"""
    assert cp not in seen, "a loop at %04X" % cp
    if cp < 128:
        return list(sheet[chr(cp)])
    if cp in DRAWN:
        return art(DRAWN[cp])
    if cp in FLIPPED:
        g = list(sheet[FLIPPED[cp]])
        return [g[6 - y] for y in range(7)] + [g[7]]
    if cp in SAME:
        t = SAME[cp]
        return glyph_of(ord(t) if isinstance(t, str) else t, sheet, seen + (cp,))
    if cp in COMPOSED:
        import unicodedata
        ch = chr(cp)
        b = BASE.get(ch)
        if b is None:
            d = unicodedata.decomposition(ch).split()
            assert d and not d[0].startswith("<"), "%s (%04X) has no plain base: say it in BASE" % (ch, cp)
            b = chr(int(d[0], 16))
        return compose(glyph_of(ord(b), sheet, seen + (cp,)), COMPOSED[cp][1], b)
    raise KeyError("%04X isn't drawn" % cp)


def squash(rows):
    """a capital's seven rows pressed to five: the rows it loses are ones that repeat their neighbour, from the middle outward, and failing that its second and sixth"""
    r = list(rows[:7])
    for _ in range(2):
        cand = [i for i in range(1, len(r) - 1) if r[i] == r[i - 1] or r[i] == r[i + 1]]
        if cand:
            mid = (len(r) - 1) / 2.0
            r.pop(min(cand, key=lambda i: abs(i - mid)))
        else:
            r.pop(1 if len(r) == 7 else len(r) - 2)
    return r


def compose(rows, mark, base):
    m = MARK[mark]
    below = mark in ("cedilla", "ogonek", "comma")
    tall = any("#" in rows[y] for y in (0, 1))           # a capital, or a small letter with a stem up (b d h k l t)
    if below:
        out = list(rows)
    elif mark == "tick":
        out = [("".join("#" if a == "#" or b == "#" else "." for a, b in zip(rows[y], m[y])) if y < 2 else rows[y]) for y in range(8)]
        return out
    elif tall:
        out = [E, E] + squash(rows) + [rows[7]]
    else:
        out = list(rows)
    # the mark sits over the letter's own middle
    cols = [x for y in range(8) for x in range(8) if out[y][x] == "#" and (below or y >= 2)]
    mcols = [x for y in range(8) for x in range(8) if m[y][x] == "#"]
    shift = 0
    if cols and mcols and mark not in ("ogonek",):
        shift = int(round((min(cols) + max(cols)) / 2.0 - (min(mcols) + max(mcols)) / 2.0))
        shift = max(-min(mcols), min(7 - max(mcols), shift))
    res = []
    for y in range(8):
        row = list(out[y])
        for x in range(8):
            if m[y][x] == "#" and 0 <= x + shift < 8:
                row[x + shift] = "#"
        res.append("".join(row))
    return res


def runs(rows):
    """how many rectangles the engine will make of it (tvFontInit's own way)"""
    a = [list(r) for r in rows]
    n = 0
    for y in range(8):
        for x in range(8):
            if a[y][x] != "#":
                continue
            w = 1
            while x + w < 8 and a[y][x + w] == "#":
                w += 1
            h = 1
            while y + h < 8 and all(a[y + h][x + i] == "#" for i in range(w)):
                h += 1
            for j in range(h):
                for i in range(w):
                    a[y + j][x + i] = "."
            n += 1
    return n


def build():
    sheet = ascii_sheet()
    cps = sorted(set(DRAWN) | set(SAME) | set(COMPOSED) | set(FLIPPED))
    glyphs = {cp: glyph_of(cp, sheet) for cp in cps}
    worst = max(runs(g) for g in glyphs.values())
    assert worst <= 40, "a glyph is %d rectangles, and the engine keeps 40" % worst
    import unicodedata
    # a letter and a combining mark after it: every glyph Unicode says is exactly that, whichever mark it names (Latvian's comma letters are written with the cedilla's mark)
    pairs = []
    for cp in cps:
        d = unicodedata.decomposition(chr(cp)).split()
        if len(d) == 2 and not d[0].startswith("<") and 0x300 <= int(d[1], 16) < 0x370 and (int(d[0], 16) < 128 or int(d[0], 16) in glyphs):
            pairs.append((int(d[0], 16), int(d[1], 16), cp))
    return sheet, glyphs, pairs, worst


def write_inc(glyphs, pairs, worst):
    cps = sorted(glyphs)
    out = ["/* engine/tvfont2.inc -- THE TV FONT PAST ASCII (queue step 26): accented Latin, Greek and Cyrillic, in the font's own hand.",
           " * Written by skate_src/author/tvfont_more.py: the art is there, and this is its %d glyphs as rows of bits, the code points sorted. Edit that, not this. */" % len(cps),
           "#define TV_MORE %d" % len(cps),
           "#define TV_RUNS %d   /* the most rectangles a glyph of either sheet makes is %d */" % (40, worst),
           "static const unsigned short TV_MORE_CP[TV_MORE]={"]
    for i in range(0, len(cps), 16):
        out.append("    " + ",".join("0x%04X" % c for c in cps[i:i + 16]) + ",")
    out.append("};")
    out.append("static const unsigned char TV_MORE_BITS[TV_MORE][8]={")
    for i in range(0, len(cps), 4):
        out.append("    " + " ".join("{" + ",".join("0x%02X" % int(r.replace("#", "1").replace(".", "0"), 2) for r in glyphs[c]) + "}," for c in cps[i:i + 4]))
    out.append("};")
    out.append("/* a letter and a combining mark after it are one code point: {letter, mark, both} */")
    out.append("#define TV_PAIRS %d" % len(pairs))
    out.append("static const unsigned short TV_PAIR[TV_PAIRS][3]={")
    for i in range(0, len(pairs), 8):
        out.append("    " + " ".join("{0x%04X,0x%04X,0x%04X}," % p for p in pairs[i:i + 8]))
    out.append("};")
    with open(os.path.join(ENGINE, "tvfont2.inc"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def write_sheet(path, sheet, glyphs):
    from PIL import Image, ImageDraw
    cps = [ord(c) for c in sorted(sheet)] + sorted(glyphs)
    per, k = 32, 4
    rows = (len(cps) + per - 1) // per
    im = Image.new("RGB", (per * 9 * k + k, rows * (9 * k + 10) + k), (24, 26, 34))
    d = ImageDraw.Draw(im)
    for i, cp in enumerate(cps):
        g = sheet[chr(cp)] if cp < 128 else glyphs[cp]
        ox, oy = k + (i % per) * 9 * k, k + (i // per) * (9 * k + 10)
        d.rectangle([ox, oy, ox + 8 * k - 1, oy + 8 * k - 1], fill=(40, 44, 58))
        for y in range(8):
            for x in range(8):
                if g[y][x] == "#":
                    d.rectangle([ox + x * k, oy + y * k, ox + x * k + k - 1, oy + y * k + k - 1], fill=(240, 240, 232))
        if i % per == 0:
            d.text((ox, oy + 8 * k), "%04X" % cp, fill=(130, 140, 170))
    im.save(path)
    return path


def main(argv):
    sheet, glyphs, pairs, worst = build()
    write_inc(glyphs, pairs, worst)
    print("engine/tvfont2.inc: %d glyphs past ASCII (%d drawn, %d the same as another, %d a letter with a mark), %d letter-and-mark pairs; the busiest is %d rectangles"
          % (len(glyphs), len(DRAWN) + len(FLIPPED), len(SAME), len(COMPOSED), len(pairs), worst))
    if "--sheet" in argv:
        print("sheet: " + write_sheet(argv[argv.index("--sheet") + 1], sheet, glyphs))


if __name__ == "__main__":
    main(sys.argv)
