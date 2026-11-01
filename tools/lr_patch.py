#!/usr/bin/env python3
"""Casiolator - build the English Little Romance.

  lr_patch.py ROM SCRIPTDIR OUT.bin [OUT.bps]

Reads the EN lines of the script files made by lr_text.py (SCRIPTDIR), and writes a
4 MB cartridge image (big-endian) and, if asked, a BPS patch against the original.

English text markup:
  |        new line            ||       new page (message boxes show 3 lines)
  {d0d2}   a 2-byte code as is (custom glyphs: hearts, icons...)
  ＠ □■△▲▽▼ and any other full-width character: one 12-pixel cell, as in Japanese
Letters, digits and punctuation go two to a cell (lr_font.py); lines are wrapped at
the box width.
"""
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lr_font  # noqa: E402
import lr_text  # noqa: E402

CART = lr_text.CART
FONT_DIR = 0x43020               # resource directory entry of the 12x12 font
NEW_SIZE = 0x400000
FREE = 0x300000                  # the added megabyte
LINE_CODES = 17                  # message box line, in 12-pixel cells (16 in Japanese)
LINE_HALVES = 2 * LINE_CODES - 1  # the border hides the right half of the 17th cell
LINE_PATCH = 0xd630              # mov #16,r6: the line length the message box asks for
NAME_CELLS = 5                   # a name code can grow to 5 cells
NAMES = "＠□■△▲▽▼"
# balloon text pitch, 13 pixels in Japanese: 12 so that letter pairs join evenly
# (mov #13 -> #12 at each place the balloon code uses it: size, columns, rows)
PITCH = [(0xee1e, 0xe3), (0xee52, 0xe4), (0xeec6, 0xeb), (0xf004, 0x72)]
SCREEN = (256, 224)
BALLOON_PAD = 8                  # the balloon is an ellipse around the text: about
                                 # 1.4 times the text, and this, each side
SLASH = b"\xa1\xbf"


def entries(path):
    """[(key, refs, english)] of a script file, in order."""
    out, cur = [], None
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line.startswith("["):
            key = line[1:line.index("]")]
            rest = line[line.index("]") + 1:].split()
            refs = [int(x, 16) for x in rest[1:]] if rest[:1] == ["refs"] else []
            cur = [key, refs, ""]
            out.append(cur)
        elif line.startswith("EN ") and cur:
            cur[2] = line[3:].strip()
    return out


