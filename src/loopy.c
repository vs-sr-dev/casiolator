/* Casiolator - Casio Loopy (1995)
 *
 * Hitachi SH7021 at 16 MHz with its 32 KB mask-ROM BIOS, 512 KB DRAM, the RH-7500 VDP at
 * 21.477272 MHz (1365 VDP cycles a line, 263 lines), cartridge ROM and battery SRAM.
 * Everything here comes from kasami's Loopy Hardware Megadoc, Loopy-Tools, Hitachi's
 * manuals and our own observation of the BIOS; no other emulator's code was read.
 *
 * CPU address: bits 26-24 select the area, bit 27 the bus width where it matters.
 *   0 BIOS ROM    1 DRAM (0x09)   2 SRAM (8-bit)   4 VDP (0x0c; 0x04 is an 8-bit mirror)
 *   5 on-chip peripherals          6 cartridge ROM (0x0e)          7 on-chip RAM (0x0f)
 *
 * Time is in CPU cycles; a VDP cycle is 16 / 21.477272 of one (2000000 / 2684659). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "core.h"
#include "loopy.h"

#define VDP_LINE      1365
#define VDP_LINES     263
#define CPU_NUM       2000000ull     /* CPU cycles = VDP cycles * CPU_NUM / CPU_DEN */
#define CPU_DEN       2684659ull
#define BIOS_SIZE     0x8000
#define DRAM_SIZE     0x80000

static struct
{
	sh1 cpu;
	sh7021 per;
	vdp v;
	uint8_t bios[BIOS_SIZE];
	int have_bios;
	uint8_t dram[DRAM_SIZE];
	uint8_t onchip[0x400];
	uint8_t *cart;
	uint32_t cart_size;
	uint8_t sram[0x8000];
	uint32_t sram_size;

	uint64_t cyc;                /* CPU cycles since power-on */
	uint64_t frame_vdp;          /* VDP cycle where this frame started */
	uint64_t next_per;           /* when the peripherals next need a sync */
	uint32_t pad[2];
	int trace;                   /* instructions left to print */
	int log;
	int16_t abuf[4096];
	int alen;
} L;

/* ---- time ---- */

static uint64_t cpu_at(uint64_t vdp_cycle)
{
	return (vdp_cycle * CPU_NUM + CPU_DEN - 1) / CPU_DEN;
}

static uint64_t vdp_now(void)
{
	return L.cyc * CPU_DEN / CPU_NUM;
}

static int hcount(void)
{
	const uint64_t p = (vdp_now() - L.frame_vdp) % VDP_LINE / 4;
	return (int)(p - 84) & 0x1ff;
}

static int vcount(void)
{
	const int line = (int)((vdp_now() - L.frame_vdp) / VDP_LINE);
	const int top = L.v.height == 240 ? 23 : 39;
	return (line - top) & 0x1ff;
}

/* ---- VDP and MMIO (area 4) ---- */

static uint16_t controller(int idx)
{
	/* matrix mode, gamepad: latch 0 DET ST TL TR, 1 A D C B, 2 up down left right */
	if (!(L.v.reg[V_MODE] & 0x10))
		return 0;
	const uint32_t b = L.pad[0];
	uint8_t lat[6] = { 0 };
	lat[0] = 1 | ((b >> PAD_START & 1) << 1) | ((b >> PAD_L & 1) << 2) | ((b >> PAD_R & 1) << 3);
	lat[1] = (b >> PAD_A & 1) | ((b >> PAD_D & 1) << 1) | ((b >> PAD_C & 1) << 2) | ((b >> PAD_B & 1) << 3);
	lat[2] = (b >> PAD_UP & 1) | ((b >> PAD_DOWN & 1) << 1) | ((b >> PAD_LEFT & 1) << 2) | ((b >> PAD_RIGHT & 1) << 3);
	return (uint16_t)((lat[idx * 2 + 1] << 8) | lat[idx * 2]);
}

