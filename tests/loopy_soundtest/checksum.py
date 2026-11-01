#!/usr/bin/env python3
# Loopy cartridge checksum (kasami's ROM Structure.md): 32-bit sum of the big-endian words
# from the first word after the header to the last word of the ROM, stored at offset 8.
import struct, sys

with open(sys.argv[1], "r+b") as f:
    rom = f.read()
    first, last = struct.unpack(">LL", rom[:8])
    body = rom[first - 0x0e000000:last - 0x0e000000 + 2]
    s = sum(struct.unpack(">%dH" % (len(body) // 2), body)) & 0xffffffff
    f.seek(8)
    f.write(struct.pack(">L", s))
