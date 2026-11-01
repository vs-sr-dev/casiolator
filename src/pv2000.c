/* Casiolator - Casio PV-2000 (1983)
 *
 * Z80 at 3.579545 MHz, TMS9928A video with 16 KB of VRAM, SN76489A sound, a keyboard with
 * a joystick built in. Cartridges call the 16 KB BIOS (BASIC included), which has to be
 * there. Hardware notes from MAME's pv2000.cpp (Wilbert Pol, anondumper):
 *  - 0x0000-0x3fff BIOS, 0x4000 VDP data, 0x4001 VDP control, 0x7000-0x7fff RAM,
 *    0xc000-0xffff cartridge (8 KB ones repeat)
 *  - ports: 0x20 write selects a keyboard column (0x0f: any key raises /INT), 0x20 read
 *    its rows 0-3, 0x10 read rows 4-7, 0x40 read the modifier keys, 0x40 write the sound
 *    chip, 0x00 and 0x60 the cassette
 *  - the VDP's interrupt goes to NMI; the keyboard's to /INT with 0xff on the bus
 *
 * Time is counted in CPU cycles: 228 a line, 262 lines. */
#include <string.h>
#include "core.h"
#include "z80.h"
#include "tms9918.h"
#include "sn76489.h"

#define CLOCK      3579545
#define LINE_CY    228
#define LINES      262
#define FRAME_CY   (LINE_CY * LINES)

/* what the frontend shows: the active area with 8 pixels of border around it */
#define BORDER     8
#define FB_W       (TMS_W + 2 * BORDER)
#define FB_H       (TMS_H + 2 * BORDER)

#define BIOS_SIZE  0x4000
#define ABUF       4096

static struct
{
	z80 cpu;
	tms9918 vdp;
	sn76489 psg;
	uint8_t bios[BIOS_SIZE];
	uint8_t rom[0x4000];        /* the cartridge, repeated over 16 KB */
	uint8_t ram[0x1000];
	uint8_t column;             /* keyboard column selected */
	uint8_t keys_down;          /* any key, at the last check */
	uint8_t vdp_int;            /* the VDP's /INT, for the NMI edge */
	uint64_t now;               /* CPU time, cycles since reset */
	uint64_t frame_start;

	uint64_t snd_time;
	int32_t hp_in, hp_out;      /* DC blocker */
	int16_t abuf[ABUF];
	int alen;

	uint32_t pad[2];
	uint32_t fb[FB_W * FB_H];
} pv;

/* ---- sound ---- */

static void sound_sync(void)
{
	while (pv.snd_time + SN_DIV <= pv.now)
	{
		pv.snd_time += SN_DIV;
		const int32_t in = sn_sample(&pv.psg) * 2;
		/* one-pole high-pass, ~10 Hz at 56 kHz */
		pv.hp_out = in - pv.hp_in + (pv.hp_out * 4091 >> 12);
		pv.hp_in = in;
		int32_t s = pv.hp_out;
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		if (pv.alen < ABUF)
			pv.abuf[pv.alen++] = (int16_t)s;
	}
}

/* ---- keyboard ---- */

/* the joystick's place in the keyboard matrix: column 6 bit 0 down, bit 1 right; column 7
 * bit 0 left, bit 1 up; column 8 bits 0 and 1 the two buttons; column 4 bit 1 START and
 * bit 2 SELECT (as Galaga names them). The second player is bits 2 and 3 of the same
 * columns: Excite Mahjong tests a button with masks 0x05 and 0x0a. Start also presses
 * RETURN (column 8 bit 4): the games say "start or return", and Rakugaki wants RETURN.
 * Column 5 has the diagonal keys; the games that read them (Front Line, Ski Command) also
 * take the cardinal pairs. */