static uint16_t vdp_r16(uint32_t a)
{
	vdp *v = &L.v;
	const uint32_t o = a & 0xffffe;
	uint16_t r;
	if (o < 0x40000)
		r = (uint16_t)((v->bitmap[o & 0x1ffff] << 8) | v->bitmap[(o & 0x1ffff) + 1]);
	else if (o < 0x50000)
		r = (uint16_t)((v->tile[o & 0xffff] << 8) | v->tile[(o & 0xffff) + 1]);
	else if (o < 0x50200)
		r = (uint16_t)((v->oam[o & 0x1ff] << 8) | v->oam[(o & 0x1ff) + 1]);
	else if (o >= 0x51000 && o < 0x51200)
		r = (uint16_t)((v->pal[o & 0x1ff] << 8) | v->pal[(o & 0x1ff) + 1]);
	else if (o >= 0x52000 && o < 0x52200)
		r = (uint16_t)((v->capture[o & 0x1ff] << 8) | v->capture[(o & 0x1ff) + 1]);
	else if (o >= 0x58000 && o < 0x60000)
	{
		const uint32_t w = (o - 0x58000) >> 1;
		switch (o)
		{
		case 0x58002: r = (uint16_t)hcount(); break;
		case 0x58004: r = (uint16_t)vcount(); break;
		case 0x5d010: case 0x5d012: case 0x5d014: r = controller((o - 0x5d010) >> 1); break;
		case 0x5d030:
			/* no seal cartridge: SCT 0, paper sensors 1, ink 0; NTSC jumper */
			r = (uint16_t)((v->reg[w] & 0x100) | 0x07);
			break;
		case 0x5d000: r = 0x1c0 << 6 & 0xffc0; break;      /* print head at 28 C */
		case 0x5d050: case 0x5d052: r = 0; break;
		default: r = v->reg[w]; break;
		}
	}
	else if (o == 0x60000)
		r = v->sync_shift;
	else
		r = v->latch;
	v->latch = r;
	return r;
}

static void fill_row(uint32_t o)
{
	vdp *v = &L.v;
	const uint32_t y = (o >> 1) & 0x1ff;
	const uint8_t fill = (uint8_t)v->reg[V_FILL_VALUE], mask = (uint8_t)v->reg[V_FILL_MASK];
	uint8_t *row = v->bitmap + (y * 256 & 0x1ffff);
	for (int x = 0; x < 256; x++)
		row[x] = (uint8_t)((row[x] & ~mask) | (fill & mask));
}

static void vdp_w16(uint32_t a, uint16_t d)
{
	vdp *v = &L.v;
	const uint32_t o = a & 0xffffe;
	v->latch = d;
	if (o < 0x40000)
	{
		v->bitmap[o & 0x1ffff] = (uint8_t)(d >> 8);
		v->bitmap[(o & 0x1ffff) + 1] = (uint8_t)d;
	}
	else if (o < 0x50000)
	{
		v->tile[o & 0xffff] = (uint8_t)(d >> 8);
		v->tile[(o & 0xffff) + 1] = (uint8_t)d;
	}
	else if (o < 0x50200)
	{
		v->oam[o & 0x1ff] = (uint8_t)(d >> 8);
		v->oam[(o & 0x1ff) + 1] = (uint8_t)d;
	}
	else if (o >= 0x51000 && o < 0x51200)
	{
		v->pal[o & 0x1ff] = (uint8_t)(d >> 8);
		v->pal[(o & 0x1ff) + 1] = (uint8_t)d;
	}
	else if (o >= 0x58000 && o < 0x60000)
	{
		const uint32_t w = (o - 0x58000) >> 1;
		if (o >= 0x5f000 && o < 0x5f400)
		{
			fill_row(o);
			return;
		}
		switch (o)
		{
		case 0x58000:
			v->reg[w] = d;
			v->height = (d & 2) ? 240 : 224;
			return;
		case 0x58006:                                       /* triggers */
			return;
		case 0x5d042:
		case 0x5d044:
			if (!(v->reg[V_PRINT_STAT] & 0x100))
				return;                                     /* printer writes disabled */
			break;
		}
		v->reg[w] = d;
	}
	else if (o == 0x60000)
		v->sync_shift = d;
	else if (o >= 0x80000 && o < 0xa0000)
		v->sound_ctrl = d;
	else if (L.log)
		fprintf(stderr, "vdp: write %06x = %04x\n", o, d);
}

/* ---- bus ---- */

static int area(uint32_t a)
{
	return (a >> 24) & 7;
}

/* bus cycles for one access of the area's width */
static int cost(uint32_t a)
{
	switch (area(a))
	{
	case 0: case 1: case 7: return 1;
	case 4: return ((a & 0xfffff) < 0x58000) ? 4 : 3;
	default: return 3;
	}
}

