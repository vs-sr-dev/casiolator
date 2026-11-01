/* Casiolator - SH7021 on-chip peripherals (see sh7021.h)
 *
 * Register layout and behaviour of the SH-1 family modules. Timer counts and serial
 * frames are computed from elapsed cycles when something looks at them (sh7021_sync),
 * and sh7021_next_event tells the machine when an interrupt can next happen. */
#include <stdio.h>
#include <string.h>
#include "sh7021.h"

#define NEVER 0x7fffffffu

/* vector of each source */
static const uint8_t s_vector[INT_COUNT] = {
	64, 65, 66, 67, 68, 69, 70, 71,
	72, 74, 76, 78,
	80, 81, 82, 84, 85, 86, 88, 89, 90, 92, 93, 94, 96, 97, 98,
	100, 101, 102, 103, 104, 105, 106, 107,
	112, 113,
};

/* priority level of a source, from IPRA-IPRE */
static int level(sh7021 *s, int src)
{
	int reg, shift;
	if (src <= INT_IRQ7)
	{
		reg = src >> 2;
		shift = 12 - (src & 3) * 4;
	}
	else if (src <= INT_DEI3)
	{
		reg = 2;
		shift = src <= INT_DEI1 ? 12 : 8;
	}
	else if (src <= INT_OVI4)
	{
		const int ch = (src - INT_IMIA0) / 3;   /* ITU0 IPRC 7-4, ITU1 IPRC 3-0, ITU2-4 IPRD */
		reg = ch < 2 ? 2 : 3;
		shift = ch == 0 ? 4 : ch == 1 ? 0 : 12 - (ch - 2) * 4;
	}
	else if (src <= INT_TEI0)
	{
		reg = 3;
		shift = 0;
	}
	else if (src <= INT_TEI1)
	{
		reg = 4;
		shift = 12;
	}
	else
	{
		reg = 4;
		shift = 4;
	}
	return (s->ipr[reg] >> shift) & 15;
}

static int pending(sh7021 *s, int src)
{
	if (src <= INT_IRQ7)
	{
		const int edge = (s->icr >> (7 - src)) & 1;
		return edge ? s->irq_edge[src] : s->irq_pin[src];
	}
	if (src <= INT_DEI3)
	{
		const uint16_t chcr = s->dma[src - INT_DEI0].chcr;
		return (chcr & 6) == 6;                 /* TE and IE */
	}
	if (src <= INT_OVI4)
	{
		const itu_ch *t = &s->itu[(src - INT_IMIA0) / 3];
		const int bit = 1 << ((src - INT_IMIA0) % 3);
		return (t->tsr & t->tier & bit) != 0;
	}
	if (src <= INT_TEI1)
	{
		const sci_ch *c = &s->sci[(src - INT_ERI0) / 4];
		switch ((src - INT_ERI0) % 4)
		{
		case 0: return (c->scr & 0x40) && (c->ssr & 0x38);
		case 1: return (c->scr & 0x40) && (c->ssr & 0x40);
		case 2: return (c->scr & 0x80) && (c->ssr & 0x80);
		default: return (c->scr & 0x04) && (c->ssr & 0x04);
		}
	}
	return 0;
}

static void update_irq(sh7021 *s)
{
	int best = 0, vec = 0;
	for (int i = 0; i < INT_COUNT; i++)
	{
		const int l = level(s, i);
		if (l > best && pending(s, i))
		{
			best = l;
			vec = s_vector[i];
		}
	}
	if (s->nmi_pending)
	{
		best = 16;
		vec = VEC_NMI;
	}
	s->cpu->irq_level = best;
	s->cpu->irq_vector = vec;
}

void sh7021_set_irq(sh7021 *s, int n, int lvl)
{
	if (lvl && !s->irq_pin[n])
		s->irq_edge[n] = 1;
	s->irq_pin[n] = (uint8_t)lvl;
	update_irq(s);
}

void sh7021_nmi(sh7021 *s)
{
	s->nmi_pending = 1;
	update_irq(s);
}

void sh7021_ack(sh7021 *s, int vector)
{
	if (vector == VEC_NMI)
		s->nmi_pending = 0;
	else if (vector >= 64 && vector <= 71)
		s->irq_edge[vector - 64] = 0;
	update_irq(s);
}

/* ---- DMAC ---- */