static uint8_t matrix(int col)
{
	uint8_t d = 0;
	for (int p = 0; p < 2; p++)
	{
		const uint32_t b = pv.pad[p];
		const int s = p * 2;
		switch (col)
		{
		case 4:
			if (p == 0)
				d |= (b & (1 << PAD_START) ? 2 : 0) | (b & (1 << PAD_SELECT) ? 4 : 0);
			break;
		case 6: d |= ((b & (1 << PAD_DOWN) ? 1 : 0) | (b & (1 << PAD_RIGHT) ? 2 : 0)) << s; break;
		case 7: d |= ((b & (1 << PAD_LEFT) ? 1 : 0) | (b & (1 << PAD_UP) ? 2 : 0)) << s; break;
		case 8:
			d |= ((b & (1 << PAD_A) ? 1 : 0) | (b & (1 << PAD_B) ? 2 : 0)) << s;
			if (p == 0)
				d |= b & (1 << PAD_START) ? 0x10 : 0;
			break;
		}
	}
	return d;
}

static uint8_t any_key(void)
{
	uint8_t d = 0;
	for (int c = 0; c < 9; c++)
		d |= matrix(c);
	return d;
}

/* ---- bus ---- */

static void update_nmi(void)
{
	const int level = tms_int(&pv.vdp);
	if (level && !pv.vdp_int)
		pv.cpu.nmi = 1;
	pv.vdp_int = (uint8_t)level;
}

static uint8_t mem_r(void *c, uint16_t a)
{
	if (a < 0x4000)
		return pv.bios[a];
	if (a < 0x4002)
	{
		const uint8_t d = tms_read(&pv.vdp, a & 1);
		update_nmi();
		return d;
	}
	if (a >= 0x7000 && a < 0x8000)
		return pv.ram[a & 0xfff];
	if (a >= 0xc000)
		return pv.rom[a & 0x3fff];
	return 0xff;
}

static void mem_w(void *c, uint16_t a, uint8_t v)
{
	if (a == 0x4000 || a == 0x4001)
	{
		tms_write(&pv.vdp, a & 1, v);
		update_nmi();
	}
	else if (a >= 0x7000 && a < 0x8000)
		pv.ram[a & 0xfff] = v;
}

static uint8_t io_r(void *c, uint16_t port)
{
	switch (port & 0xff)
	{
	case 0x10:
		return pv.column < 9 ? matrix(pv.column) >> 4 : 0;
	case 0x20:
		return 0xf0 | (pv.column < 10 ? matrix(pv.column) & 0x0f : 0);
	case 0x40:
		return 0xf0;                /* COLOR, FUNC, SHIFT up */
	case 0x60:
		return 0x02;                /* cassette: bit 1 high, bit 2 low, no data */
	default:
		return 0xff;
	}
}

static void io_w(void *c, uint16_t port, uint8_t v)
{
	switch (port & 0xff)
	{
	case 0x20:
		pv.column = v & 0x0f;
		pv.cpu.irq = 0;
		break;
	case 0x40:
		sound_sync();
		sn_write(&pv.psg, v);
		break;
	}
}

/* ---- run ---- */

static void run_until(uint64_t t)
{
	while (pv.now < t)
		pv.now += (uint64_t)z80_step(&pv.cpu);
}

static void fill(uint32_t *row, int n, uint32_t c)
{
	for (int i = 0; i < n; i++)
		row[i] = c;
}

/* frame line -> framebuffer row: active lines, the bottom border after them, the top
 * border at the end of the frame */
static int fb_row(int line)
{
	if (line < TMS_H + BORDER)
		return line + BORDER;
	if (line >= LINES - BORDER)
		return line - (LINES - BORDER);
	return -1;
}

