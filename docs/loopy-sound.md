# Loopy sound: what we know (clean room)

Our own findings, from kasami's documentation (Hardware Megadoc, Loopy-Tools "Music track
format.txt") and from observing the BIOS, the games and the sample ROM. No other
emulator's code was read for this. Other emulators may be used only as black boxes, to
compare recordings.

## The chips

- NEC CDT109, a cost-reduced uPD937 (Casio keyboard synthesizer), MIDI in from the
  SH7021's SCI1 at 31250 baud 8N1 (BRR 15 at 16 MHz, n = 0).
- Sample ROM HN62434FA at LSI352, 512 KB, CRC32 8f51fa17 (No-Intro mislabels it
  "[BIOS] Internal Thermal Printer").
- RH-7501 simulates the keyboard's buttons and volume sliders: SOUND_CTRL (0x0c080000),
  bits in the Megadoc and in src/loopy.h.
- Output: stereo, nominal 84864 Hz (Megadoc), anti-alias filter with a slight boost near
  7.5 kHz.

## MIDI the BIOS sends

The BIOS drives SCI1 with DMA channel 1 (CHCR 0x1701: source increments, TXI1 request,
byte size). `CL_MIDI=file` writes every byte with its time, and SOUND_CTRL writes.

Anime Land boot (times in seconds):

```
0.30 ctrl 0000, 0.37 ctrl 0010 (ON), 0.43 ctrl 0000, 0.50 ctrl 0020 (MIDI),
0.57 ctrl 0024 (MIDI+CH4), 0.64 ctrl 0020, 0.72 ctrl 0000, 0.79 ctrl 0800 (RVOL3),
0.87 ctrl 0000, 0.94 ctrl 0100 (AVOL4), 1.00 ctrl 0924 ...
1.02 c2 c2 ...            program changes
2.49 c0 c0 90 38 40 c1 5c 91 38 40 c3 50 93 38 40 ...   notes start
```

Supported messages (kasami): note on/off 36-96, program change 0-109, pitch bend +/- 2
semitones (8 bits used), sustain (CC 0x40), no velocity; 4 channels with 6/4/2/4 voices
in mode 0.

## Sample ROM layout

- 0x00000-0x0bfff: tables (low entropy). Starts with seven little-endian words:
  `021b 0311 036a 0532 05ae 3500 24e3` (meaning still to find: offsets?).
- 0x0c000-0x7ffff: sample data (entropy 7.1-7.8 bits per byte).
- The ROM reads as **16-bit little-endian words**.

## Sample encoding

Found by trying encodings and measuring the mean step between neighbouring samples
(a real waveform is smooth):

| reading | mean step / range |
|-|-|
| 12 bits = word >> 4, little-endian | **0.031** |
| signed 16-bit little-endian | 0.114 |
| unsigned 8-bit | 0.266 |
| others | 0.27-0.43 |

The 12-bit values average 2048.5, so they are offset binary: **sample = (word >> 4) -
0x800**. The low nibble is not extra precision: second differences are the same with
and without it. It is separate data, maybe loop or end markers. Distribution over the
sample area: 15 in 38% of words, 0 in 16%, the rest spread out.

## Next steps

1. Decode the tables: what the seven header words point to; find 110 program entries
   (start address, loop, end, root key, envelope?).
2. Low-nibble meaning: look at where it changes along a sample (sample boundaries?).
3. Black-box reference: record LoopyMSE/zRupi or the SR2 MAME fork playing known MIDI
   (one note per program) to WAV, to compare pitch, envelope and loop.
4. Synth: voice allocation per mode, pitch from the note, envelope, stereo, 84864 Hz,
   output filter.
