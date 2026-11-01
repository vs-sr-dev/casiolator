# Casiolator

An emulator for Casio's two cartridge consoles, the **PV-1000** (1983) and the **Loopy**
(1995), made to be easy: open it, drop a cartridge on the window, play. No configuration
files to edit, no frontend to set up, controllers work as soon as they are plugged in.

Status:

| System | |
|-|-|
| PV-1000 | all 13 games play, with sound |
| Loopy | all 12 cartridges start and respond, with music (first version); no printer, no mouse |

Casiolator does not come with any game or BIOS: bring your own dumps.

The Loopy needs its BIOS: the 32 KB ROM inside the CPU (`hd6437021.lsi302`, also inside
MAME's `casloopy.zip`). Casiolator looks for it beside the executable (also in a `bios`
folder) and beside the game; otherwise the first Loopy game you open asks for it once.

For the music it also needs the sound chip's 512 KB sample ROM (`hn62434fa.lsi352`, also in
MAME's `casloopy.zip`; the No-Intro set calls it "[BIOS] Internal Thermal Printer", which
it is not). It is looked for in the same places and beside the BIOS, then asked for once;
without it the games run silent.

One known bad dump: No-Intro's "Little Romance (Japan)", whose `IC104.bin` holds its first
megabyte twice (the game's own header checksum disagrees). The game starts, but pictures
from that part of the cartridge are wrong. A good dump is one 3 MB image, CRC32
`550616f0` in big-endian byte order.

## Use

Start `casiolator.exe`. Drop a cartridge image on the window, or click to look for one
(Ctrl+O does the same at any time). These all work as they come:

- a plain `.bin`, or a `.zip` with the game in it (the No-Intro sets are fine as they are,
  one zip per game)
- Loopy images in either byte order (No-Intro's, or the older big-endian sets)
- Loopy games on two chips (`IC104.bin` + `IC105.bin`), zipped or in a folder: open either
  chip

From a terminal: `casiolator game.zip`. `casiolator --help` lists the options.

### Controls

Controllers are player 1 and player 2 in the order they are plugged in; the keyboard plays
the first player left without one. So: no controller, the keyboard is player 1; one
controller, it is player 1 and the keyboard player 2; two controllers, the keyboard joins
player 1.

| PV-1000 | Keyboard | Controller |
|-|-|-|
| joystick | arrows | d-pad, left stick |
| button 1 | Z | A (south) |
| button 2 | X | B (east) |
| start | Enter | Start |
| select | Right Shift, Backspace | Back / Select |

Two-player PV-1000 games start with Select, then Start.

| Loopy | Keyboard | Controller |
|-|-|-|
| d-pad | arrows | d-pad, left stick |
| A, B, C, D | Z, X, A, S | A, B, X, Y |
| L, R | Q, W | shoulders, triggers |
| Start | Enter | Start |

Loopy games save in `saves/`, next to the executable, whenever the cartridge's battery RAM
changes.

### Keys

| Key | |
|-|-|
| Ctrl+O | open a cartridge (or drop it on the window) |
| P, Pause | pause |
| F5 | reset |
| F11, Alt+Enter | full screen (Esc leaves it) |
| F12 | screenshot, in `screenshots/` |
| Ctrl+Plus, Ctrl+Minus | window size |
| Esc | quit |

`casiolator.ini`, next to the executable, keeps the last folder, the window size and the
volume.

## Build

C99 and SDL2. On Windows with MSYS2 (mingw64: gcc, cmake, ninja, SDL2):

```
cmake -S . -B build -G Ninja
ninja -C build
```

Without SDL2 the build is headless only (`--headless --frames N --screenshot f.png`).

`build/zextest` runs the Z80 exercisers (zexdoc, zexall, not included); the Z80 core passes
both.

## Translations

English translations of the Loopy games are in the works, starting with Little Romance.
They will be released as patches (BPS) to apply to your own dump; this repository holds
the tools, not the games' text. The tools (Python 3, no extra packages):

- `tools/lr_text.py ROM DIR`: extracts Little Romance's script into text files
- `tools/lr_patch.py ROM DIR OUT.bin [OUT.bps]`: builds the English cartridge and patch
- `tools/lr_font.py`: the 6-pixel Latin font the patch uses

How the game's text, font and speech balloons work is in `docs/little-romance.md`.

## Development

- `build/shdis file base start end`: SH-1 disassembler for Loopy code
- `docs/loopy-sound.md`: the Loopy's sound chip and sample ROM, as far as we know
- `tests/loopy_soundtest`: a homebrew Loopy cartridge that plays every instrument (to
  compare with a real console)
- environment variables for the Loopy: `CL_TRACE=n` (trace n instructions), `CL_LOG=1`
  (unknown I/O, battery RAM writes), `CL_MIDI=file` (what goes to the sound chip),
  `CL_DUMP=file` with `CL_DUMP_FRAME=n` (video memories), `CL_WATCH=lo-hi` (CPU reads of
  an address range, hex, with the code that reads)
- `TODO.md`: what is done and what comes next

## Licence

MIT (see `LICENSE`). Credits for the hardware knowledge this builds on are in
`THIRD_PARTY_NOTICES.md`.
