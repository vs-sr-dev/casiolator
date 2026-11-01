# Loopy sound: what we know (clean room)

Our own findings, from kasami's documentation (Hardware Megadoc, Loopy-Tools "Music track
format.txt", Loopy-Schematics), from reading the sample ROM, and from observing the BIOS
and the games. hxlnt's LoopyMID instrument table (a data table, not code) pointed at where
the zone table is; everything below was checked against the ROM itself. No other
emulator's code was read. Other emulators are used only as black boxes, to compare
recordings. The synthesizer is `src/upd937.c`.

## The chips

- NEC CDT109, a cost-reduced uPD937 (Casio keyboard synthesizer), MIDI in from the
  SH7021's SCI1 at 31250 baud 8N1 (SMR1 = 0, BRR1 = 15: the async formula has
  64 * 2^(2n-1), so 32 for n = 0).
- Sample ROM HN62434FA at LSI352, 512 KB, CRC32 8f51fa17 (No-Intro mislabels it
  "[BIOS] Internal Thermal Printer").
- RH-7501 simulates the keyboard's buttons and volume sliders: SOUND_CTRL (0x0c080000).
- Output: stereo at 21.725 MHz / 256 = 84863 Hz.

## Output filter (Loopy-Schematics, Audio-Path)

DAC (uPD6379) -> 10u coupling -> unity-gain third-order Sallen-Key low-pass (IC252,
R253-R255 10K, C256 330p to ground, C280 5n6 feedback, C259 330p to ground) -> inverting
mixer (gain 6.8, cartridge audio at 1.0) -> AV board. The low-pass:

    H(s) = 1 / (1 + 1.32e-5 s + 3.914e-10 s^2 + 6.098e-16 s^3)

+0.1 dB at 1 kHz, +4.8 dB at 7.5 kHz, 0 dB at 10.5 kHz, -14 dB at 20 kHz, -43 dB at
85 kHz (kasami's figures, and ours from the values). The coupling capacitors only block DC
(corners below 1 Hz). `upd937.c` runs it digitally (bilinear, warped at 7.5 kHz).

## MIDI the BIOS sends

The BIOS drives SCI1 with DMA channel 1 (CHCR 0x1701: source increments, TXI1 request,
byte size). `CL_MIDI=file` writes every byte with its time, and SOUND_CTRL writes.

SOUND_CTRL at boot (Anime Land; about 4 frames apart): 0000, 0010 (ON), 0000, 0020 (MIDI),
0024 (MIDI + CH4 pressed), 0020, 0000, 0800, 0000, 0100, 0924; then each music track
writes its control value (0x08a0 for the boot jingle).

Supported messages (kasami): note on/off 36-96, program change 0-109 (others reset the
channel), pitch bend +/- 2 semitones (8 bits used), sustain (CC 0x40), no velocity; mode 0
has 4 channels with 6/4/2/4 voices, mode 2 three channels 6/4/2, mode 3 one channel of 12.

A program change ends the notes playing on its channel: the BIOS changes music tracks
with program changes only (Anime Land, title to menu: `c0 13 ; c1 3c ; c3 27`, no
note-offs), and kasami's track notes stop the music the same way. We release them.

## Sample ROM layout

The ROM reads as 16-bit little-endian words. Byte offsets:

| at | what |
|-|-|
| 0x0000 | seven words `021b 0311 036a 0532 05ae 3500 24e3`: the first five are table addresses / 32 (0x4360 zones, 0x6220 pitch modulation, 0x6d40 envelopes, 0xa640 samples, 0xb5c0 ?) |
| 0x0200 | 256 bytes: a bend-wheel curve (0 below 0x13, a dead zone of 0x80 in the middle); not used for MIDI as far as we know |
| 0x0400-0x051f | rhythm / demo data (not decoded) |
| 0x0600 | tempo table: 224 words, 1250000 / (k + 40), for 40-263 BPM (rhythms and demo) |
| 0x0800 | pointers, rhythm / demo (not decoded) |
| 0x1000 | envelope rates: 256 x (step, wait - 1 \| (0x40 + index / 2) << 8). The level moves by `step` every `wait` ticks; index 0 does not move, 0xff (0x7fff) jumps |
| 0x1400 | level to amplitude: 256 words, 0 then 0x16 .. 0x7fff, about 0.25 dB a step |
| 0x1600 | pitch: 1536 words, 0x800 * 2^(i / 384): four octaves at 1/32 semitone |
| 0x2200 | programs: 112 x (first zone * 6, split map \| 0x1000) |
| 0x3da0 | split maps: 46 x 32 bytes: 60 nibbles, the key range of keys 36-95, then the highest range |
| 0x4360 | zones: 12 bytes, two layers of (pitch modulation, envelope, sample) |
| 0x6220 | pitch modulation: 89 x 32 bytes |
| 0x6d40 | envelopes: 228 x 64 bytes |
| 0xa640 | samples: 405 x 10 bytes (key, start, end, loop), 24-bit word addresses |
| 0xb612-0x7ffff | sample data |

Programs 0-109 are the MIDI programs; 110 and 111 are further drum kits (rhythm part?).

A note: the program's split map gives the key range (nibble `note - 36`); the zone is
`first + range`, or with bit 0x1000 `first + 2 * range` and the one after it: those
programs play two zones at once ("polyphony is halved for some complex sounds", kasami).
Each zone has two layers; we play layer 1 on the left and layer 2 on the right (most
programs have the same sample on both with a small detune, +-1/32 semitone, which makes
the stereo chorus; a black-box recording is decorrelated left/right the same way).

### Samples

Word = 12-bit sample in the high bits, offset binary: `(word >> 4) - 0x800`. The low
nibble is not precision (a sine fits worse with it) but other data, unknown. Samples loop
from `loop` to `end` (every sample has a loop). Single-cycle sines: 96 words at key 33,
48 at 45, 24 at 57, 12 at 69: at its key a sample plays 5280 samples a second, i.e. the
pitch table's 0x800 = one sample every 16 output frames (84863 / 16 = 5304 Hz, the console
is then 8 cents sharp; a recording of the real console would tell).

