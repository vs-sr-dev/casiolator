/* Casiolator - Casio PV-1000 (1983)
 *
 * Z80 at 17.897725 MHz / 5 and the NEC D65010G031 video/sound chip (pixel clock / 4).
 * Hardware notes from MAME's pv1000.cpp (Wilbert Pol, plgDavid, lidnariq's test ROMs):
 *  - 0x0000-0x7fff cartridge, 0xb800-0xbbff name table (32x24), 0xbc00-0xbfff RAM that
 *    also holds 32 RAM tiles (codes 0xe0-0xff)
 *  - a tile is 32 bytes: 8 unused, then the red, green and blue planes (8 rows each)
 *  - on the 192 tile lines the chip holds the Z80 with BUSREQ from pixel 0 to 248 of each
 *    288-pixel line: the CPU only runs in the last 40 pixels and in the blanking lines
 *  - 16 "matrix" interrupts during blanking, 4 lines apart, plus a "prerender" one on line 20
 *  - three square waves at master / 1024 / period, with different resistor volumes
 *
 * Time is counted in master clocks: CPU cycle 5, pixel 4, line 1152, frame 262 lines. */
#include <string.h>
#include "core.h"
#include "z80.h"

#define MASTER     17897725
#define CPU_MC     5
#define PIX_MC     4
#define LINE_PIX   288
#define LINE_MC    (LINE_PIX * PIX_MC)
#define LINES      262
#define FRAME_MC   (LINE_MC * LINES)
#define SND_MC     1024

#define TILE_TOP   26           /* first tile line */
#define BUSREQ_END 248          /* pixel where the CPU gets the bus back */
#define IRQ_PIX    256

/* what the frontend shows: the 28 tile columns, 212 lines around the 192 tile lines */
#define FB_W       224
#define FB_H       212
#define FB_TOP     16           /* frame line shown on the first row */

#define ABUF       4096

static struct
{
	z80 cpu;
	uint8_t rom[0x8000];        /* the cartridge, mirrored over 32 KB */
	uint8_t vram[0x400];
	uint8_t gfxram[0x400];
	uint8_t matrix;             /* port 0xfd write: keyboard/joystick rows selected */
	uint8_t irq_enabled, irq_active;
	uint8_t pcg_bank, force_pattern, border, render_disable;
	uint64_t now;               /* CPU time, master clocks since reset */
	uint64_t frame_start;
	int line;                   /* line being run */

	struct { uint32_t count; uint16_t period; uint8_t val; } voice[3];
	uint8_t snd_ctrl;
	uint64_t snd_time;          /* master clock of the next sample */
	int32_t hp_in, hp_out;      /* DC blocker (the output is AC coupled) */
	int16_t abuf[ABUF];
	int alen;

	uint32_t pad[2];
	uint32_t fb[FB_W * FB_H];
} pv;

static const uint32_t palette[8] = {
	0x000000, 0xff0000, 0x00ff00, 0xffff00, 0x0000ff, 0xff00ff, 0x00ffff, 0xffffff
};

/* ---- sound ---- */

static void sound_sync(void)
{
	/* resistor mix: -6 dB, -3 dB, 0 dB */
	static const int volumes[3] = { 0x1000, 0x1800, 0x2000 };

	while (pv.snd_time < pv.now)
	{
		pv.snd_time += SND_MC;
		for (int i = 0; i < 3; i++)
		{
			pv.voice[i].count++;
			if (pv.voice[i].period && pv.voice[i].count >= pv.voice[i].period)
			{
				pv.voice[i].count = 0;
				pv.voice[i].val ^= 1;
			}
		}
		int32_t sum = 0;
		if (pv.snd_ctrl & 2)
		{
			if (pv.snd_ctrl & 1)
			{
				/* ring modulation */
				sum += (pv.voice[0].val ^ pv.voice[1].val) * volumes[0];
				sum += (pv.voice[1].val ^ pv.voice[2].val) * volumes[1];
			}
			else
			{
				sum += pv.voice[0].val * volumes[0];
				sum += pv.voice[1].val * volumes[1];
			}
			sum += pv.voice[2].val * volumes[2];
		}
		/* one-pole high-pass, ~15 Hz at 17.5 kHz */
		pv.hp_out = sum - pv.hp_in + (pv.hp_out * 4053 >> 12);
		pv.hp_in = sum;
		int32_t s = pv.hp_out;
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		if (pv.alen < ABUF)
			pv.abuf[pv.alen++] = (int16_t)s;
	}
}

