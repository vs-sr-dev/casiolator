# TODO

## Done (session 1, 2026-11-01)

- Z80 core (src/z80.c): zexdoc and zexall all OK.
- PV-1000 (src/pv1000.c): video per line, BUSREQ stalls on the tile lines, matrix and
  prerender interrupts, 3 square waves with the resistor mix and ring modulation,
  2 joysticks. All 13 No-Intro games reach gameplay with scripted input; sound in all.
- Frontend (src/main.c): start screen with no arguments, drag and drop, file dialog,
  sound as the clock, controllers with hotplug (2 players), pause, reset, screenshots,
  full screen, window size, `casiolator.ini`; headless mode with `--press` scripts,
  `--screenshot`, `--wav`.
- Cartridge loading (src/game.c, src/zip.c): .bin or .zip (own inflater), Loopy byte order
  detected, split IC104/IC105 chips joined (zip or folder), clear messages for a whole
  collection zip or a non-cartridge.

## Next

### 1. PV-1000, user testing
- [x] User played (2026-11-01): speed right, sound fine, controller detected at once
- [x] Two players: pad = P1, keyboard = P2 confirmed by the user
- [ ] Check against videos of the real console (colours, the border, Dig Dug's stripes)

### 2. Casio Loopy core

Session 1 progress (2026-11-01): SH-1 core (src/sh1.c, from Hitachi's programming manual
in refs/), SH7021 peripherals (src/sh7021.c), machine (src/loopy.c), VDP renderer
(src/vdp.c). All 12 cartridges start (Magical Shop with kasami's header). BIOS asked once,
saves in saves/. 7.5x real time. Still missing: sound, printer, mouse, capture.
Open questions:
- [x] Loopy Town menu: the black rectangle was the hand cursor; OBJ_CTRL FMT applies to
      OBJ1 too (the Megadoc says both "for both OBJ layers" and "OBJ1 is 4bpp only")
- [ ] Anime Land menu: d-pad/A did not visibly change it in a headless test
- [ ] SH7020/21 hardware manual (refs/sh7020_hw_manual.pdf) to check the peripherals
      written from SH-1 family knowledge: ITU, DMAC request sources, INTC, SCI timing
- [ ] Access timings (DRAM rows, refresh, VDP waits) are simplified

Decided (user, 2026-11-01): our own core, clean room, MIT. Do NOT read LoopyMSE's or
MAME's casloopy source. Allowed sources: kasami's Hardware Megadoc
(D:\Homebrew2\CL\docs), Loopy-Schematics, the patent US5883611, Hitachi's SH7021/SH-1
manuals, NEC docs, and our own experiments. LoopyMSE / zRupi / MAME may be run as black
boxes (compare screens, sound, timing), never read. Pieces:
- [ ] SH-1 (SH7021) interpreter + on-chip peripherals (ITU timers, DMA, SCI, IRQs)
- [ ] VDP RH-7500: bitmap layers, tilemaps, sprites, the line buffer / capture
- [ ] Sound: uPD937 (NEC CDT-109) and its 512 KB sample ROM (hn62434fa, CRC 8f51fa17).
      Clean room from scratch (user, 2026-11-01): the SR2 MAME-fork synth was ported
      from LoopyMSE and must not be reused. Sources: Megadoc, kasami's "Music track
      format.txt" (MIDI subset, modes, polyphony), analysis of the sample ROM, the MIDI
      the games send on SCI1; other emulators only as black boxes (WAV comparison).
- [ ] Cartridge SRAM saves (.sav beside the game or in saves/)
- [ ] BIOS: ask once, remember it (the No-Intro "[BIOS] Internal Thermal Printer" file is
      the main BIOS, md5 c0f1c899...; the sound BIOS is HD6437021's internal ROM)
- [ ] Magical Shop without header (find the byte order from the code)
- [ ] Chakrakun no Omajinai Paradise (missing from the No-Intro zip; in D:\Homebrew2\CL\games)
- [ ] Mouse (Wonder Palette)

### 3. Printer -> PNG
The Loopy's selling point: stickers. Registers known from kasami's megadoc
(PRINTER_TEMP 0x0C05D000, STATUS 0x30, HEAD_DATA 0x40, MOTOR 0x42, HEAD_CTRL 0x44), our
layer-1 observer from SR2 session 3 exists. Plan: collect head lines while the motor steps,
compose the colour passes (CMY ribbon), write a PNG in prints/.

### 4. Translation patches
Evaluate: text encoding and font in the Loopy games (Shift-JIS? custom tiles?), free space
in the ROMs, an IPS/BPS applier at load time (patch beside the ROM, never the ROM itself).
PV-1000 games are mostly English already.

### 5. Frontend
- [ ] Controls panel (remapping) as Flick's F4 panel
- [ ] Toolbar on mouse move as Flick
- [ ] Save states
- [ ] Release packaging (zip with SDL2.dll, README, licences), Linux build
- [ ] libretro core (the core.h interface is shaped for it)
- [ ] Name: "Casiolator" is the working name