static void dma_unit(sh7021 *s, dma_ch *d)
{
	const int word = (d->chcr >> 3) & 1, size = word ? 2 : 1;
	const int sm = (d->chcr >> 12) & 3, dm = (d->chcr >> 14) & 3;
	const uint32_t sar = d->sar, dar = d->dar;
	const uint16_t v = word ? s->bus_r16(s->ctx, sar) : s->bus_r8(s->ctx, sar);
	s->dma_cycles += s->bus_cost(s->ctx, sar) + s->bus_cost(s->ctx, dar);
	/* the channel moves on before the write: writing SCI TDR can ask for the next
	 * transfer at once (sci_start), which must see the next address */
	if (sm == 1) d->sar += size;
	else if (sm == 2) d->sar -= size;
	if (dm == 1) d->dar += size;
	else if (dm == 2) d->dar -= size;
	if (--d->tcr == 0)
		d->chcr |= 2;                           /* TE */
	if (word)
		s->bus_w16(s->ctx, dar, v);
	else
		s->bus_w8(s->ctx, dar, (uint8_t)v);
}

static int dma_active(sh7021 *s, dma_ch *d)
{
	return (s->dmaor & 1) && !(s->dmaor & 6) && (d->chcr & 3) == 1;
}

static void sci_start(sh7021 *s, int ch);

/* auto-request channels run to the end as soon as they are enabled; a channel on TXI
 * starts if the transmitter is already waiting */
static void dma_check(sh7021 *s)
{
	sci_start(s, 0);
	sci_start(s, 1);
	for (int i = 0; i < 4; i++)
	{
		dma_ch *d = &s->dma[i];
		if (!dma_active(s, d) || ((d->chcr >> 8) & 15) != 0xc)
			continue;
		do
			dma_unit(s, d);
		while (!(d->chcr & 2));
	}
	update_irq(s);
}

/* a peripheral asked for one transfer (RS code); 1 if a channel took it */
static int dma_request(sh7021 *s, int rs)
{
	int done = 0;
	for (int i = 0; i < 4; i++)
	{
		dma_ch *d = &s->dma[i];
		if (dma_active(s, d) && ((d->chcr >> 8) & 15) == rs)
		{
			dma_unit(s, d);
			done = 1;
		}
	}
	return done;
}

/* ---- ITU ---- */

static uint32_t itu_div(const itu_ch *t)
{
	const int tpsc = t->tcr & 7;
	return tpsc < 4 ? 1u << tpsc : 0;          /* external clocks are not wired */
}

/* the value after which the counter goes back to 0 */
static uint16_t itu_top(const itu_ch *t)
{
	switch ((t->tcr >> 5) & 3)
	{
	case 1: return t->gra;
	case 2: return t->grb;
	default: return 0xffff;
	}
}

static void itu_flag(sh7021 *s, int ch, int bit)
{
	itu_ch *t = &s->itu[ch];
	t->tsr |= bit;
	/* IMIA0-3 with IMIEA set can start a DMA transfer, which clears IMFA */
	if (bit == 1 && ch < 4 && (t->tier & 1) && dma_request(s, 8 + ch))
		t->tsr &= ~1;
}

/* the counter steps n times. A compare match flag is set as the counter leaves the
 * matching value (manual 10.4: "in the last state in which the values match"); at the
 * clearing value it goes back to 0, and leaving 0xffff is an overflow */
static void itu_count(sh7021 *s, int ch, uint32_t n)
{
	itu_ch *t = &s->itu[ch];
	while (n)
	{
		const uint16_t top = itu_top(t) >= t->tcnt ? itu_top(t) : 0xffff;
		uint32_t d = (uint32_t)(top - t->tcnt) + 1;
		if (t->gra >= t->tcnt && (uint32_t)(t->gra - t->tcnt) + 1 < d) d = (uint32_t)(t->gra - t->tcnt) + 1;
		if (t->grb >= t->tcnt && (uint32_t)(t->grb - t->tcnt) + 1 < d) d = (uint32_t)(t->grb - t->tcnt) + 1;
		if (d > n)
		{
			t->tcnt += (uint16_t)n;
			return;
		}
		n -= d;
		const uint16_t left = (uint16_t)(t->tcnt + d - 1);
		if (left == t->gra) itu_flag(s, ch, 1);
		if (left == t->grb) itu_flag(s, ch, 2);
		if (left == top)
		{
			t->tcnt = 0;
			if (top == 0xffff)
				itu_flag(s, ch, 4);
		}
		else
			t->tcnt = (uint16_t)(left + 1);
	}
}

