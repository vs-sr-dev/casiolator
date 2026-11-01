#!/usr/bin/env python3
"""Casiolator - Little Romance (Casio Loopy) script extractor.

  lr_text.py ROM OUTDIR

ROM: the 3 MB cartridge, big-endian or byte-swapped, one file or IC104 + IC105
(a folder or the two files joined). Writes UTF-8 text files in OUTDIR:

  dialogue.txt   strings reached through 32-bit pointers, grouped by pointer array
                 (message boxes, answers, captions, menus)
  balloons.txt   the speech balloons of the manga panels (records of 0xb0 bytes)
  sfx.txt        the sound effects drawn in the panels
  names.txt      the default names of the six characters
  stray.txt      text no pointer reaches (to look at: found by offset, or unused)

Notes on the formats are in docs/little-romance.md.
"""
import io
import os
import struct
import sys

CART = 0x0e000000
SUM_START = 0x80                  # header checksum: 16-bit words from here
FONT_INDEX = 0x282cd8             # JIS row/cell -> glyph of the 12x12 font
FONT_GLYPHS = 1191
PANELS = (0x2a94b4, 0x2d8780)     # manga panel records
PANEL_SIZE = 0xb0
NAMES = 0x414b8                   # 6 x 10 bytes
SLASH, SPACE = b"\xa1\xbf", b"\xa1\xa1"


