# Third-party notices

## Hardware documentation

Casiolator's code is its own; what it knows about the hardware comes from:

- **PV-1000**: MAME's `pv1000.cpp` driver (BSD-3-Clause; Wilbert Pol, with plgDavid's
  sound and timing analysis and lidnariq's test ROMs), used as documentation for the
  memory map, the video chip's bus timing, the interrupts and the sound mixer.
- **Z80**: the Z80 exercisers zexdoc and zexall (Frank D. Cringle, J.G. Harston's
  updates) are the reference for the CPU's behaviour, flags included.

## SDL2

The window frontend links SDL2 (zlib licence), distributed beside the executable as
`SDL2.dll` with its licence.
