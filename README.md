# Casiolator

An emulator for Casio's two cartridge consoles, the **PV-1000** (1983) and the **Loopy**
(1995), made to be easy: open it, drop a cartridge on the window, play. No configuration
files to edit, no frontend to set up, controllers work as soon as they are plugged in.

Status:

| System | |
|-|-|
| PV-1000 | all 13 games play, with sound |
| Loopy | in progress |

Casiolator does not come with any game or BIOS: bring your own dumps.

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

## Licence

MIT (see `LICENSE`). Credits for the hardware knowledge this builds on are in
`THIRD_PARTY_NOTICES.md`.