/* counts until the next flag of a channel */
static uint32_t itu_until_flag(const itu_ch *t)
{
	const uint16_t top = itu_top(t) >= t->tcnt ? itu_top(t) : 0xffff;
	uint32_t d = top == 0xffff ? (uint32_t)(0xffff - t->tcnt) + 1 : 0x20000;
	if (t->gra >= t->tcnt && (uint32_t)(t->gra - t->tcnt) + 1 < d) d = (uint32_t)(t->gra - t->tcnt) + 1;
	if (t->grb >= t->tcnt && (uint32_t)(t->grb - t->tcnt) + 1 < d) d = (uint32_t)(t->grb - t->tcnt) + 1;
	if (d == 0x20000)
		d = (uint32_t)(top - t->tcnt) + 1;     /* cleared before any flag: look again from 0 */
	return d;
}

static void itu_sync(sh7021 *s)
{
	uint64_t dt = s->now - s->itu_time;
	s->itu_time = s->now;
	if (!dt)
		return;
	for (int ch = 0; ch < 5; ch++)
	{
		itu_ch *t = &s->itu[ch];
		const uint32_t div = itu_div(t);
		if (!(s->tstr & (1 << ch)) || !div)
			continue;
		uint64_t total = t->sub + dt;
		itu_count(s, ch, (uint32_t)(total / div));
		t->sub = (uint32_t)(total % div);
	}
}

/* ---- SCI ---- */

static uint32_t sci_frame(const sci_ch *c)
{
	const int bits = 1 + ((c->smr & 0x40) ? 7 : 8) + ((c->smr & 0x20) ? 1 : 0) + ((c->smr & 0x08) ? 2 : 1);
	return (uint32_t)bits * (32u << (2 * (c->smr & 3))) * (c->brr + 1u);
}

/* TDR empty with TXI enabled: a DMA channel on TXI fills it (and clears TDRE, 15.3.2) */
static void sci_txi_dma(sh7021 *s, int ch)
{
	sci_ch *c = &s->sci[ch];
	if (!(c->ssr & 0x80) || (c->scr & 0xa0) != 0xa0)
		return;
	/* cleared first: the TDR write below comes back through sci_start */
	c->ssr &= ~0x80;
	if (!dma_request(s, ch ? 7 : 5))
		c->ssr |= 0x80;
}

/* shift register empty and a byte waiting: start sending it */
static void sci_start(sh7021 *s, int ch)
{
	sci_ch *c = &s->sci[ch];
	sci_txi_dma(s, ch);
	if (c->tx_busy || (c->ssr & 0x80) || !(c->scr & 0x20))
		return;
	c->tsr = c->tdr;
	c->tx_busy = 1;
	c->tx_done = s->now + sci_frame(c);
	c->ssr |= 0x80;                             /* TDRE: the data register is free again */
	c->ssr &= ~0x04;
	sci_txi_dma(s, ch);
}

static void sci_sync(sh7021 *s)
{
	for (int ch = 0; ch < 2; ch++)
	{
		sci_ch *c = &s->sci[ch];
		while (c->tx_busy && s->now >= c->tx_done)
		{
			c->tx_busy = 0;
			if (s->sci_tx)
				s->sci_tx(s->ctx, ch, c->tsr);
			const uint64_t end = c->tx_done;
			sci_start(s, ch);
			if (c->tx_busy)
				c->tx_done = end + sci_frame(c);
			else
				c->ssr |= 0x04;                 /* TEND */
		}
	}
}

void sh7021_sync(sh7021 *s)
{
	itu_sync(s);
	sci_sync(s);
	update_irq(s);
}

uint32_t sh7021_next_event(sh7021 *s)
{
	uint64_t best = NEVER;
	for (int ch = 0; ch < 5; ch++)
	{
		const itu_ch *t = &s->itu[ch];
		const uint32_t div = itu_div(t);
		if (!(s->tstr & (1 << ch)) || !div || !(t->tier & 7))
			continue;
		const uint64_t cyc = (uint64_t)itu_until_flag(t) * div - t->sub;
		if (cyc < best)
			best = cyc;
	}
	for (int ch = 0; ch < 2; ch++)
		if (s->sci[ch].tx_busy)
		{
			const uint64_t cyc = s->sci[ch].tx_done > s->now ? s->sci[ch].tx_done - s->now : 1;
			if (cyc < best)
				best = cyc;
		}
	return best ? (uint32_t)best : 1;
}