static void voice_w(int ch, uint8_t data)
{
	sound_sync();
	if (ch == 3)
	{
		pv.snd_ctrl = data;
		return;
	}
	uint16_t per = ~data & 0x3f;
	if (per == 0 && pv.voice[ch].period != 0)
		pv.voice[ch].val ^= 1;  /* flips once and stays there */
	pv.voice[ch].period = per;
}

/* ---- bus ---- */

static uint8_t mem_r(void *c, uint16_t a)
{
	if (a < 0x8000)
		return pv.rom[a];
	if (a >= 0xb800 && a < 0xbc00)
		return pv.vram[a & 0x3ff];
	if (a >= 0xbc00 && a < 0xc000)
		return pv.gfxram[a & 0x3ff];
	return 0xff;
}

static void mem_w(void *c, uint16_t a, uint8_t v)
{
	if (a >= 0xb800 && a < 0xbc00)
		pv.vram[a & 0x3ff] = v;
	else if (a >= 0xbc00 && a < 0xc000)
		pv.gfxram[a & 0x3ff] = v;
}

static void update_irq(void)
{
	pv.cpu.irq = pv.irq_active != 0;
}

/* joystick matrix: rows selected by the last write to 0xfd, 4 bits each
 * row 0: select 1, start 1, select 2, start 2; row 1: down, right (P1, P2);
 * row 2: left, up; row 3: button 1, button 2 */
static uint8_t joystick(void)
{
	uint8_t data = 0;
	for (int row = 0; row < 4; row++)
	{
		if (!(pv.matrix & (1 << row)))
			continue;
		for (int p = 0; p < 2; p++)
		{
			uint32_t b = pv.pad[p];
			int lo = 0, hi = 0;
			switch (row)
			{
			case 0: lo = b & (1 << PAD_SELECT); hi = b & (1 << PAD_START); break;
			case 1: lo = b & (1 << PAD_DOWN); hi = b & (1 << PAD_RIGHT); break;
			case 2: lo = b & (1 << PAD_LEFT); hi = b & (1 << PAD_UP); break;
			default: lo = b & (1 << PAD_A); hi = b & (1 << PAD_B); break;
			}
			data |= ((lo ? 1 : 0) | (hi ? 2 : 0)) << (p * 2);
		}
	}
	return data;
}

/* MSB reads high on the two readable ports, everything else is pulled up */
static uint8_t io_r(void *c, uint16_t port)
{
	switch (port & 0xff)
	{
	case 0xfc:
		return 0x80 | (joystick() & 0x0c) | (pv.irq_active & 3);
	case 0xfd:
		if (pv.irq_active & 2)
		{
			pv.irq_active &= ~2;
			update_irq();
		}
		return 0x80 | joystick();
	default:
		return 0xff;
	}
}

static int line_pixel(void)
{
	return (int)((pv.now - pv.frame_start - (uint64_t)pv.line * LINE_MC) / PIX_MC);
}

static int tile_line(int line)
{
	return line >= TILE_TOP && line < TILE_TOP + 192;
}

static void render_line(int line);

static void io_w(void *c, uint16_t port, uint8_t v)
{
	switch (port & 0xff)
	{
	case 0xf8: case 0xf9: case 0xfa: case 0xfb:
		voice_w((port & 0xff) - 0xf8, v);
		break;
	case 0xfc:
		/* bit 1 matrix interrupt enable, bit 0 prerender interrupt enable */
		pv.irq_enabled = v & 3;
		pv.irq_active &= pv.irq_enabled;
		update_irq();
		break;
	case 0xfd:
		pv.irq_active &= ~1;    /* acknowledges the prerender interrupt */
		update_irq();
		pv.matrix = v;
		break;
	case 0xff:
		pv.pcg_bank = v >> 5;
		pv.force_pattern = (v >> 4) & 1;   /* RAM tile codes read ROM too (Dig Dug) */
		pv.border = v & 7;
		if (pv.render_disable && !(v & 8))
		{
			/* rendering back on mid-line: the bus is taken until pixel 248 */
			pv.render_disable = 0;
			if (tile_line(pv.line) && line_pixel() < BUSREQ_END)
			{
				render_line(pv.line);
				uint64_t until = pv.frame_start + (uint64_t)pv.line * LINE_MC + BUSREQ_END * PIX_MC;
				if (pv.now < until)
					pv.now = until;
			}
		}
		pv.render_disable = (v >> 3) & 1;
		break;
	}
}

