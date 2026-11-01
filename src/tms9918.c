/* Casiolator - TMS9918A video display processor (see tms9918.h)
 *
 * Registers: 0 bit 1 M3 (graphics II); 1 bit 6 display on, bit 5 interrupt enable, bit 4 M1
 * (text), bit 3 M2 (multicolour), bit 1 16x16 sprites, bit 0 magnified sprites; 2 name
 * table / 0x400; 3 colour table / 0x40; 4 pattern table / 0x800; 5 sprite attributes /
 * 0x80; 6 sprite patterns / 0x800; 7 text colour (high nibble) and backdrop (low). */
#include <string.h>
#include "tms9918.h"

/* colour 0 is transparent: it shows the backdrop */
static const uint32_t palette[16] = {
	0x000000, 0x000000, 0x21c842, 0x5edc78, 0x5455ed, 0x7d76fc, 0xd4524d, 0x42ebf5,
	0xfc5554, 0xff7978, 0xd4c154, 0xe6ce80, 0x21b03b, 0xc95bba, 0xcccccc, 0xffffff
};

void tms_reset(tms9918 *v)
{
	memset(v->reg, 0, sizeof(v->reg));
	v->status = 0;
	v->latch = v->second = 0;
	v->buffer = 0;
	v->addr = 0;
}

uint8_t tms_read(tms9918 *v, int port)
{
	v->second = 0;
	if (port & 1)
	{
		const uint8_t s = v->status;
		v->status &= 0x1f;
		return s;
	}
	const uint8_t d = v->buffer;
	v->buffer = v->vram[v->addr];
	v->addr = (v->addr + 1) & 0x3fff;
	return d;
}

void tms_write(tms9918 *v, int port, uint8_t data)
{
	if (!(port & 1))
	{
		v->second = 0;
		v->vram[v->addr] = data;
		v->buffer = data;
		v->addr = (v->addr + 1) & 0x3fff;
		return;
	}
	if (!v->second)
	{
		v->latch = data;
		v->second = 1;
		return;
	}
	v->second = 0;
	if (data & 0x80)
		v->reg[data & 7] = v->latch;
	else
	{
		v->addr = ((data & 0x3f) << 8) | v->latch;
		if (!(data & 0x40))
		{
			/* set up for reading: the first byte is fetched now */
			v->buffer = v->vram[v->addr];
			v->addr = (v->addr + 1) & 0x3fff;
		}
	}
}

int tms_int(const tms9918 *v)
{
	return (v->status & 0x80) && (v->reg[1] & 0x20);
}

void tms_frame(tms9918 *v)
{
	v->status |= 0x80;
}

uint32_t tms_backdrop(const tms9918 *v)
{
	return palette[v->reg[7] & 15];
}

static void pattern_8(uint32_t *out, uint8_t bits, uint8_t colors, uint32_t back)
{
	const uint32_t fg = colors >> 4 ? palette[colors >> 4] : back;
	const uint32_t bg = colors & 15 ? palette[colors & 15] : back;
	for (int i = 0; i < 8; i++)
		out[i] = bits & (0x80 >> i) ? fg : bg;
}

