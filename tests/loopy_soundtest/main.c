/* Casiolator Loopy sound test: known MIDI to the sound chip, as a yardstick for recordings
 *
 * Sets the sound chip up as the BIOS does (SOUND_CTRL sequence observed in Anime Land's
 * boot), then plays, one frame (1/60 s) at a time:
 *   1. programs 0-109 on channel 1: C4 for 48 frames, 24 frames of silence each
 *   2. program 0: C2, C3 ... C7 (36-96), 30 frames each, 10 apart
 *   3. program 9: C4 with the pitch bend swept up, down and back to the centre
 *   4. program 0: C4 held 30 frames with the sustain pedal, released 120 frames later
 *   5. program 0: C4 at velocity 16 and 127
 *   6. program 9: eight notes at once on channel 1 (more than its 6 voices)
 *   7. program 9: C4 on channels 2 and 4 at each volume setting
 * with 120 frames of silence between parts. */
#include <stdint.h>

#define REG8(a)  (*(volatile uint8_t *)(a))
#define REG16(a) (*(volatile uint16_t *)(a))
#define SMR1 REG8(0x05fffec8)
#define BRR1 REG8(0x05fffec9)
#define SCR1 REG8(0x05fffeca)
#define TDR1 REG8(0x05fffecb)
#define SSR1 REG8(0x05fffecc)
#define VCOUNT REG16(0x0c058004)
#define SOUND_CTRL REG16(0x0c080000)

static void frames(int n)
{
	while (n-- > 0)
	{
		while ((VCOUNT & 0x1ff) != 0)
			;
		while ((VCOUNT & 0x1ff) == 0)
			;
	}
}

/* waits for TDRE, but not forever: an emulator whose SCI never sets it still gets bytes
 * about 1 ms apart */
static void tx(uint8_t b)
{
	for (volatile int i = 0; i < 3000 && !(SSR1 & 0x80); i++)
		;
	TDR1 = b;
	SSR1 &= ~0x80;
}

static void msg3(uint8_t s, uint8_t a, uint8_t b) { tx(s); tx(a); tx(b); }
static void program(int ch, int p) { tx(0xc0 | ch); tx(p); }
static void note_on(int ch, int n, int v) { msg3(0x90 | ch, n, v); }
static void note_off(int ch, int n) { msg3(0x80 | ch, n, 0); }
static void bend(int ch, int v) { msg3(0xe0 | ch, v & 0x7f, v >> 7); }

void main(void)
{
	static const uint16_t setup[] = {
		0x0000, 0x0010, 0x0000, 0x0020, 0x0024, 0x0020, 0x0000, 0x0800, 0x0000, 0x0100, 0x0924
	};
	SCR1 = 0;
	SMR1 = 0;                                   /* 8N1, clock / 1 */
	BRR1 = 15;                                  /* 31250 baud at 16 MHz */
	frames(1);
	SCR1 = 0x20;                                /* transmitter on */
	for (unsigned i = 0; i < sizeof(setup) / sizeof(setup[0]); i++)
	{
		SOUND_CTRL = setup[i];
		frames(4);
	}
	SOUND_CTRL = 0x0920;                        /* all channels at full volume */
	frames(60);

	/* 1 */
	for (int p = 0; p < 110; p++)
	{
		program(0, p);
		note_on(0, 60, 64);
		frames(48);
		note_off(0, 60);
		frames(24);
	}
	frames(120);

	/* 2 */
	program(0, 0);
	for (int n = 36; n <= 96; n += 12)
	{
		note_on(0, n, 64);
		frames(30);
		note_off(0, n);
		frames(10);
	}
	frames(120);

	/* 3 */
	program(0, 9);
	note_on(0, 60, 64);
	frames(30);
	for (int v = 0x2000; v < 0x4000; v += 0x100) { bend(0, v); frames(2); }
	bend(0, 0x3fff);
	frames(30);
	for (int v = 0x3f00; v >= 0; v -= 0x100) { bend(0, v); frames(2); }
	frames(30);
	bend(0, 0x2000);
	frames(30);
	note_off(0, 60);
	frames(120);

	/* 4 */
	program(0, 0);
	note_on(0, 60, 64);
	msg3(0xb0, 0x40, 0x7f);
	frames(30);
	note_off(0, 60);
	frames(120);
	msg3(0xb0, 0x40, 0x00);
	frames(120);

	/* 5 */
	note_on(0, 60, 16);
	frames(30);
	note_off(0, 60);
	frames(30);
	note_on(0, 60, 127);
	frames(30);
	note_off(0, 60);
	frames(120);

	/* 6 */
	static const uint8_t chord[] = { 48, 52, 55, 60, 64, 67, 72, 76 };
	program(0, 9);
	for (int i = 0; i < 8; i++)
	{
		note_on(0, chord[i], 64);
		frames(10);
	}
	frames(60);
	for (int i = 0; i < 8; i++)
		note_off(0, chord[i]);
	frames(120);

	/* 7: AVOL (channels 2-3) and RVOL (channel 4) at 2, 3, 4 */
	static const uint16_t vol[] = { 0x0240 | 0x20, 0x0480 | 0x20, 0x0900 | 0x20 };
	program(1, 9);
	program(3, 9);
	for (int i = 0; i < 3; i++)
	{
		SOUND_CTRL = vol[i];
		note_on(1, 60, 64);
		frames(30);
		note_off(1, 60);
		frames(10);
		note_on(3, 60, 64);
		frames(30);
		note_off(3, 60);
		frames(10);
	}
	SOUND_CTRL = 0x0920;
	frames(120);
	for (;;)
		;
}
