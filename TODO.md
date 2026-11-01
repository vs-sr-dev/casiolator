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

## Rules for the Loopy (user, 2026-11-01)

Clean room, MIT. Never read LoopyMSE's or MAME's casloopy source, nor the SR2 MAME-fork
uPD937 (it was ported from LoopyMSE). Allowed: kasami's Megadoc and Loopy-Tools
(D:\Homebrew2\CL\docs), Loopy-Schematics, patent US5883611, Hitachi/NEC manuals, our own
experiments. Other emulators only as black boxes (run them, compare pictures/recordings).

## Next session: Loopy sound

Start from docs/loopy-sound.md (what we found so far: ROM layout, 12-bit samples, MIDI).

1. **Reference recordings (black box).** Best: the real console, if available. Otherwise
   the SR2 MAME fork (`-wavwrite`) or LoopyMSE/zRupi run as programs. Make a tiny test
   cartridge with the SR2 toolchain (sh-elf-gcc in WSL, D:\Homebrew2\CL) that sends known
   MIDI on SCI1: each program 0-109 playing one note (e.g. C4, 1 s on, 1 s off), plus a
   few notes over the range for pitch, a pitch bend and a sustain. Record it; it becomes
   the yardstick for every step below.
2. **Sample ROM tables (0x00000-0x0bfff).** The seven header words (021b 0311 036a 0532
   05ae 3500 24e3): offsets of sub-tables? Find the 110 program entries: sample start /
   loop / end in the 0x0c000+ area, root key or pitch, envelope references. Look for runs
   of addresses that point into 0x0c000-0x7ffff.
3. **Low nibble of the sample words.** Where it changes along a waveform; whether it marks
   loop points or sample ends (compare with the boundaries found in step 2).
4. **Synth (src/upd937.c, new).** MIDI parser (running status included), the modes and
   voice counts from SOUND_CTRL (kasami: 6/4/2/4 voices in mode 0, 12 in mode 3), voice
   allocation, pitch from the note (and the 8-bit pitch bend, +/- 2 semitones), sample
   playback with loop, envelope, sustain, stereo. Output at 84864 Hz (Megadoc), then the
   frontend's resampler; the analogue filter's slight boost near 7.5 kHz last.
5. **Hook up:** SCI1 bytes to the synth with their time (CL_MIDI shows they already
   arrive), SOUND_CTRL buttons (ON resets, MIDI mode, CH3/CH4/EXT, DEMO, volumes),
   the core's audio_take, `--wav` for comparisons.
6. Wanwan Aijou Monogatari's OKI MSM6653A sound effects (expansion audio, 0x040a0000):
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

### PV-1000
- [ ] Compare with videos of the real console (colours, border, Dig Dug's stripes)

### Frontend
- [ ] Controls panel (remapping), as Flick's F4 panel
- [ ] Toolbar on mouse move, as Flick
- [ ] Save states
- [ ] Release packaging (zip with SDL2.dll, README, licences), Linux build
- [ ] libretro core (core.h is shaped for it)
- [ ] Name: "Casiolator" is the working name