static int bus_cost(void *c, uint32_t a)
{
	return cost(a);
}

static uint8_t r8(void *c, uint32_t a)
{
	switch (area(a))
	{
	case 0: return L.bios[a & (BIOS_SIZE - 1)];
	case 1: return L.dram[a & (DRAM_SIZE - 1)];
	case 2: return L.sram_size ? L.sram[a % L.sram_size] : 0xff;
	case 4: {
		const uint16_t w = vdp_r16(a);
		return (a & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
	}
	case 5: return sh7021_r8(&L.per, a);
	case 6: return L.cart_size ? L.cart[(a & 0x3fffff) % L.cart_size] : 0xff;
	case 7: return L.onchip[a & 0x3ff];
	}
	if (L.log)
		fprintf(stderr, "bus: read8 %08x at %08x\n", a, L.cpu.pc);
	return 0xff;
}

static uint16_t r16(void *c, uint32_t a)
{
	switch (area(a))
	{
	case 4: return vdp_r16(a);
	case 5: return sh7021_r16(&L.per, a);
	default: return (uint16_t)((r8(c, a) << 8) | r8(c, a + 1));
	}
}

static void w8(void *c, uint32_t a, uint8_t v)
{
	switch (area(a))
	{
	case 1: L.dram[a & (DRAM_SIZE - 1)] = v; return;
	case 2: if (L.sram_size) L.sram[a % L.sram_size] = v; if (L.log) fprintf(stderr, "sram %08x=%02x\n", a, v); return;
	case 4:
		if (a & 0x08000000)
		{
			/* 16-bit bus: the other byte is read back and kept */
			const uint16_t w = vdp_r16(a);
			vdp_w16(a, (a & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0xff) | (v << 8)));
		}
		else
			vdp_w16(a & ~1u, v);                            /* 8-bit mirror: upper byte undefined */
		return;
	case 5: sh7021_w8(&L.per, a, v); return;
	case 7: L.onchip[a & 0x3ff] = v; return;
	}
	if (L.log)
		fprintf(stderr, "bus: write8 %08x = %02x at %08x\n", a, v, L.cpu.pc);
}

static void w16(void *c, uint32_t a, uint16_t v)
{
	switch (area(a))
	{
	case 4: vdp_w16(a, v); return;
	case 5: sh7021_w16(&L.per, a, v); return;
	default: w8(c, a, (uint8_t)(v >> 8)); w8(c, a + 1, (uint8_t)v); return;
	}
}

/* CPU-side accessors: also count the bus time */
static uint8_t cpu_r8(void *c, uint32_t a)
{
	L.cpu.wait += cost(a) - 1;
	L.per.now = L.cyc;
	return r8(c, a);
}

static uint16_t cpu_r16(void *c, uint32_t a)
{
	L.cpu.wait += cost(a) - 1;
	L.per.now = L.cyc;
	return r16(c, a);
}

static uint32_t cpu_r32(void *c, uint32_t a)
{
	const int ar = area(a);
	L.cpu.wait += (ar == 0 || ar == 7 || ar == 5) ? cost(a) - 1 : 2 * cost(a) - 1;
	L.per.now = L.cyc;
	return ((uint32_t)r16(c, a) << 16) | r16(c, a + 2);
}

static void cpu_w8(void *c, uint32_t a, uint8_t v)
{
	L.cpu.wait += cost(a) - 1;
	L.per.now = L.cyc;
	w8(c, a, v);
	if (area(a) == 5)
		L.next_per = L.cyc;
}

static void cpu_w16(void *c, uint32_t a, uint16_t v)
{
	L.cpu.wait += cost(a) - 1;
	L.per.now = L.cyc;
	w16(c, a, v);
	if (area(a) == 5)
		L.next_per = L.cyc;
}

static void cpu_w32(void *c, uint32_t a, uint32_t v)
{
	const int ar = area(a);
	L.cpu.wait += (ar == 0 || ar == 7 || ar == 5) ? cost(a) - 1 : 2 * cost(a) - 1;
	L.per.now = L.cyc;
	w16(c, a, (uint16_t)(v >> 16));
	w16(c, a + 2, (uint16_t)v);
	if (ar == 5)
		L.next_per = L.cyc;
}

static uint16_t cpu_fetch(void *c, uint32_t a)
{
	const int ar = area(a);
	if (ar != 0 && ar != 7)
		L.cpu.wait += cost(a) - 1;
	return r16(c, a);
}