def load(path):
    if os.path.isdir(path):
        parts = {f.upper(): os.path.join(path, f) for f in os.listdir(path)}
        d = open(parts["IC104.BIN"], "rb").read() + open(parts["IC105.BIN"], "rb").read()
    else:
        d = open(path, "rb").read()
    if d[:2] != b"\x0e\x00":
        s = bytearray(len(d))
        s[0::2], s[1::2] = d[1::2], d[0::2]
        d = bytes(s)
    if len(d) != 0x300000 or d[:4] != b"\x0e\x00\x00\x80":
        sys.exit("not Little Romance (3 MB Loopy cartridge expected)")
    want = struct.unpack_from(">I", d, 8)[0]
    have = sum(struct.unpack_from(">%dH" % ((len(d) - SUM_START) // 2), d, SUM_START)) & 0xffffffff
    if have != want:
        sys.exit("checksum %08x, the header says %08x: bad dump. No-Intro's IC104 repeats its "
                 "first megabyte; use a good dump (the 2023 set, CRC32 550616f0)." % (have, want))
    return d


class Rom:
    def __init__(self, d):
        self.d = d
        self.index = struct.unpack_from(">%dH" % (83 * 94), d, FONT_INDEX)

    def glyph(self, c1, c2):
        if not (0xa1 <= c1 <= 0xfe and 0xa1 <= c2 <= 0xfe) or c1 - 0xa1 >= 83:
            return None
        return self.index[(c1 - 0xa1) * 94 + c2 - 0xa1]

    def valid(self, o):
        g = self.glyph(self.d[o], self.d[o + 1])
        return g is not None and (0 < g < FONT_GLYPHS or self.d[o:o + 2] == SPACE)

    def text(self, o, limit=None):
        """Characters from o up to the NUL (or limit bytes); None if not text."""
        e, end = o, o + limit if limit else len(self.d)
        while e + 1 < end and self.d[e] and self.valid(e):
            e += 2
        if e == o or (e < end and self.d[e] != 0):
            return None
        return self.d[o:e]


def read_script(path):
    """key -> English of a script file ('[key] ...' then 'JP ...' and 'EN ...' lines)."""
    en, key = {}, None
    if os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            line = line.rstrip("\n")
            if line.startswith("["):
                key = line[1:line.index("]")]
            elif line.startswith("EN ") and key and line[3:].strip():
                en[key] = line[3:]
    return en


class Out(io.StringIO):
    """A script file written on close, keeping the English already in the old one."""
    def __init__(self, path):
        super().__init__()
        self.path = path

    def __exit__(self, *exc):
        old, key, lines = read_script(self.path), None, []
        for line in self.getvalue().split("\n"):
            if line.startswith("["):
                key = line[1:line.index("]")]
            elif line == "EN " and key in old:
                line = "EN " + old[key]
            lines.append(line)
        with open(self.path, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        return super().__exit__(*exc)


def show(t):
    out = []
    for k in range(0, len(t), 2):
        c = t[k:k + 2]
        try:
            out.append(c.decode("euc_jp"))
        except UnicodeDecodeError:
            out.append("{%s}" % c.hex())
    return "".join(out)


def pointers(rom):
    """target -> list of places holding a pointer to it (2-byte aligned)."""
    d, refs = rom.d, {}
    for o in range(0, len(d) - 3, 2):
        v = struct.unpack_from(">I", d, o)[0] - CART
        if 0 <= v < len(d):
            refs.setdefault(v, []).append(o)
    return refs


def dialogue(rom, refs, out):
    found = {}
    for t, at in refs.items():
        if PANELS[0] <= t < PANELS[1] or NAMES <= t < NAMES + 60 or t < 0x30000:
            continue                      # panels and names have their own files; code
        s = rom.text(t)
        if s and (len(s) >= 4 or any(a % 4 == 0 for a in at)):
            found[t] = (s, at)
    # pointer arrays: runs of consecutive pointer words
    arrays = {}
    for t, (s, at) in found.items():
        arrays.setdefault(min(at), []).append(t)
    blocks, seen = [], set()
    for a in sorted(arrays):
        if a in seen:
            continue
        run, p = [], a
        while p in arrays:
            run += arrays[p]
            seen.add(p)
            p += 4
        blocks.append((a, sorted(set(run))))
    n = chars = 0
    with Out(out) as f:
        f.write("# Little Romance: text reached through pointers. ／ = new line, ＠ = player's name,\n"
                "# □■△▲▽▼ = the other names. Message boxes: 3 lines of 16 characters.\n\n")
        for a, ts in sorted(blocks, key=lambda b: b[1][0]):
            f.write("## pointers at %06x\n" % a)
            for t in ts:
                s, at = found[t]
                f.write("[%06x] refs %s\nJP %s\nEN \n" % (t, " ".join("%06x" % x for x in at), show(s)))
                n += 1
                chars += len(s) // 2
            f.write("\n")
    return n, chars, set(found)


def panels(rom, refs, out_b, out_s):
    d = rom.d
    recs = sorted(t for t in refs if PANELS[0] <= t < PANELS[1] and (t - PANELS[0]) % PANEL_SIZE == 0)
    first = {}
    nb = chars = 0
    with Out(out_b) as fb, Out(out_s) as fs:
        fb.write("# Little Romance: speech balloons. Vertical in Japanese: ／ starts a new column\n"
                 "# (right to left); 24 codes at most, the NUL included. 'shape' = balloon\n"
                 "# graphic, flags, x, y; 'grid' = columns x longest column.\n\n")
        fs.write("# Little Romance: sound effects in the panels, 8 codes (16 bytes, no NUL needed).\n\n")
        for r in recs:
            for slot, off in ((1, 0x30), (2, 0x64)):
                if d[r + off] == 0xff:
                    continue
                s = rom.text(r + off + 4, 48)
                if not s:
                    continue
                cols = show(s).split("／")
                key = "%06x.%d" % (r, slot)
                dup = first.setdefault(s, key)
                fb.write("[%s] shape %02x %02x %02x %02x grid %dx%d%s\nJP %s\nEN \n\n" % (
                    key, *d[r + off:r + off + 4], len(cols), max(len(c) for c in cols),
                    "" if dup == key else " same as " + dup, show(s)))
                nb += 1
                chars += sum(1 for k in range(0, len(s), 2) if s[k:k + 2] != SLASH)
            if d[r + 0x98] != 0xff:
                raw = d[r + 0xa0:r + 0xb0]
                s = raw[:raw.index(0)] if 0 in raw else raw
                fs.write("[%06x] %s\nJP %s\nEN \n\n" % (r, d[r + 0x98:r + 0x9c].hex(" "), show(s)))
    return len(recs), nb, chars


def names(rom, out):
    with Out(out) as f:
        f.write("# Little Romance: default names of the characters (□■△▲▽▼), 5 codes each.\n\n")
        for k, sym in enumerate("□■△▲▽▼"):
            o = NAMES + 10 * k
            s = rom.text(o, 10) or b""
            f.write("[%06x] %s\nJP %s\nEN \n\n" % (o, sym, show(s)))


def stray(rom, refs, known, out):
    d, n = rom.d, 0
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("# Text that no pointer reaches (outside the panel records): found by offset,\n"
                "# part of a bigger structure, or unused. To look at one by one.\n\n")
        o = 0
        while o < len(d) - 1:
            if PANELS[0] <= o < PANELS[1] or NAMES <= o < NAMES + 60:
                o += 2
                continue
            s = rom.text(o)
            if s and o in known:
                o += len(s) + 1
            elif s and len(s) >= 6 and (d[o - 1] == 0 or o % 2 == 0):
                f.write("[%06x] JP %s\n" % (o, show(s)))
                n += 1
                o += len(s) + 1
            else:
                o += 1
    return n


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rom = Rom(load(sys.argv[1]))
    os.makedirs(sys.argv[2], exist_ok=True)
    p = lambda name: os.path.join(sys.argv[2], name)
    refs = pointers(rom)
    n, c, known = dialogue(rom, refs, p("dialogue.txt"))
    print("dialogue: %d strings, %d characters" % (n, c))
    r, b, bc = panels(rom, refs, p("balloons.txt"), p("sfx.txt"))
    print("panels: %d records, %d balloons, %d characters" % (r, b, bc))
    names(rom, p("names.txt"))
    print("stray: %d strings" % stray(rom, refs, known, p("stray.txt")))


if __name__ == "__main__":
    main()