### Envelopes (0x6d40)

16 (rate, level) byte pairs for the key held, then 16 for after its release. Bit 7 of the
rate is the direction (set: down), the rest indexes the rate table (we use entry 2 * rate:
entry 0 is "stay", like rate 0 in the data). The level (8 bits; 15 internally) moves to
the pair's level, then the next pair starts. A pair already reached is skipped, so the
`(n, 00)` pairs that start entries 0x00-0x29 do nothing (unless they are a delay: open).
At the key's release the envelope goes on in the second half from the same pair index;
the second half repeats its pairs so that every index leads to the release. The table is
sorted by its first two bytes.

### Pitch modulation (0x6220)

Words: detune (sign and magnitude, sign in bit 12), delay, then (speed, distance) pairs:
the pitch moves `distance` units up, or down if the speed has bit 12, at `speed`; then the
next pair. 0x20nn loops to pair nn (counted from 1); distance 0 or speed 0 ends. The unit
is the detune's, 1/32 semitone (a black-box recording agrees for the detune). Read this
way every entry adds up: vibratos are centred triangles (0x0a: -5, +10, -5 at speed 15,
loop), the brass entries scoop up from their detune and land on 0 before their vibrato
(0x4c: -50, then +48 fast and +2 slow, then +-5), the "jitter" ones (0x0d, 0x0e) sum to 0,
and the piano's left layer (0x58) moves up 1 unit at once: the stereo chorus.

Speed 1 = about 7.5 units a second puts all the vibratos at 5-6 Hz and the brass scoops
at about 40 ms; the delay unit is about 12 ms. Both are guesses.

## Open questions (for a recording of the real console)

- Envelope time base: one step per output frame with entry 2 * rate gives a piano that
  loses about 6 dB in 0.2 s (envelope alone), and a release of 40 dB/s. A black-box
  emulator decays slower while held and releases faster.
- The pitch modulation's speed and delay units.
- Whether `(n, 00)` pairs are delays.
- Interpolation between samples (we interpolate linearly).
- The 8 cents: is the chip's clock exactly 21.725 MHz?
- Keys outside 36-96 (we move them by octaves into the range), the rhythm part (EXT,
  real-time messages), the DEMO tune.

## Test cartridge

`tests/loopy_soundtest` (build in WSL with sh-elf-gcc: `wsl make -C tests/loopy_soundtest`)
plays known MIDI: every program on C4, C2-C7, a pitch bend sweep, the sustain pedal,
velocity, eight notes at once, the channel volumes. It sets the chip up as the BIOS does.
It writes SCI1 waiting for TDRE (with a timeout, for emulators that never set it). MAME
wants cartridges byte-swapped. Recording it on a real Loopy (with a flash cartridge) would
answer most of the open questions.