static void cpu_ack(void *c, int vector)
{
	sh7021_ack(&L.per, vector);
}

static void sci_tx(void *c, int ch, uint8_t b)
{
	if (L.log)
		fprintf(stderr, "sci%d: %02x\n", ch, b);
}

/* ---- run ---- */

static void run_until(uint64_t t)
{
	while (L.cyc < t)
	{
		if (L.cyc >= L.next_per)
		{
			L.per.now = L.cyc;
			sh7021_sync(&L.per);
			L.next_per = L.cyc + sh7021_next_event(&L.per);
		}
		if (L.trace > 0 && !L.cpu.sleeping)
		{
			char d[64];
			const uint32_t pc = L.cpu.pc;
			sh1_disasm(r16(NULL, pc), pc, d, sizeof(d));
			fprintf(stderr, "%08x  %-28s r0=%08x r1=%08x r2=%08x r3=%08x r15=%08x sr=%03x\n",
				pc, d, L.cpu.r[0], L.cpu.r[1], L.cpu.r[2], L.cpu.r[3], L.cpu.r[15], L.cpu.sr);
			L.trace--;
		}
		if (L.cpu.sleeping && L.cpu.irq_level <= (int)((L.cpu.sr >> 4) & 15))
		{
			/* asleep: nothing happens until the next event */
			uint64_t to = L.next_per < t ? L.next_per : t;
			L.cyc = to > L.cyc ? to : L.cyc + 1;
			continue;
		}
		L.per.dma_cycles = 0;
		const int n = sh1_step(&L.cpu);
		L.cyc += (uint64_t)n + L.per.dma_cycles;
	}
}

static void lp_run_frame(void)
{
	vdp *v = &L.v;
	for (int line = 0; line < VDP_LINES; line++)
	{
		const uint64_t ls = L.frame_vdp + (uint64_t)line * VDP_LINE;
		run_until(cpu_at(ls));
		v->line = line;
		const int top = v->height == 240 ? 23 : 39;
		const int vc = line - top;                          /* -top..height-1 */

		if (line == 0)
		{
			/* vertical blanking starts: NMI, and IRQ1 if it is set to V-sync */
			if (v->reg[V_IRQ_CTRL] & 0x04)
				sh7021_nmi(&L.per);
			if ((v->reg[V_SYNC_IRQ] & 3) == 1)
			{
				sh7021_set_irq(&L.per, 1, 1);
				sh7021_set_irq(&L.per, 1, 0);
			}
		}
		if (vc >= 0)
		{
			vdp_render_line(v, vc);
			if ((v->reg[V_SYNC_IRQ] & 3) == 3)
			{
				run_until(cpu_at(ls + 84 * 4));
				sh7021_set_irq(&L.per, 1, 1);
				sh7021_set_irq(&L.per, 1, 0);
			}
		}
		/* raster IRQ0: H compare, and V compare within the picture if enabled */
		const uint16_t ic = v->reg[V_IRQ_CTRL];
		if ((ic & 0x82) == 0x82)
		{
			const int vcmp = v->reg[V_IRQ_VCMP] & 0x1ff;
			if (!(ic & 0x20) || (vc >= 0 && vc == vcmp))
			{
				const int pix = ((v->reg[V_IRQ_HCMP] & 0x1ff) + 84) & 0x1ff;
				if (pix < 342)
				{
					run_until(cpu_at(ls + (uint64_t)pix * 4));
					sh7021_set_irq(&L.per, 0, 1);
					sh7021_set_irq(&L.per, 0, 0);
				}
			}
		}
	}
	L.frame_vdp += (uint64_t)VDP_LINES * VDP_LINE;
	run_until(cpu_at(L.frame_vdp));
	if (L.log)
		fprintf(stderr, "frame end: pc %08x sr %03x vbr %08x\n", L.cpu.pc, L.cpu.sr, L.cpu.vbr);

	/* silence until the sound chip is in: 32 kHz, carrying the fraction over */
	static double frac;
	frac += 32000.0 * VDP_LINE * VDP_LINES / 21477272.0;
	int n = (int)frac;
	frac -= n;
	if (L.alen + n <= (int)(sizeof(L.abuf) / sizeof(L.abuf[0])))
	{
		memset(L.abuf + L.alen, 0, n * sizeof(int16_t));
		L.alen += n;
	}
}