/* ---- registers ---- */

static void unknown(sh7021 *s, const char *what, uint32_t a, uint32_t v)
{
	if (s->log_unknown)
		fprintf(stderr, "sh7021: %s %08x = %x\n", what, a, v);
}

/* 16-bit registers, by offset in the 0x200 block (0 if a byte register) */
static int is_word(uint32_t o)
{
	if (o >= 0x104 && o < 0x140)
	{
		/* ITU: TCNT, GRA, GRB (and BRA, BRB on 3 and 4) */
		static const uint8_t base[5] = { 0x04, 0x0e, 0x18, 0x22, 0x32 };
		for (int ch = 0; ch < 5; ch++)
		{
			const uint32_t r = o - 0x100 - base[ch];
			if (r < 0x0e && r >= 4 && (ch >= 3 || r < 0x0a))
				return 1;
		}
		return 0;
	}
	if (o >= 0x140 && o < 0x180) return 1;      /* DMAC */
	if (o >= 0x184 && o < 0x190) return 1;      /* INTC */
	if (o >= 0x1a0 && o < 0x1b4) return 1;      /* BSC */
	if (o >= 0x1c0 && o < 0x1d0) return 1;      /* ports */
	if (o == 0x1ee) return 1;
	return 0;
}

static itu_ch *itu_reg(sh7021 *s, uint32_t o, uint32_t *r)
{
	static const uint8_t base[5] = { 0x04, 0x0e, 0x18, 0x22, 0x32 };
	static const uint8_t len[5] = { 0x0a, 0x0a, 0x0a, 0x0e, 0x0e };
	for (int ch = 0; ch < 5; ch++)
		if (o >= 0x100u + base[ch] && o < 0x100u + base[ch] + len[ch])
		{
			*r = o - 0x100 - base[ch];
			return &s->itu[ch];
		}
	return NULL;
}

static uint16_t word_r(sh7021 *s, uint32_t o)
{
	uint32_t r;
	itu_ch *t;
	if ((t = itu_reg(s, o, &r)))
	{
		itu_sync(s);
		switch (r)
		{
		case 0x04: return t->tcnt;
		case 0x06: return t->gra;
		case 0x08: return t->grb;
		case 0x0a: return t->bra;
		case 0x0c: return t->brb;
		}
		return 0;
	}
	if (o >= 0x140 && o < 0x180)
	{
		if (o == 0x148 || o == 0x149) return s->dmaor;
		dma_ch *d = &s->dma[(o - 0x140) >> 4];
		switch (o & 0xe)
		{
		case 0x0: return d->sar >> 16;
		case 0x2: return d->sar & 0xffff;
		case 0x4: return d->dar >> 16;
		case 0x6: return d->dar & 0xffff;
		case 0xa: return d->tcr;
		case 0xe: return d->chcr;
		}
		return 0;
	}
	if (o >= 0x184 && o < 0x18e) return s->ipr[(o - 0x184) >> 1];
	if (o == 0x18e) return s->icr;
	if (o >= 0x1a0 && o < 0x1b4) return s->bsc[(o - 0x1a0) >> 1];
	switch (o)
	{
	case 0x1c0: return (s->padr & s->paior) | (s->pa_in & ~s->paior);
	case 0x1c2: return (s->pbdr & s->pbior) | (s->pb_in & ~s->pbior);
	case 0x1c4: return s->paior;
	case 0x1c6: return s->pbior;
	case 0x1c8: return s->pacr1;
	case 0x1ca: return s->pacr2;
	case 0x1cc: return s->pbcr1;
	case 0x1ce: return s->pbcr2;
	case 0x1ee: return s->cascr;
	}
	return 0;
}