static void pv_run_frame(void)
{
	for (int line = 0; line < LINES; line++)
	{
		run_until(pv.frame_start + (uint64_t)line * LINE_CY);
		const int y = fb_row(line);
		if (y >= 0)
		{
			uint32_t *row = pv.fb + y * FB_W;
			const uint32_t back = tms_backdrop(&pv.vdp);
			fill(row, BORDER, back);
			fill(row + FB_W - BORDER, BORDER, back);
			if (line < TMS_H)
				tms_line(&pv.vdp, line, row + BORDER);
			else
				fill(row + BORDER, TMS_W, back);
		}
		if (line == TMS_H)
		{
			tms_frame(&pv.vdp);
			update_nmi();
			/* keyboard interrupt: a change in the keys held, if the BIOS asked for it */
			const uint8_t keys = any_key();
			if (pv.column == 0x0f && keys && keys != pv.keys_down)
				pv.cpu.irq = 1;
			pv.keys_down = keys;
		}
	}
	run_until(pv.frame_start + FRAME_CY);
	pv.frame_start += FRAME_CY;
	sound_sync();
}

static void pv_reset(void)
{
	z80_reset(&pv.cpu);
	pv.cpu.irq = 0;
	pv.cpu.irq_vector = 0xff;
	tms_reset(&pv.vdp);
	sn_reset(&pv.psg);
	memset(pv.ram, 0xff, sizeof(pv.ram));
	pv.column = pv.keys_down = pv.vdp_int = 0;
}

static int pv_load_bios(const uint8_t *data, size_t size, char *err, size_t errn)
{
	if (size != BIOS_SIZE)
	{
		if (err)
			strncpy(err, "the PV-2000 BIOS is 16 KB", errn);
		return 0;
	}
	memcpy(pv.bios, data, BIOS_SIZE);
	return 1;
}

static int pv_load(const uint8_t *rom, size_t size, char *err, size_t errn)
{
	if (size != 0x2000 && size != 0x4000)
	{
		if (err)
			strncpy(err, "PV-2000 cartridges are 8 or 16 KB", errn);
		return 0;
	}
	uint8_t bios[BIOS_SIZE];
	memcpy(bios, pv.bios, BIOS_SIZE);
	memset(&pv, 0, sizeof(pv));
	memcpy(pv.bios, bios, BIOS_SIZE);
	for (size_t i = 0; i < sizeof(pv.rom); i++)
		pv.rom[i] = rom[i % size];
	pv.cpu.read = mem_r;
	pv.cpu.write = mem_w;
	pv.cpu.in = io_r;
	pv.cpu.out = io_w;
	z80_init(&pv.cpu);
	pv_reset();
	return 1;
}

static const uint32_t *pv_framebuffer(void)
{
	return pv.fb;
}

static int pv_audio_take(int16_t *out, int max)
{
	int n = pv.alen < max ? pv.alen : max;
	memcpy(out, pv.abuf, n * sizeof(int16_t));
	memmove(pv.abuf, pv.abuf + n, (pv.alen - n) * sizeof(int16_t));
	pv.alen -= n;
	return n;
}

static void pv_set_pad(int pad, uint32_t buttons)
{
	if (pad >= 0 && pad < 2)
		pv.pad[pad] = buttons;
}

static const char *const bios_names[] = { "pv2000.zip", "hn613128pc64.bin", NULL };

const core core_pv2000 = {
	"Casio PV-2000", "pv2000",
	FB_W, FB_H,
	FB_W * 8.0 / 7.0 / FB_H,             /* pixel aspect 8:7 */
	(double)CLOCK / FRAME_CY,
	(double)CLOCK / SN_DIV, 1,
	pv_load, pv_reset, pv_run_frame, pv_framebuffer, pv_audio_take, pv_set_pad,
	pv_load_bios, 0x8f31f297, BIOS_SIZE,
	"The Casio PV-2000 needs its BIOS: the 16 KB ROM \"hn613128pc64.bin\". Choose that "
	"file, or a zip with it such as MAME's pv2000.zip.",
	bios_names,
	NULL,                                    /* no battery RAM */
	NULL, 0, 0, NULL, NULL,                  /* no second ROM */
};
