# TODO

## Done

### Session 1 (2026-11-01)

- Z80 core (src/z80.c): zexdoc and zexall all OK.
- PV-1000 (src/pv1000.c): video per line, BUSREQ stalls on the tile lines, matrix and
  prerender interrupts, 3 square waves with the resistor mix and ring modulation,
  2 joysticks. All 13 games play; user-tested: speed, sound, pad, two players.
- Frontend (src/main.c): start screen with no arguments, drag and drop, file dialog,
  sound as the clock, controllers with hotplug (keyboard takes the first player without
  a controller), pause, reset, screenshots, full screen, window size, `casiolator.ini`;
  headless mode with `--press`, `--screenshot`, `--wav`, `--bios`.
- Cartridge loading (src/game.c, src/zip.c): .bin or .zip (own inflater), Loopy byte order
  detected, split IC104/IC105 chips joined (zip or folder), headerless Magical Shop
  (byte order from NOP counts + kasami's header), clear messages for a whole collection
  zip or a non-cartridge.
- Casio Loopy, clean room (src/sh1.c, src/sh7021.c, src/loopy.c, src/vdp.c):
  - SH-1 from Hitachi's programming manual; SH7021 peripherals checked against the
    SH7020/21 hardware manual (both in refs/, not in git)
  - VDP from kasami's Megadoc: tilemaps, bitmaps (colour latch, flash fill), objects,
    screens A/B, the 16 priority orders, blend modes
  - all 12 cartridges start and respond (Chakrakun from D:\Homebrew2\CL\games); battery
    saves in saves/; the BIOS (hd6437021.lsi302, CRC 8c57ff9f) found beside the exe or
    the game, else asked once; 7.5x real time
  - user-tested in the window: Loopy Town's cursor fixed (OBJ format bit also on OBJ1)
- Debug aids: CL_TRACE=n (instruction trace), CL_LOG=1 (unknown I/O, SRAM writes, PC at
  each frame end), CL_MIDI=file (bytes to the sound chip and SOUND_CTRL writes, timed),
  CL_DUMP=file + CL_DUMP_FRAME=n (VDP memories and registers), build/shdis (disassembler).

### Session 2 (2026-11-01)

- Loopy sound, clean room (src/upd937.c, docs/loopy-sound.md): the whole sample ROM layout
  decoded (programs, split maps, zones of two layers, samples with key/start/end/loop,
  envelopes with a release half, pitch modulation, the rate, amplitude and pitch tables);
  MIDI parser with running status, modes and voices per channel (6/4/2/4, 3 channels,
  12), two-zone programs taking two voices, sustain pedal, pitch bend, the volume sliders,
  layers left and right, linear interpolation, the output filter from kasami's
  schematics. 84863 Hz stereo; the frontend went stereo (core.audio_channels).
- Second ROM support in the frontend (core.load_aux): the sample ROM is looked for beside
  the executable, the game and the BIOS (also in casloopy.zip), then asked for once;
  `--samples`, `loopy_sound` in casiolator.ini. Without it the Loopy is silent.
- SH7021 DMA fix: a DMA write to SCI TDR asked for the next byte before the channel had
  moved on, so the second byte of every BIOS MIDI message repeated the first (lost
  program changes and note-offs).
- A program change ends the channel's notes (the BIOS changes tracks that way; user found
  notes hanging across screens).
- Test cartridge tests/loopy_soundtest (all programs, range, bend, sustain, velocity,
  polyphony, volumes); recorded in the SR2 MAME fork as a black box
  (byte-swapped cartridge; retail games are silent there: its SCI1 does not take DMA).

## Rules for the Loopy (user, 2026-11-01)

Clean room, MIT. Never read LoopyMSE's or MAME's casloopy source, nor the SR2 MAME-fork
uPD937 (it was ported from LoopyMSE). Allowed: kasami's Megadoc and Loopy-Tools
(D:\Homebrew2\CL\docs), Loopy-Schematics, patent US5883611, Hitachi/NEC manuals, our own
experiments. Other emulators only as black boxes (run them, compare pictures/recordings).

## Next: Loopy sound, second pass

1. **A recording of the real console** playing tests/loopy_soundtest (needs a flash
   cartridge), or at least of game music (boot jingle, title screens) to compare by ear
   and by spectrum. It settles the open questions in docs/loopy-sound.md: envelope time
   base (now one step per output frame, rate table entry 2 * rate), pitch modulation
   speed and delay units, `(n, 00)` envelope pairs, interpolation, the 8 cents.
2. User listening pass in the window: wrong instruments, stuck notes, too loud or quiet
   (mix: 12-bit sample * level >> 14 per layer).