static void word_w(sh7021 *s, uint32_t o, uint16_t v)
{
	uint32_t r;
	itu_ch *t;
	if ((t = itu_reg(s, o, &r)))
	{
		itu_sync(s);
		switch (r)
		{
		case 0x04: t->tcnt = v; break;
		case 0x06: t->gra = v; break;
		case 0x08: t->grb = v; break;
		case 0x0a: t->bra = v; break;
		case 0x0c: t->brb = v; break;
		}
		return;
	}
	if (o >= 0x140 && o < 0x180)
	{
		if (o == 0x148)
		{
			/* AE and NMIF are cleared by writing 0 */
			s->dmaor = (v & 0x0301) | (s->dmaor & v & 6);
			dma_check(s);
			return;
		}
		dma_ch *d = &s->dma[(o - 0x140) >> 4];
		switch (o & 0xe)
		{
		case 0x0: d->sar = (d->sar & 0xffff) | ((uint32_t)v << 16); break;
		case 0x2: d->sar = (d->sar & 0xffff0000u) | v; break;
		case 0x4: d->dar = (d->dar & 0xffff) | ((uint32_t)v << 16); break;
		case 0x6: d->dar = (d->dar & 0xffff0000u) | v; break;
		case 0xa: d->tcr = v; break;
		case 0xe:
			/* TE is cleared by writing 0 */
			d->chcr = (v & ~2) | (d->chcr & v & 2);
			if (((v >> 8) & 15) != 0xc && (v & 1))
				unknown(s, "DMA request source", o, v);
			dma_check(s);
			break;
		}
		update_irq(s);
		return;
	}
	if (o >= 0x184 && o < 0x18e) { s->ipr[(o - 0x184) >> 1] = v; update_irq(s); return; }
	if (o == 0x18e) { s->icr = (s->icr & 0x8000) | (v & 0x01ff); update_irq(s); return; }
	if (o >= 0x1a0 && o < 0x1b4) { s->bsc[(o - 0x1a0) >> 1] = v; return; }
	switch (o)
	{
	case 0x1c0: s->padr = v; return;
	case 0x1c2: s->pbdr = v; return;
	case 0x1c4: s->paior = v; return;
	case 0x1c6: s->pbior = v; return;
	case 0x1c8: s->pacr1 = v; return;
	case 0x1ca: s->pacr2 = v; return;
	case 0x1cc: s->pbcr1 = v; return;
	case 0x1ce: s->pbcr2 = v; return;
	case 0x1ee: s->cascr = v; return;
	}
	unknown(s, "write16", o, v);
}

static uint8_t byte_r(sh7021 *s, uint32_t o)
{
	uint32_t r;
	itu_ch *t;
	if (o >= 0x0c0 && o < 0x0d0 && (o & 7) < 6)
	{
		sci_sync(s);
		sci_ch *c = &s->sci[(o >> 3) & 1];
		switch (o & 7)
		{
		case 0: return c->smr;
		case 1: return c->brr;
		case 2: return c->scr;
		case 3: return c->tdr;
		case 4: return c->ssr;
		default: return c->rdr;
		}
	}
	if ((t = itu_reg(s, o, &r)))
	{
		itu_sync(s);
		switch (r)
		{
		case 0: return t->tcr | 0x80;
		case 1: return t->tior | 0x88;
		case 2: return t->tier | 0xf8;
		case 3: return t->tsr | 0xf8;
		}
		return 0;
	}
	switch (o)
	{
	case 0x100: return s->tstr | 0xe0;
	case 0x101: return s->tsnc | 0xe0;
	case 0x102: return s->tmdr | 0x80;
	case 0x103: return s->tfcr | 0xc0;
	case 0x131: return s->tocr | 0xfc;
	case 0x1b8: return s->wdt_tcsr | 0x18;
	case 0x1b9: return s->wdt_tcnt;
	case 0x1bb: return s->wdt_rstcsr | 0x1f;
	case 0x1bc: return s->sbycr | 0x7f;
	}
	unknown(s, "read8", o, 0);
	return 0xff;
}