/* ---- video ---- */

static void render_line(int line)
{
	if (line < FB_TOP || line >= FB_TOP + FB_H)
		return;
	uint32_t *out = pv.fb + (line - FB_TOP) * FB_W;
	if (pv.render_disable || !tile_line(line))
	{
		for (int x = 0; x < FB_W; x++)
			out[x] = palette[pv.border];
		return;
	}
	const int y = line - TILE_TOP, row = y & 7;
	const uint8_t *names = pv.vram + (y >> 3) * 32;
	for (int tx = 2; tx < 30; tx++)
	{
		unsigned code = names[tx];
		const uint8_t *t;
		if (code < 0xe0 || pv.force_pattern)
			t = pv.rom + ((((code + (pv.pcg_bank << 8)) * 32) + 8 + row) & 0x7fff);
		else
			t = pv.gfxram + (code - 0xe0) * 32 + 8 + row;
		/* t[0] red, t[8] green, t[16] blue */
		const uint8_t r = t[0], g = t[8], b = t[16];
		for (int px = 0; px < 8; px++)
		{
			const int s = 7 - px;
			*out++ = palette[((r >> s) & 1) | (((g >> s) & 1) << 1) | (((b >> s) & 1) << 2)];
		}
	}
}

/* ---- run ---- */

static void run_until(uint64_t t)
{
	while (pv.now < t)
		pv.now += (uint64_t)z80_step(&pv.cpu) * CPU_MC;
}

static int irq_line(int line)
{
	return (line >= 222 && line <= 258 && (line - 222) % 4 == 0) || (line <= 20 && line % 4 == 0);
}

static void pv_run_frame(void)
{
	for (pv.line = 0; pv.line < LINES; pv.line++)
	{
		const uint64_t ls = pv.frame_start + (uint64_t)pv.line * LINE_MC;
		run_until(ls);
		render_line(pv.line);
		if (tile_line(pv.line) && !pv.render_disable)
		{
			/* the chip reads the tiles with the CPU held off the bus */
			const uint64_t until = ls + BUSREQ_END * PIX_MC;
			if (pv.now < until)
				pv.now = until;
		}
		run_until(ls + IRQ_PIX * PIX_MC);
		if (irq_line(pv.line))
		{
			pv.irq_active |= 2 & pv.irq_enabled;
			if (pv.line == 20)
				pv.irq_active |= 1 & pv.irq_enabled;
			update_irq();
		}
	}
	pv.line = LINES - 1;
	run_until(pv.frame_start + FRAME_MC);
	pv.frame_start += FRAME_MC;
	sound_sync();
}

static void pv_reset(void)
{
	z80_reset(&pv.cpu);
	pv.matrix = 0;
	pv.irq_enabled = pv.irq_active = 0;
	pv.pcg_bank = pv.force_pattern = pv.border = pv.render_disable = 0;
	update_irq();
	memset(pv.voice, 0, sizeof(pv.voice));
	for (int i = 0; i < 3; i++)
		pv.voice[i].val = 1;
	pv.snd_ctrl = 0;
}

static int pv_load(const uint8_t *rom, size_t size, char *err, size_t errn)
{
	if (size != 0x2000 && size != 0x4000 && size != 0x8000)
	{
		if (err)
			strncpy(err, "PV-1000 cartridges are 8, 16 or 32 KB", errn);
		return 0;
	}
	memset(&pv, 0, sizeof(pv));
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

const core core_pv1000 = {
	"Casio PV-1000", "pv1000",
	FB_W, FB_H,
	FB_W * 48.0 / 35.0 / FB_H,           /* pixel aspect 48:35 */
	(double)MASTER / FRAME_MC,
	(double)MASTER / SND_MC,
	pv_load, pv_reset, pv_run_frame, pv_framebuffer, pv_audio_take, pv_set_pad,
	NULL,
};
