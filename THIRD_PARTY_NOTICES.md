# Third-party notices

## Hardware documentation

Casiolator's code is its own; what it knows about the hardware comes from:

- **PV-1000**: MAME's `pv1000.cpp` driver (BSD-3-Clause; Wilbert Pol, with plgDavid's
  sound and timing analysis and lidnariq's test ROMs), used as documentation for the
  memory map, the video chip's bus timing, the interrupts and the sound mixer.
- **Loopy**: kasami's Casio Loopy Hardware Megadoc, Loopy-Tools documentation (ROM header,
  music track format and the sound chip's MIDI subset) and Loopy-Schematics (the audio
  output filter); Hitachi's SH-1 programming and SH7020/SH7021 hardware manuals. The sound
  chip's sample ROM layout is our own reading of the ROM; hxlnt's LoopyMID instrument
  table (InstrumentDefinitions.csv) pointed at where its zone table is. No other
  emulator's code was read.
- **Z80**: the Z80 exercisers zexdoc and zexall (Frank D. Cringle, J.G. Harston's
  updates) are the reference for the CPU's behaviour, flags included.

## SDL2

The window frontend links SDL2 (zlib licence), distributed beside the executable as
`SDL2.dll` with its licence.