static void lp_reset(void)
{
	memset(L.dram, 0, sizeof(L.dram));
	memset(L.onchip, 0, sizeof(L.onchip));
	memset(&L.v, 0, offsetof(vdp, fb));
	L.v.height = 224;
	L.per.cpu = &L.cpu;
	L.per.ctx = NULL;
	L.per.sci_tx = sci_tx;
	L.per.bus_r8 = r8;
	L.per.bus_r16 = r16;
	L.per.bus_w8 = w8;
	L.per.bus_w16 = w16;
	L.per.bus_cost = bus_cost;
	L.per.log_unknown = L.log;
	sh7021_reset(&L.per);
	L.cpu.fetch = cpu_fetch;
	L.cpu.read8 = cpu_r8;
	L.cpu.read16 = cpu_r16;
	L.cpu.read32 = cpu_r32;
	L.cpu.write8 = cpu_w8;
	L.cpu.write16 = cpu_w16;
	L.cpu.write32 = cpu_w32;
	L.cpu.irq_ack = cpu_ack;
	sh1_reset(&L.cpu);
	L.next_per = 0;
}

static int lp_load_bios(const uint8_t *data, size_t size, char *err, size_t errn)
{
	if (size != BIOS_SIZE)
	{
		snprintf(err, errn, "the Loopy BIOS is the SH7021's 32 KB ROM (HD6437021)");
		return 0;
	}
	memcpy(L.bios, data, BIOS_SIZE);
	L.have_bios = 1;
	return 1;
}

static int lp_load(const uint8_t *rom, size_t size, char *err, size_t errn)
{
	if (!L.have_bios)
	{
		snprintf(err, errn, "the Loopy BIOS is needed");
		return 0;
	}
	free(L.cart);
	L.cart = malloc(size);
	if (!L.cart)
		return 0;
	memcpy(L.cart, rom, size);
	L.cart_size = (uint32_t)size;

	/* SRAM size from the header (first and last byte address) */
	const uint32_t first = (uint32_t)(rom[0x10] << 24 | rom[0x11] << 16 | rom[0x12] << 8 | rom[0x13]);
	const uint32_t last = (uint32_t)(rom[0x14] << 24 | rom[0x15] << 16 | rom[0x16] << 8 | rom[0x17]);
	L.sram_size = 0;
	if ((first >> 24) == 2 && (last >> 24) == 2 && last > first && last - first < sizeof(L.sram))
		L.sram_size = last + 1 - first;
	memset(L.sram, 0, sizeof(L.sram));
	L.cyc = 0;
	L.frame_vdp = 0;
	L.alen = 0;
	const char *t = getenv("CL_TRACE");
	L.trace = t ? atoi(t) : 0;
	L.log = getenv("CL_LOG") != NULL;
	lp_reset();
	return 1;
}

static const uint32_t *lp_framebuffer(void)
{
	return L.v.fb;
}

static int lp_audio_take(int16_t *out, int max)
{
	int n = L.alen < max ? L.alen : max;
	memcpy(out, L.abuf, n * sizeof(int16_t));
	memmove(L.abuf, L.abuf + n, (L.alen - n) * sizeof(int16_t));
	L.alen -= n;
	return n;
}

static void lp_set_pad(int pad, uint32_t buttons)
{
	if (pad >= 0 && pad < 2)
		L.pad[pad] = buttons;
}

static uint8_t *lp_save_ram(size_t *size)
{
	*size = L.sram_size;
	return L.sram_size ? L.sram : NULL;
}

static const char *const s_bios_names[] = {
	"hd6437021.lsi302", "casloopy.zip", "bios/hd6437021.lsi302", "bios/casloopy.zip", "loopy_bios.bin", NULL
};

const core core_loopy = {
	"Casio Loopy", "loopy",
	LOOPY_W, 224,
	4.0 / 3.0,
	21477272.0 / (VDP_LINE * VDP_LINES),
	32000.0,
	lp_load, lp_reset, lp_run_frame, lp_framebuffer, lp_audio_take, lp_set_pad,
	lp_load_bios,
	0x8c57ff9f, BIOS_SIZE,
	"The Casio Loopy needs its BIOS: the 32 KB ROM inside the SH7021 CPU (HD6437021, "
	"\"hd6437021.lsi302\"). Choose that file, or a zip with it such as MAME's casloopy.zip.",
	s_bios_names,
	lp_save_ram,
};