static void byte_w(sh7021 *s, uint32_t o, uint8_t v)
{
	uint32_t r;
	itu_ch *t;
	if (o >= 0x0c0 && o < 0x0d0 && (o & 7) < 6)
	{
		const int ch = (o >> 3) & 1;
		sci_sync(s);
		sci_ch *c = &s->sci[ch];
		switch (o & 7)
		{
		case 0: c->smr = v; break;
		case 1: c->brr = v; break;
		case 2:
			c->scr = v;
			if (!(v & 0x20))
			{
				c->ssr |= 0x84;                 /* transmitter off: TDRE and TEND set */
				c->tx_busy = 0;
			}
			break;
		case 3: c->tdr = v; break;
		case 4:
			/* status flags are cleared by writing 0; MPBT is written */
			c->ssr = (c->ssr & (v | 0x06)) & ~0x01;
			c->ssr |= v & 0x01;
			break;
		}
		sci_start(s, ch);
		update_irq(s);
		return;
	}
	if ((t = itu_reg(s, o, &r)))
	{
		itu_sync(s);
		switch (r)
		{
		case 0: t->tcr = v & 0x7f; break;
		case 1: t->tior = v & 0x77; break;
		case 2: t->tier = v & 7; break;
		case 3: t->tsr &= v | 0xf8; break;     /* flags cleared by writing 0 */
		}
		update_irq(s);
		return;
	}
	switch (o)
	{
	case 0x100: itu_sync(s); s->tstr = v & 0x1f; return;
	case 0x101: s->tsnc = v & 0x1f; return;
	case 0x102: s->tmdr = v & 0x7f; return;
	case 0x103: s->tfcr = v & 0x3f; return;
	case 0x131: s->tocr = v & 3; return;
	case 0x1bc: s->sbycr = v & 0x80; return;
	}
	unknown(s, "write8", o, v);
}

uint8_t sh7021_r8(sh7021 *s, uint32_t a)
{
	const uint32_t o = a & 0x1ff;
	if (is_word(o))
	{
		const uint16_t w = word_r(s, o & ~1u);
		return (o & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
	}
	return byte_r(s, o);
}

uint16_t sh7021_r16(sh7021 *s, uint32_t a)
{
	const uint32_t o = a & 0x1fe;
	if (is_word(o))
		return word_r(s, o);
	return (uint16_t)((byte_r(s, o) << 8) | byte_r(s, o + 1));
}

void sh7021_w8(sh7021 *s, uint32_t a, uint8_t v)
{
	const uint32_t o = a & 0x1ff;
	if (is_word(o))
	{
		const uint16_t w = word_r(s, o & ~1u);
		word_w(s, o & ~1u, (o & 1) ? (uint16_t)((w & 0xff00) | v) : (uint16_t)((w & 0xff) | (v << 8)));
		return;
	}
	byte_w(s, o, v);
}

void sh7021_w16(sh7021 *s, uint32_t a, uint16_t v)
{
	const uint32_t o = a & 0x1fe;
	if (is_word(o))
	{
		word_w(s, o, v);
		return;
	}
	if (o == 0x1b8)
	{
		/* watchdog: the upper byte is a key, 0xa5 for TCSR and 0x5a for TCNT */
		if ((v >> 8) == 0xa5) s->wdt_tcsr = (uint8_t)v;
		else if ((v >> 8) == 0x5a) s->wdt_tcnt = (uint8_t)v;
		return;
	}
	if (o == 0x1ba)
	{
		if ((v >> 8) == 0x5a) s->wdt_rstcsr = (uint8_t)v;
		return;
	}
	byte_w(s, o, (uint8_t)(v >> 8));
	byte_w(s, o + 1, (uint8_t)v);
}

void sh7021_reset(sh7021 *s)
{
	sh1 *cpu = s->cpu;
	void *ctx = s->ctx;
	void (*tx)(void *, int, uint8_t) = s->sci_tx;
	uint8_t (*r8)(void *, uint32_t) = s->bus_r8;
	uint16_t (*r16)(void *, uint32_t) = s->bus_r16;
	void (*w8)(void *, uint32_t, uint8_t) = s->bus_w8;
	void (*w16)(void *, uint32_t, uint16_t) = s->bus_w16;
	int (*cost)(void *, uint32_t) = s->bus_cost;
	const int log = s->log_unknown;
	memset(s, 0, sizeof(*s));
	s->cpu = cpu;
	s->ctx = ctx;
	s->sci_tx = tx;
	s->bus_r8 = r8;
	s->bus_r16 = r16;
	s->bus_w8 = w8;
	s->bus_w16 = w16;
	s->bus_cost = cost;
	s->log_unknown = log;
	for (int ch = 0; ch < 5; ch++)
		s->itu[ch].gra = s->itu[ch].grb = s->itu[ch].bra = s->itu[ch].brb = 0xffff;
	for (int ch = 0; ch < 2; ch++)
	{
		s->sci[ch].brr = 0xff;
		s->sci[ch].tdr = 0xff;
		s->sci[ch].ssr = 0x84;
	}
	s->icr = 0x8000;                            /* NMI pin high */
	s->pa_in = s->pb_in = 0xffff;
	s->sbycr = 0x1f;
	update_irq(s);
}