3. Rhythm part (mode 1, EXT, real-time messages), DEMO button, programs 110-111.
4. Wanwan Aijou Monogatari's OKI MSM6653A sound effects (expansion audio, 0x040a0000):
   samples are inside that chip, not dumped as far as we know; leave for later.

## Later

### Loopy
- [ ] Mouse (Wonder Palette; Megadoc: counters at 0x0c05d050, MODE bit 3)
- [ ] Anime Land menu: d-pad/A did not visibly change it in a headless test; check in
      the window
- [ ] Access timings (DRAM rows and refresh, VDP waits) are simplified
- [ ] Scanline capture (VDP.CAPTURE), Magical Shop's video input (none: show a still?)
- [ ] Hi-res blend mode 3 is averaged into 256 pixels

### Printer -> PNG
The Loopy's selling point: stickers. Registers from the Megadoc (PRINTER_TEMP 0x0c05d000,
STATUS 0x30 with ENP/SCT/OPTO, HEAD_DATA 0x40 fed by DMA, MOTOR 0x42 key 0x5a5, HEAD_CTRL
0x44 key 0xa5a). The BIOS print routine watches the paper and ink sensors, so the printer
must answer like the mechanism: a seal cartridge present (SCT), the alignment marks and
ribbon colours passing the OPTO sensors as the motor steps. Collect head lines per pass,
compose the three colour passes, write a PNG in prints/.

### Translation patches
Text encoding and fonts of the Loopy games (Shift-JIS? tiles?), free ROM space, an
IPS/BPS applier at load time (patch file beside the game, the ROM never changed).
Target language: English, for an international audience (user, 2026-11-01).
First survey (session 2):
- plain Shift-JIS text: Wanwan Aijou Monogatari (~45k characters, the whole adventure),
  Dream Change (~37k, dialogue), Loopy Town (~6k); Dream Change also has a full-width
  ASCII table, likely an SJIS-indexed font
- Anime Land: own 1-byte encoding, gojuon order (00 あ .. 2d ん), 36 っ, voiced = base +
  0x37 (4a ど), d8 space/padding, 90 end; menu strings in fixed 4-8 byte fields
  (0x250a0 おんな/おとこ, 0x116160 やさしい/あかるい..., とし は どの くらい); the font has
  only kana: English needs Latin glyphs in unused codes and room for longer strings
- Little Romance: EUC-JP (both chips byte-swapped like the rest), NUL-terminated, "／"
  for a new line, 32-bit pointers before each block (0x2ddd40: どこからはじめる？); about
  4000 strings, 0x37f56-0x2fb228. The name entry has hiragana, katakana, ABC and kanji
  pages: the font already has Latin letters
- Nigaoe Artist: own 16-bit big-endian codes, kana roughly gojuon from 0x20 (not exact
  further on: check against the font), kanji above (0x1b3 大, 0x31a 出); 0x1fcd40
  ふつうの大きさのシールが出るよ, 0x1fce70 ...をたしかめてね (two copies)
- HARIHARI: the menu items are graphics (shadowed font); the dialogue
  (どんなえのしゅるいをえらぶ？) not found yet: compressed, or another order
- Lupiton: ゛ ゜ are separate characters on screen (ギ = キ + ゛): search with such a table;
  ギャラリーだよ しゃしん イラストが いっぱいだよ
- PC Collection: the menu labels (文章を書く, 絵を描く...) look like graphics

### PV-1000
- [ ] Compare with videos of the real console (colours, border, Dig Dug's stripes)

### PV-2000 (after the translations; user, 2026-11-01)
A third system: Z80 (our core), TI TMS9918A video and SN76489 sound (TI's datasheets and
manuals), built-in keyboard, BIOS with BASIC, cartridges. Memory map, keyboard matrix and
I/O ports from MAME's pv2000 driver used as documentation, as for the PV-1000 (credited in
THIRD_PARTY_NOTICES; the strict clean room is for the Loopy only). To check first: the
dumps (cartridges and BIOS: not in nointro-casio-loopy-pv-1000.zip) and which games need
keys besides the joystick (a comfortable keyboard mapping). Estimate: one session to
boot, one to polish (TMS9918A modes, sprites, 4 per line, collisions).

### Frontend
- [ ] Controls panel (remapping), as Flick's F4 panel
- [ ] Toolbar on mouse move, as Flick
- [ ] Save states
- [ ] Release packaging (zip with SDL2.dll, README, licences), Linux build
- [ ] libretro core (core.h is shaped for it)
- [ ] Name: "Casiolator" is the working name
