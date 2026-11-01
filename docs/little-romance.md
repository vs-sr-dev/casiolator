# Little Romance: text, font and translation plan

Notes from session 3 (2026-11-01), the start of the English translation. Found by reading
the ROM, by disassembling with `build/shdis`, and by watching the game run with
`CL_WATCH` (CPU reads in an address range, with the PC). Addresses are ROM offsets
(the cartridge is at 0x0e000000). `tools/lr_text.py` extracts everything listed here.

## The dump

No-Intro's "Little Romance (Japan)" is a bad dump: its IC104.bin (2 MB) is the first
megabyte twice, so 0x100000-0x1fffff (graphics) is wrong. The header checksum (32-bit sum
of the big-endian 16-bit words from 0x80, stored at 0x08) says so: 0xaa38a780 instead of
0x6a410bb2. The 2023 set (one 3 MB file, CRC32 550616f0, "Little Romance.bin") matches the
header. The other No-Intro cartridges match their headers. The translation targets the
good dump only.

## Text engine

- Encoding: EUC-JP, NUL-terminated. Single bytes below 0x80 are skipped by the reader
  (0x0e00d478), so plain ASCII cannot be used as is.
- Control characters: `／` (a1bf) new line (a new column in balloons), `＠` (a1f7) the
  player's name (RAM 0x0900054c), `□■△▲▽▼` (a2a2-a2a7) the six other characters' names
  (RAM 0x09000506 + 10 n); 5 characters each. Substitution: 0x0e00d29c. Default names at
  0x414b8 (6 x 10 bytes: 真澄 直樹 千里 元幸 律子 幹央).
- Custom glyphs in JIS row 0x50 (d0a1-d0f1): heart, card suits, faces, hands, arrows,
  icon pictures used in the tutorial text (`倔` = "move" icon...), and `佰` (d0d1).
- Message box (0x0e00d608 -> 0x0e00d504): up to 3 lines of 16 characters (`mov #16,r6`
  at 0x0e00d630), automatic break at 16, `、` `。` may hang as the 17th. The box is 18
  cells wide. Longer texts page by 3 lines.

## Font

- 12x12 glyphs, one 16-bit word per row (bits 11-0), 24 bytes each, 1191 glyphs at
  0x27bd30. Index (JIS row/cell -> glyph number, 83 x 94 words) at 0x282cd8. Both are
  reached through the resource directory at 0x43000 (0x43020 font, 0x43024 index), so
  they can be moved.
- Order: punctuation, digits, A-Z, a-z, kana, the custom glyphs, then about 900 kanji
  in the game's own order. Drawn at a 12-pixel pitch with no gap.
- A second index at 0x2869d8 (0x4301c) covers kana and symbols only; not yet known where
  it is used.

## Where the text is (1221 + 1551 + 322 + 6 strings)

1. Strings reached through 32-bit pointers, 1221 strings, 39 400 characters:
   message boxes (record `50 00 13 nn`, the text, the answers, then the pointer array),
   captions and questions of the manga lessons (0x2eb000-0x2fa000), menus and tool names
   in the code banks (0x37f44, 0x414b8-0x42a79). All movable: write the new text
   anywhere and change the pointers.
2. Manga panels: 1086 records of 0xb0 bytes (0x2a94b4-0x2d8780), each reached through a
   pointer from the lesson nodes. Per record:
   - 0x00-0x2f characters on the panel (sprite, position, size)
   - 0x30 balloon 1: shape, flags, x, y; 0x34 text, 48 bytes
   - 0x64 balloon 2: same; 0x68 text, 48 bytes
   - 0x98 sound effect: 4 bytes; 0xa0 text, 16 bytes
   Shape 0xff = no balloon (the text after it is leftover and not shown). After the NUL
   the editor left old text: ignore it. 0x0e0124a8 copies a balloon to RAM, 24 codes at
   most, expanding the names. 1551 balloons (1467 different), 19 500 characters; 322
   different sound effects.
3. Balloons are written vertically: each `／` starts a column, right to left; empty
   columns pad the text to the balloon's middle. The balloon shapes (22 kinds) are
   drawn graphics, picked by the shape byte.
4. Only 10 strings are not reached by any pointer (debug screens, unused).
5. Graphics with Japanese text (to redraw): the zodiac buttons, the stats labels
   (知識 技術 感性 熱意 適性), the name-entry buttons (ひら カタ 漢 決定), the icon board
   and more: to survey screen by screen.

## Space

The 3 MB ROM has about 9 KB free. The cartridge space is 4 MB (0x0e000000-0x0e3fffff)
and the Floopy Drive flash cart is 32 Mbit, so the patch grows the ROM to 4 MB and puts
the new font, the English text and any new code in the last megabyte (header end address
and checksum updated).

## Balloons (found in session 3)

The balloon code can already draw horizontal text. Byte 1 of a balloon's header, bit 0x20:
set = vertical (every Japanese balloon has it), clear = rows from the top, and the game
draws a balloon of the right size around them. The chain: 0x0e0124a8 copies the balloon
to RAM; 0x0e00f07c builds a grid of glyphs (a `／` starts a new column, or a new row);
0x0e00ee1c sizes it (13 pixels a cell; width and height swap with the mode); 0x0e00eeb8
draws it (two branches, by the mode) into a buffer that goes to the VDP bitmap.
Byte 0 is the outline: 0 none, 3 shout, 4 oval with a tail, 7 box, 10 thought, 14 cloud
and more (22 in use). Bytes 2-3 are the centre, x and y.

The game checks the header checksum: a ROM with a wrong one takes another path.

## The patch (tools/lr_patch.py)

`lr_patch.py ROM translations/little-romance OUT.bin [OUT.bps]` reads the EN lines of
the script files and builds a 4 MB image:

- Font: a 6-pixel Latin font of our own (tools/lr_font.py); every pair of letters the
  text uses becomes a 12x12 glyph, given a JIS code the index leaves free (rows 0xb0
  up). The codes are kept in `pairs.txt` beside the script, which only grows: names in a
  battery save keep their meaning from build to build. The font (old glyphs, then the
  pairs) moves to 0x300000; the directory entry at 0x43020 points there.
- Message boxes: words wrapped at 33 letters (line length raised from 16 to 17 cells,
  `mov #16,r6` at 0x0e00d630; the box border hides the right half of the 17th cell).
  `|` new line, `||` new page (3 lines), `{d0d2}` a raw code, `＠□■…` names (counted as 5
  cells).
- Strings go after the font; every pointer to the old string is changed.
- Balloons: horizontal, one row per `|`, 23 codes at most; pitch 13 -> 12 pixels in the
  balloon code (0x0e00ee1e, ee52, eec6, f004) so pairs join evenly. The centre is moved
  to keep the balloon on screen; `{at=xx,yy}`, `{shape=nn}` and `{swap}` (the panel's
  two balloons trade places: English reads left to right) override.
- Default names: romaji, 4 codes.
- Header end address and checksum; a BPS patch against the good dump (about 33 KB now).

Tested in Casiolator: the title menu, the introduction, the profile questions and the
first panel (Masumi) in English.

## Still to do

1. Translate everything (Claude translates, the user plays and reviews).
2. Choice boxes, captions and menus: check their widths (wrapped like message boxes now).
3. Sound effects (16 bytes, 8 codes): probably the same balloon code; to try.
4. Graphics with Japanese text: survey the screens, redraw.
5. The player types the heroine's name on the ABC page in full-width letters (12 pixels
   each, 5 at most): fine, but could use the pairs later.
6. Casiolator: apply a BPS beside the game at load; warn about a bad header checksum.