static void sprites(tms9918 *v, int y, uint32_t *out)
{
	const uint8_t *sat = v->vram + ((v->reg[5] & 0x7f) << 7);
	const uint16_t spg = (v->reg[6] & 7) << 11;
	const int big = v->reg[1] & 2, mag = v->reg[1] & 1;
	const int size = (big ? 16 : 8) << mag;
	uint8_t drawn[TMS_W + 32], hit[TMS_W + 32];  /* index x + 32: early clock goes left */
	memset(drawn, 0, sizeof(drawn));
	memset(hit, 0, sizeof(hit));
	int count = 0, i;
	for (i = 0; i < 32; i++)
	{
		const uint8_t *s = sat + i * 4;
		if (s[0] == 0xd0)
			break;
		int sy = s[0];
		if (sy > 0xe0)
			sy -= 256;
		int row = y - (sy + 1);
		if (row < 0 || row >= size)
			continue;
		if (++count > 4)
		{
			if (!(v->status & 0x40))
				v->status = (v->status & 0xa0) | 0x40 | i;
			return;
		}
		row >>= mag;
		const uint8_t name = big ? s[2] & 0xfc : s[2];
		const uint8_t *p = v->vram + ((spg + name * 8 + row) & 0x3fff);
		const uint16_t bits = big ? (p[0] << 8) | p[16] : p[0] << 8;
		const int x0 = s[1] - (s[3] & 0x80 ? 32 : 0);
		const int color = s[3] & 15;
		for (int px = 0; px < size; px++)
		{
			if (!(bits & (0x8000 >> (px >> mag))))
				continue;
			const int x = x0 + px;
			if (x < 0 || x >= TMS_W)
				continue;
			if (hit[x + 32])
				v->status |= 0x20;
			hit[x + 32] = 1;
			if (color && !drawn[x + 32])
			{
				drawn[x + 32] = 1;
				out[x] = palette[color];
			}
		}
	}
	/* no fifth sprite: the number is the last sprite looked at */
	if (!(v->status & 0x40))
		v->status = (v->status & 0xe0) | (i < 32 ? i : 31);
}

void tms_line(tms9918 *v, int y, uint32_t *out)
{
	const uint8_t *r = v->reg;
	const uint32_t back = palette[r[7] & 15];
	if (!(r[1] & 0x40))
	{
		for (int x = 0; x < TMS_W; x++)
			out[x] = back;
		return;
	}
	const uint16_t nt = (r[2] & 0x0f) << 10;
	const int row = y & 7;
	if (r[1] & 0x10)
	{
		/* text: 40 columns of 6 pixels between 8-pixel borders */
		const uint8_t *names = v->vram + nt + (y >> 3) * 40;
		const uint16_t pg = (r[4] & 7) << 11;
		uint8_t colors = r[7];
		for (int x = 0; x < 8; x++)
			out[x] = out[TMS_W - 8 + x] = back;
		for (int c = 0; c < 40; c++)
		{
			const uint8_t bits = v->vram[pg + names[c] * 8 + row];
			uint32_t tmp[8];
			pattern_8(tmp, bits, colors, back);
			memcpy(out + 8 + c * 6, tmp, 6 * sizeof(uint32_t));
		}
		return;
	}
	const uint8_t *names = v->vram + nt + (y >> 3) * 32;
	if (r[1] & 0x08)
	{
		/* multicolour: each name is 2x2 blocks of 4x4 pixels, two colours a byte */
		const uint16_t pg = (r[4] & 7) << 11;
		for (int c = 0; c < 32; c++)
		{
			const uint8_t b = v->vram[pg + names[c] * 8 + ((y >> 3) & 3) * 2 + ((y >> 2) & 1)];
			const uint32_t left = b >> 4 ? palette[b >> 4] : back;
			const uint32_t right = b & 15 ? palette[b & 15] : back;
			for (int i = 0; i < 4; i++)
			{
				out[c * 8 + i] = left;
				out[c * 8 + 4 + i] = right;
			}
		}
	}
	else if (r[0] & 0x02)
	{
		/* graphics II: a pattern and colour row per name in each third of the screen;
		 * registers 3 and 4 mask the table index */
		const unsigned third = (y >> 6) << 8;
		const unsigned pmask = ((r[4] & 3) << 8) | 0xff, cmask = ((r[3] & 0x7f) << 3) | 7;
		const uint16_t pbase = (r[4] & 4) << 11, cbase = (r[3] & 0x80) << 6;
		for (int c = 0; c < 32; c++)
		{
			const unsigned index = names[c] + third;
			pattern_8(out + c * 8, v->vram[pbase | ((index & pmask) << 3) | row],
				v->vram[cbase | ((index & cmask) << 3) | row], back);
		}
	}
	else
	{
		/* graphics I: one colour byte for each 8 names */
		const uint16_t pg = (r[4] & 7) << 11, ct = r[3] << 6;
		for (int c = 0; c < 32; c++)
			pattern_8(out + c * 8, v->vram[pg + names[c] * 8 + row], v->vram[ct + (names[c] >> 3)], back);
	}
	sprites(v, y, out);
}
