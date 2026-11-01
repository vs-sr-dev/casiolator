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

## Plan

1. Font: a 6-pixel-wide Latin font (letters, digits, punctuation), and glyphs made of two
   letters each ("dual-tile encoding"): every 12x12 cell shows two 6-pixel letters, so a
   line holds 32 letters (36 with the line length raised to 18, which the box allows).
   The text is then encoded as bigram codes chosen among the JIS codes the index leaves
   unused (about 4600); a script of this size needs about 1000-1500 bigrams. No change
   to the text engine; the text costs about one byte per letter, as Japanese costs about
   one byte per kana.
2. Message boxes: wrap words at the box width in the inserter, write `／` there.
3. Balloons: a code patch to draw them horizontally (rows top to bottom, centred) instead
   of columns; English lines must fit the balloon's width (columns x 12 pixels) and
   height (longest column x 12), and 23 codes. To find: the drawing routine that reads
   the RAM copy made by 0x0e0124a8.
4. Names: the six defaults romanised (Masumi, Naoki, Chisato, Motoyuki, Ritsuko, Mikio:
   readings to check). The player types the heroine's name on the ABC page, in full-width
   letters, 5 at most.
5. Graphics: redraw the buttons and labels.
6. Patch: a BPS file against the good dump, applied by Casiolator when it is beside the
   game, and usable with any patcher for the flash cart.