class Encoder:
    """Turns English into codes: pairs of letters get a glyph each, made on demand.

    The codes are kept in a file (pairs.txt, "code ab" per line) that only grows: a
    pair keeps its code from build to build, so names in a battery save stay right."""

    def __init__(self, rom, path):
        self.rom, self.path = rom, path
        self.codes = {}              # (a, b) -> 2-byte code
        self.glyphs = []             # (JIS row/cell number, glyph), in index order
        if os.path.exists(path):
            for line in open(path, encoding="utf-8"):
                line = line.rstrip("\n")
                if len(line) == 7:
                    self.add(bytes.fromhex(line[:4]), line[5], line[6])
        used = {(c[0] - 0xa1) * 94 + c[1] - 0xa1 for c in self.codes.values()}
        self.free = [k for k in range(15 * 94, 83 * 94) if rom.index[k] == 0 and k not in used]

    def add(self, code, a, b):
        self.codes[(a, b)] = code
        self.glyphs.append(((code[0] - 0xa1) * 94 + code[1] - 0xa1, lr_font.pair(a, b)))

    def pair(self, a, b):
        if (a, b) not in self.codes:
            if not self.free:
                sys.exit("no JIS codes left for letter pairs")
            k = self.free.pop(0)
            code = bytes([0xa1 + k // 94, 0xa1 + k % 94])
            self.add(code, a, b)
            with open(self.path, "a", encoding="utf-8", newline="\n") as f:
                f.write("%s %s%s\n" % (code.hex(), a, b))
        return self.codes[(a, b)]

    def tokens(self, text):
        """[(kind, value)]: ('h', letter) half cell, ('f', code) full cell,
        ('br', 1) new line, ('br', 3) new page, ('sp', ' ')."""
        text = text.replace("...", "…").replace("’", "'").replace("‘", "'")
        text = text.replace("“", '"').replace("”", '"')
        out, i = [], 0
        while i < len(text):
            c = text[i]
            if text.startswith("||", i):
                out.append(("br", 3))
                i += 2
                continue
            if c == "|":
                out.append(("br", 1))
            elif c == "{":
                j = text.index("}", i)
                out.append(("f", bytes.fromhex(text[i + 1:j])))
                i = j
            elif c == " ":
                out.append(("sp", " "))
            elif c in lr_font.GLYPHS:
                out.append(("h", c))
            else:
                code = c.encode("euc_jp")
                if len(code) != 2:
                    sys.exit("cannot write %r" % c)
                out.append(("f", code))
            i += 1
        return out

    @staticmethod
    def width(tok):
        if tok[0] == "f":
            return 2 * NAME_CELLS if tok[1].decode("euc_jp", "replace") in NAMES else 2
        return 1

    def line_width(self, ts):
        """Half cells a line takes; a full cell starts on a cell boundary."""
        w = 0
        for t in ts:
            if t[0] == "f":
                w += (w & 1) + self.width(t)
            else:
                w += 1
        return w

    def wrap(self, toks, halves):
        """Lines (lists of tokens) no wider than halves; ('page',) marks a page break."""
        lines, line, word = [], [], []
        width = self.line_width

        def flush_word():
            nonlocal line, word
            if not word:
                return
            sep = [("sp", " ")] if line else []
            if width(line + sep + word) > halves and line:
                lines.append(line)
                line, sep = [], []
            line = line + sep + word
            while width(line) > halves:         # a word longer than the line
                cut = 0
                while width(line[:cut + 1]) <= halves:
                    cut += 1
                lines.append(line[:cut])
                line = line[cut:]
            word = []

        for t in toks:
            if t[0] == "sp":
                flush_word()
            elif t[0] == "br":
                flush_word()
                lines.append(line)
                line = []
                if t[1] == 3:
                    lines.append(("page",))
            else:
                word.append(t)
        flush_word()
        if line:
            lines.append(line)
        return lines

    def encode_line(self, line):
        out, half = b"", None
        for kind, v in line:
            if kind == "f":
                if half is not None:
                    out += self.pair(half, " ")
                    half = None
                out += v
            elif half is None:
                half = v
            else:
                out += self.pair(half, v)
                half = None
        if half is not None:
            out += self.pair(half, " ")
        return out

    def encode(self, text, halves=LINE_HALVES, page=3):
        lines, out = self.wrap(self.tokens(text), halves), []
        for line in lines:
            if line == ("page",):
                while len(out) % page:
                    out.append(b"")
            else:
                out.append(self.encode_line(line))
        while out and out[-1] == b"":
            out.pop()
        return SLASH.join(out)


def bps(source, target):
    """A BPS patch: copy from the source where the bytes agree, runs of one byte value
    as a byte and a copy of it, literal bytes elsewhere."""
    def num(v):
        o = bytearray()
        while True:
            x = v & 0x7f
            v >>= 7
            if not v:
                o.append(0x80 | x)
                return bytes(o)
            o.append(x)
            v -= 1

    def same(j):
        return j < len(source) and source[j] == target[j]

    def run(j):
        k = j
        while k < n and target[k] == target[j] and k - j < 0x10000:
            k += 1
        return k - j

    out = bytearray(b"BPS1") + num(len(source)) + num(len(target)) + num(0)
    i, n, rel = 0, len(target), 0
    while i < n:
        j = i
        if same(i):
            while j < n and same(j):
                j += 1
            out += num(((j - i - 1) << 2) | 0)                    # SourceRead
        elif run(i) >= 16:
            k = run(i)
            off = i - rel
            out += num(1) + target[i:i + 1]                        # TargetRead, 1 byte
            out += num(((k - 2) << 2) | 3) + num((abs(off) << 1) | (off < 0))  # TargetCopy
            rel = i + k - 1
            j = i + k
        else:
            while j < n and not same(j) and run(j) < 16:
                j += 1
            out += num(((j - i - 1) << 2) | 1) + target[i:j]       # TargetRead
        i = j
    out += struct.pack("<II", zlib.crc32(source), zlib.crc32(target))
    out += struct.pack("<I", zlib.crc32(out))
    return bytes(out)


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    src = lr_text.load(sys.argv[1])
    rom = lr_text.Rom(src)
    enc = Encoder(rom, os.path.join(sys.argv[2], "pairs.txt"))
    d = bytearray(src) + b"\xff" * (NEW_SIZE - len(src))

    # text first: the font size depends on the letter pairs used
    script = lambda name: entries(os.path.join(sys.argv[2], name))
    texts = []
    for key, refs, en in script("dialogue.txt"):
        if en:
            texts.append((refs, enc.encode(en)))

    # balloons: drawn horizontally (flag 0x20 off: the game draws the text in rows and
    # a balloon that fits); one row per |, 23 codes and the NUL at most.
    # {at=xx,yy} places the balloon's centre, {shape=nn} picks another outline (hex, as
    # in the record: 0 none, 3 shout, 4 oval with a tail, 7 box, 10 thought...)
    balloons = 0
    todo = [(k, en) for k, _, en in script("balloons.txt") if en]
    for key, en in todo:
        # {swap}: the two balloons of the panel trade places (Japanese reads the right
        # one first; English wants it on the left)
        if "{swap}" in en:
            r = int(key.split(".")[0], 16)
            d[r + 0x32:r + 0x34], d[r + 0x66:r + 0x68] = d[r + 0x66:r + 0x68], d[r + 0x32:r + 0x34]
    for key, en in todo:
        rec, slot = key.split(".")
        o = int(rec, 16) + (0x30 if slot == "1" else 0x64)
        opts = dict(re.findall(r"\{(at|shape|swap)=?([0-9a-fA-F,]*)\}", en))
        en = re.sub(r"\{(at|shape|swap)=?[0-9a-fA-F,]*\}\s*", "", en)
        code = enc.encode(en, halves=99, page=1)
        if len(code) > 46:
            sys.exit("balloon %s: %d codes, 23 at most" % (key, len(code) // 2))
        rows = code.split(SLASH)
        if "shape" in opts:
            d[o] = int(opts["shape"], 16)
        if "at" in opts:
            d[o + 2:o + 4] = bytes(int(v, 16) for v in opts["at"].split(","))
        else:
            # x, y are the balloon's centre: keep it on the screen
            for axis, size in ((0, max(len(r) for r in rows) // 2 * 12), (1, len(rows) * 12)):
                half = size * 7 // 10 + BALLOON_PAD
                d[o + 2 + axis] = min(max(d[o + 2 + axis], half), SCREEN[axis] - half)
        d[o + 1] &= ~0x20
        d[o + 4:o + 52] = code + bytes(48 - len(code))
        balloons += 1

    # default names: 4 codes and the NUL
    for key, _, en in script("names.txt"):
        if en:
            code = enc.encode(en, halves=8)
            o = int(key, 16)
            d[o:o + 10] = code + bytes(10 - len(code))

    # font: the old glyphs, then the pairs, in the added megabyte
    font = FREE
    old = src[0x27bd30:0x27bd30 + 24 * lr_text.FONT_GLYPHS]
    d[font:font + len(old)] = old
    for n, (k, g) in enumerate(enc.glyphs):
        o = font + len(old) + 24 * n
        d[o:o + 24] = struct.pack(">12H", *g)
        struct.pack_into(">H", d, lr_text.FONT_INDEX + 2 * k, lr_text.FONT_GLYPHS + n)
    struct.pack_into(">I", d, FONT_DIR, CART + font)
    at = (font + len(old) + 24 * len(enc.glyphs) + 15) & ~15

    for refs, code in texts:
        d[at:at + len(code) + 1] = code + b"\0"
        for r in refs:
            struct.pack_into(">I", d, r, CART + at)
        at = (at + len(code) + 2) & ~1
    if at > NEW_SIZE:
        sys.exit("out of space")

    # message box lines: 17 cells (the line buffer holds 17 codes and the NUL; the
    # 18th cell that 、 and 。 could add never comes, English has no such codes)
    assert d[LINE_PATCH:LINE_PATCH + 2] == bytes([0xe6, 0x10])
    d[LINE_PATCH + 1] = LINE_CODES

    for o, op in PITCH:
        assert d[o:o + 2] == bytes([op, 13])
        d[o + 1] = 12

    # header: end of ROM, checksum
    struct.pack_into(">I", d, 4, CART + NEW_SIZE - 2)
    total = sum(struct.unpack_from(">%dH" % ((NEW_SIZE - 0x80) // 2), d, 0x80)) & 0xffffffff
    struct.pack_into(">I", d, 8, total)
    open(sys.argv[3], "wb").write(d)
    if len(sys.argv) == 5:
        open(sys.argv[4], "wb").write(bps(src, bytes(d)))
    print("%d strings, %d balloons, %d letter pairs, %d KB used of the added megabyte"
          % (len(texts), balloons, len(enc.glyphs), (at - FREE) // 1024))


if __name__ == "__main__":
    main()
