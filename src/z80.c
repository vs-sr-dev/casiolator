/* Casiolator - Zilog Z80 interpreter (see z80.h)
 *
 * Decoding follows the x/y/z/p/q split of the opcode byte. With a DD/FD prefix, HL becomes
 * IX/IY, H and L become their halves, and (HL) becomes (IX+d); an instruction that uses
 * (IX+d) keeps the plain H and L for its other operand. */
#include "z80.h"

#define FC 0x01
#define FN 0x02
#define FP 0x04
#define FX 0x08
#define FH 0x10
#define FY 0x20
#define FZ 0x40
#define FS 0x80

#define A  z->af.b.h
#define F  z->af.b.l
#define B  z->bc.b.h
#define C  z->bc.b.l
#define D  z->de.b.h
#define E  z->de.b.l
#define H  z->hl.b.h
#define L  z->hl.b.l
#define BC z->bc.w
#define DE z->de.w
#define HL z->hl.w
#define SP z->sp.w
#define PC z->pc.w
#define WZ z->wz.w

static uint8_t sz53[256], sz53p[256];

static const uint8_t cyc_main[256] = {
	 4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,
	 8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
	 7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,
	 7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
	 5,10,10,10,10,11, 7,11, 5,10,10, 0,10,17, 7,11,
	 5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 0, 7,11,
	 5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 0, 7,11,
	 5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 0, 7,11,
};

void z80_init(z80 *z)
{
	for (int i = 0; i < 256; i++)
	{
		int p = 1;
		for (int b = 0; b < 8; b++)
			p ^= (i >> b) & 1;
		sz53[i] = (i & (FS | FX | FY)) | (i ? 0 : FZ);
		sz53p[i] = sz53[i] | (p ? FP : 0);
	}
	z->irq_vector = 0xff;
	z80_reset(z);
}

void z80_reset(z80 *z)
{
	PC = 0;
	z->i = z->r = 0;
	z->iff1 = z->iff2 = 0;
	z->im = 0;
	z->halted = 0;
	z->ei_delay = 0;
	z->nmi = 0;
	z->af.w = SP = 0xffff;
}

/* ---- memory ---- */

static inline uint8_t rd(z80 *z, uint16_t a) { return z->read(z->ctx, a); }
static inline void wr(z80 *z, uint16_t a, uint8_t v) { z->write(z->ctx, a, v); }
static inline uint16_t rd16(z80 *z, uint16_t a) { return rd(z, a) | (rd(z, (uint16_t)(a + 1)) << 8); }
static inline void wr16(z80 *z, uint16_t a, uint16_t v) { wr(z, a, v & 0xff); wr(z, (uint16_t)(a + 1), v >> 8); }
static inline uint8_t fetch(z80 *z) { return rd(z, PC++); }
static inline uint16_t fetch16(z80 *z) { uint16_t v = rd16(z, PC); PC += 2; return v; }
static inline void inc_r(z80 *z) { z->r = (z->r & 0x80) | ((z->r + 1) & 0x7f); }
static inline void push(z80 *z, uint16_t v) { SP -= 2; wr16(z, SP, v); }
static inline uint16_t pop(z80 *z) { uint16_t v = rd16(z, SP); SP += 2; return v; }

/* ---- ALU ---- */

static void add8(z80 *z, uint8_t v, int carry)
{
	unsigned r = A + v + carry;
	F = sz53[r & 0xff] | ((r >> 8) & FC) | ((A ^ v ^ r) & FH) | ((((A ^ ~v) & (A ^ r)) & 0x80) >> 5);
	A = (uint8_t)r;
}

static uint8_t sub8(z80 *z, uint8_t v, int carry)
{
	unsigned r = A - v - carry;
	F = sz53[r & 0xff] | ((r >> 8) & FC) | FN | ((A ^ v ^ r) & FH) | ((((A ^ v) & (A ^ r)) & 0x80) >> 5);
	return (uint8_t)r;
}

static void alu(z80 *z, int op, uint8_t v)
{
	switch (op)
	{
	case 0: add8(z, v, 0); break;
	case 1: add8(z, v, F & FC); break;
	case 2: A = sub8(z, v, 0); break;
	case 3: A = sub8(z, v, F & FC); break;
	case 4: A &= v; F = sz53p[A] | FH; break;
	case 5: A ^= v; F = sz53p[A]; break;
	case 6: A |= v; F = sz53p[A]; break;
	case 7: sub8(z, v, 0); F = (F & ~(FX | FY)) | (v & (FX | FY)); break;
	}
}

static uint8_t inc8(z80 *z, uint8_t v)
{
	uint8_t r = v + 1;
	F = (F & FC) | sz53[r] | ((r & 0x0f) ? 0 : FH) | (r == 0x80 ? FP : 0);
	return r;
}

static uint8_t dec8(z80 *z, uint8_t v)
{
	uint8_t r = v - 1;
	F = (F & FC) | FN | sz53[r] | ((v & 0x0f) ? 0 : FH) | (r == 0x7f ? FP : 0);
	return r;
}

static uint16_t add16(z80 *z, uint16_t a, uint16_t v)
{
	unsigned r = a + v;
	WZ = a + 1;
	F = (F & (FS | FZ | FP)) | ((r >> 16) & FC) | (((a ^ v ^ r) >> 8) & FH) | ((r >> 8) & (FX | FY));
	return (uint16_t)r;
}

static void adc16(z80 *z, uint16_t v)
{
	unsigned a = HL, r = a + v + (F & FC);
	WZ = a + 1;
	F = ((r >> 8) & (FS | FX | FY)) | ((r & 0xffff) ? 0 : FZ) | ((r >> 16) & FC) |
	    (((a ^ v ^ r) >> 8) & FH) | ((~(a ^ v) & (a ^ r) & 0x8000) >> 13);
	HL = (uint16_t)r;
}

static void sbc16(z80 *z, uint16_t v)
{
	unsigned a = HL, r = a - v - (F & FC);
	WZ = a + 1;
	F = FN | ((r >> 8) & (FS | FX | FY)) | ((r & 0xffff) ? 0 : FZ) | ((r >> 16) & FC) |
	    (((a ^ v ^ r) >> 8) & FH) | (((a ^ v) & (a ^ r) & 0x8000) >> 13);
	HL = (uint16_t)r;
}

/* CB-page rotates and shifts: RLC RRC RL RR SLA SRA SLL SRL */
static uint8_t rot(z80 *z, int op, uint8_t v)
{
	uint8_t r, c;
	switch (op)
	{
	case 0: c = v >> 7; r = (v << 1) | c; break;
	case 1: c = v & 1; r = (v >> 1) | (c << 7); break;
	case 2: c = v >> 7; r = (v << 1) | (F & FC); break;
	case 3: c = v & 1; r = (v >> 1) | ((F & FC) << 7); break;
	case 4: c = v >> 7; r = v << 1; break;
	case 5: c = v & 1; r = (v >> 1) | (v & 0x80); break;
	case 6: c = v >> 7; r = (v << 1) | 1; break;
	default: c = v & 1; r = v >> 1; break;
	}
	F = sz53p[r] | c;
	return r;
}

/* BIT n: X and Y come from the operand (register) or from MEMPTR's high byte (memory) */
static void bit(z80 *z, int n, uint8_t v, uint8_t xy)
{
	uint8_t t = v & (1 << n);
	F = (F & FC) | FH | (t ? 0 : (FZ | FP)) | (t & FS) | (xy & (FX | FY));
}

static int cond(z80 *z, int c)
{
	switch (c)
	{
	case 0: return !(F & FZ);
	case 1: return F & FZ;
	case 2: return !(F & FC);
	case 3: return F & FC;
	case 4: return !(F & FP);
	case 5: return F & FP;
	case 6: return !(F & FS);
	default: return F & FS;
	}
}

/* ---- prefixes ---- */

/* the 16-bit register a DD/FD prefix substitutes for HL (0 none, 1 IX, 2 IY) */
static inline z80_pair *xhl(z80 *z, int idx) { return idx == 0 ? &z->hl : idx == 1 ? &z->ix : &z->iy; }

/* 8-bit register by its 3-bit code (not 6); H and L follow the prefix */
static inline uint8_t *reg8(z80 *z, int r, int idx)
{
	switch (r)
	{
	case 0: return &B;
	case 1: return &C;
	case 2: return &D;
	case 3: return &E;
	case 4: return &xhl(z, idx)->b.h;
	case 5: return &xhl(z, idx)->b.l;
	default: return &A;
	}
}

/* rp table (BC DE HL SP) and rp2 (BC DE HL AF) */
static inline uint16_t *rp(z80 *z, int p, int idx)
{
	switch (p)
	{
	case 0: return &BC;
	case 1: return &DE;
	case 2: return &xhl(z, idx)->w;
	default: return &SP;
	}
}

static inline uint16_t *rp2(z80 *z, int p, int idx)
{
	return p == 3 ? &z->af.w : rp(z, p, idx);
}

/* address of the (HL) operand: HL, or IX/IY plus the displacement byte */
static uint16_t mem_addr(z80 *z, int idx)
{
	if (!idx)
		return HL;
	uint16_t a = xhl(z, idx)->w + (int8_t)fetch(z);
	WZ = a;
	return a;
}

/* ---- CB page ---- */

static int exec_cb(z80 *z, int idx)
{
	uint16_t addr = 0;
	uint8_t op, v;
	int mem;

	if (idx)
	{
		/* DD CB d op: the displacement comes before the opcode, which is not an M1 */
		addr = mem_addr(z, idx);
		op = fetch(z);
		mem = 1;
	}
	else
	{
		op = fetch(z);
		inc_r(z);
		mem = (op & 7) == 6;
		if (mem)
			addr = HL;
	}

	int x = op >> 6, y = (op >> 3) & 7, r = op & 7;
	v = mem ? rd(z, addr) : *reg8(z, r, 0);

	if (x == 1)
	{
		bit(z, y, v, mem ? z->wz.b.h : v);
		return idx ? 20 : mem ? 12 : 8;
	}
	if (x == 0)
		v = rot(z, y, v);
	else if (x == 2)
		v &= ~(1 << y);
	else
		v |= 1 << y;
	if (mem)
	{
		wr(z, addr, v);
		/* DD CB with a register field other than 6 also copies the result there */
		if (idx && r != 6)
			*reg8(z, r, 0) = v;
	}
	else
		*reg8(z, r, 0) = v;
	return idx ? 23 : mem ? 15 : 8;
}

/* ---- ED page ---- */

static int exec_ed(z80 *z)
{
	uint8_t op = fetch(z);
	inc_r(z);
	int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;

	if (x == 1)
	{
		switch (zz)
		{
		case 0: { /* IN r,(C) */
			uint8_t v = z->in(z->ctx, BC);
			WZ = BC + 1;
			if (y != 6)
				*reg8(z, y, 0) = v;
			F = (F & FC) | sz53p[v];
			return 12;
		}
		case 1: /* OUT (C),r */
			z->out(z->ctx, BC, y == 6 ? 0 : *reg8(z, y, 0));
			WZ = BC + 1;
			return 12;
		case 2:
			if (q)
				adc16(z, *rp(z, p, 0));
			else
				sbc16(z, *rp(z, p, 0));
			return 15;
		case 3: {
			uint16_t a = fetch16(z);
			if (q)
				*rp(z, p, 0) = rd16(z, a);
			else
				wr16(z, a, *rp(z, p, 0));
			WZ = a + 1;
			return 20;
		}
		case 4: { /* NEG */
			uint8_t v = A;
			A = 0;
			A = sub8(z, v, 0);
			return 8;
		}
		case 5: /* RETN, RETI */
			z->iff1 = z->iff2;
			PC = WZ = pop(z);
			return 14;
		case 6:
			z->im = (y & 3) == 0 ? 0 : (y & 3) == 1 ? 0 : (y & 3) == 2 ? 1 : 2;
			return 8;
		default:
			switch (y)
			{
			case 0: z->i = A; return 9;
			case 1: z->r = A; return 9;
			case 2:
				A = z->i;
				F = (F & FC) | sz53[A] | (z->iff2 ? FP : 0);
				return 9;
			case 3:
				A = z->r;
				F = (F & FC) | sz53[A] | (z->iff2 ? FP : 0);
				return 9;
			case 4: { /* RRD */
				uint8_t v = rd(z, HL);
				wr(z, HL, (A << 4) | (v >> 4));
				A = (A & 0xf0) | (v & 0x0f);
				F = (F & FC) | sz53p[A];
				WZ = HL + 1;
				return 18;
			}
			case 5: { /* RLD */
				uint8_t v = rd(z, HL);
				wr(z, HL, (v << 4) | (A & 0x0f));
				A = (A & 0xf0) | (v >> 4);
				F = (F & FC) | sz53p[A];
				WZ = HL + 1;
				return 18;
			}
			default:
				return 8;
			}
		}
	}

	if (x == 2 && y >= 4 && zz <= 3)
	{
		int dec = y & 1, rep = y & 2;
		int step = dec ? -1 : 1;
		switch (zz)
		{
		case 0: { /* LDI LDD LDIR LDDR */
			uint8_t v = rd(z, HL);
			wr(z, DE, v);
			HL += step;
			DE += step;
			BC--;
			uint8_t n = v + A;
			F = (F & (FS | FZ | FC)) | (BC ? FP : 0) | (n & FX) | ((n << 4) & FY);
			if (rep && BC)
			{
				PC -= 2;
				WZ = PC + 1;
				return 21;
			}
			return 16;
		}
		case 1: { /* CPI CPD CPIR CPDR */
			uint8_t v = rd(z, HL);
			uint8_t r = A - v;
			HL += step;
			BC--;
			WZ += step;
			F = (F & FC) | FN | (sz53[r] & ~(FX | FY)) | ((A ^ v ^ r) & FH) | (BC ? FP : 0);
			uint8_t n = r - ((F & FH) ? 1 : 0);
			F |= (n & FX) | ((n << 4) & FY);
			if (rep && BC && r)
			{
				PC -= 2;
				WZ = PC + 1;
				return 21;
			}
			return 16;
		}
		case 2: { /* INI IND INIR INDR */
			uint8_t v = z->in(z->ctx, BC);
			WZ = BC + step;
			wr(z, HL, v);
			B--;
			HL += step;
			unsigned k = v + (uint8_t)(C + step);
			F = sz53[B] | ((v & 0x80) ? FN : 0) | (k > 0xff ? (FH | FC) : 0) | (sz53p[(k & 7) ^ B] & FP);
			if (rep && B)
			{
				PC -= 2;
				return 21;
			}
			return 16;
		}
		default: { /* OUTI OUTD OTIR OTDR */
			uint8_t v = rd(z, HL);
			B--;
			z->out(z->ctx, BC, v);
			WZ = BC + step;
			HL += step;
			unsigned k = v + L;
			F = sz53[B] | ((v & 0x80) ? FN : 0) | (k > 0xff ? (FH | FC) : 0) | (sz53p[(k & 7) ^ B] & FP);
			if (rep && B)
			{
				PC -= 2;
				return 21;
			}
			return 16;
		}
		}
	}
	return 8; /* undefined: a two-byte NOP */
}

/* ---- unprefixed page (also with DD/FD) ---- */

static int exec_main(z80 *z, uint8_t op, int idx)
{
	int x = op >> 6, y = (op >> 3) & 7, zz = op & 7, p = y >> 1, q = y & 1;
	int cyc = cyc_main[op] + (idx ? 4 : 0);
	z80_pair *hx = xhl(z, idx);

	switch (x)
	{
	case 0:
		switch (zz)
		{
		case 0:
			switch (y)
			{
			case 0: break;
			case 1: { z80_pair t = z->af; z->af = z->af2; z->af2 = t; break; }
			case 2: { /* DJNZ */
				int8_t d = fetch(z);
				if (--B)
				{
					PC += d;
					WZ = PC;
					cyc += 5;
				}
				break;
			}
			case 3: {
				int8_t d = fetch(z);
				PC += d;
				WZ = PC;
				break;
			}
			default: {
				int8_t d = fetch(z);
				if (cond(z, y - 4))
				{
					PC += d;
					WZ = PC;
					cyc += 5;
				}
				break;
			}
			}
			break;
		case 1:
			if (q)
				hx->w = add16(z, hx->w, *rp(z, p, idx));
			else
				*rp(z, p, idx) = fetch16(z);
			break;
		case 2:
			switch (y)
			{
			case 0: wr(z, BC, A); z->wz.b.l = BC + 1; z->wz.b.h = A; break;
			case 1: A = rd(z, BC); WZ = BC + 1; break;
			case 2: wr(z, DE, A); z->wz.b.l = DE + 1; z->wz.b.h = A; break;
			case 3: A = rd(z, DE); WZ = DE + 1; break;
			case 4: { uint16_t a = fetch16(z); wr16(z, a, hx->w); WZ = a + 1; break; }
			case 5: { uint16_t a = fetch16(z); hx->w = rd16(z, a); WZ = a + 1; break; }
			case 6: { uint16_t a = fetch16(z); wr(z, a, A); z->wz.b.l = a + 1; z->wz.b.h = A; break; }
			default: { uint16_t a = fetch16(z); A = rd(z, a); WZ = a + 1; break; }
			}
			break;
		case 3:
			if (q)
				(*rp(z, p, idx))--;
			else
				(*rp(z, p, idx))++;
			break;
		case 4:
		case 5:
			if (y == 6)
			{
				uint16_t a = mem_addr(z, idx);
				uint8_t v = rd(z, a);
				wr(z, a, zz == 4 ? inc8(z, v) : dec8(z, v));
				if (idx)
					cyc += 8;
			}
			else
			{
				uint8_t *r = reg8(z, y, idx);
				*r = zz == 4 ? inc8(z, *r) : dec8(z, *r);
			}
			break;
		case 6:
			if (y == 6)
			{
				uint16_t a = mem_addr(z, idx);
				wr(z, a, fetch(z));
				if (idx)
					cyc += 5;
			}
			else
				*reg8(z, y, idx) = fetch(z);
			break;
		default:
			switch (y)
			{
			case 0: A = (A << 1) | (A >> 7); F = (F & (FS | FZ | FP)) | (A & (FX | FY | FC)); break;
			case 1: {
				uint8_t c = A & 1;
				A = (A >> 1) | (c << 7);
				F = (F & (FS | FZ | FP)) | (A & (FX | FY)) | c;
				break;
			}
			case 2: {
				uint8_t c = A >> 7;
				A = (A << 1) | (F & FC);
				F = (F & (FS | FZ | FP)) | (A & (FX | FY)) | c;
				break;
			}
			case 3: {
				uint8_t c = A & 1;
				A = (A >> 1) | (F << 7);
				F = (F & (FS | FZ | FP)) | (A & (FX | FY)) | c;
				break;
			}
			case 4: { /* DAA */
				uint8_t a = A, add = 0, carry = F & FC;
				if ((F & FH) || (a & 0x0f) > 9)
					add = 6;
				if (carry || a > 0x99)
				{
					add |= 0x60;
					carry = FC;
				}
				uint8_t r = (F & FN) ? a - add : a + add;
				F = (F & FN) | carry | sz53p[r] | ((a ^ r) & FH);
				A = r;
				break;
			}
			case 5: A = ~A; F = (F & (FS | FZ | FP | FC)) | FH | FN | (A & (FX | FY)); break;
			case 6: F = (F & (FS | FZ | FP)) | FC | (A & (FX | FY)); break;
			default: F = (F & (FS | FZ | FP)) | ((F & FC) ? FH : FC) | (A & (FX | FY)); break;
			}
			break;
		}
		break;

	case 1:
		if (op == 0x76)
		{
			z->halted = 1;
			break;
		}
		if (y == 6)
		{
			uint16_t a = mem_addr(z, idx);
			wr(z, a, *reg8(z, zz, 0));
			if (idx)
				cyc += 8;
		}
		else if (zz == 6)
		{
			uint16_t a = mem_addr(z, idx);
			*reg8(z, y, 0) = rd(z, a);
			if (idx)
				cyc += 8;
		}
		else
			*reg8(z, y, idx) = *reg8(z, zz, idx);
		break;

	case 2:
		if (zz == 6)
		{
			alu(z, y, rd(z, mem_addr(z, idx)));
			if (idx)
				cyc += 8;
		}
		else
			alu(z, y, *reg8(z, zz, idx));
		break;

	default:
		switch (zz)
		{
		case 0:
			if (cond(z, y))
			{
				PC = WZ = pop(z);
				cyc += 6;
			}
			break;
		case 1:
			if (!q)
				*rp2(z, p, idx) = pop(z);
			else
				switch (p)
				{
				case 0: PC = WZ = pop(z); break;
				case 1: { /* EXX */
					z80_pair t;
					t = z->bc; z->bc = z->bc2; z->bc2 = t;
					t = z->de; z->de = z->de2; z->de2 = t;
					t = z->hl; z->hl = z->hl2; z->hl2 = t;
					break;
				}
				case 2: PC = hx->w; break;
				default: SP = hx->w; break;
				}
			break;
		case 2:
			WZ = fetch16(z);
			if (cond(z, y))
				PC = WZ;
			break;
		case 3:
			switch (y)
			{
			case 0: PC = WZ = fetch16(z); break;
			case 1: break; /* CB: z80_step takes it */
			case 2: { /* OUT (n),A */
				uint8_t n = fetch(z);
				z->out(z->ctx, (A << 8) | n, A);
				z->wz.b.l = n + 1;
				z->wz.b.h = A;
				break;
			}
			case 3: { /* IN A,(n) */
				uint16_t port = (A << 8) | fetch(z);
				A = z->in(z->ctx, port);
				WZ = port + 1;
				break;
			}
			case 4: { /* EX (SP),HL */
				uint16_t v = rd16(z, SP);
				wr16(z, SP, hx->w);
				hx->w = WZ = v;
				break;
			}
			case 5: { uint16_t t = DE; DE = HL; HL = t; break; }
			case 6: z->iff1 = z->iff2 = 0; break;
			default: z->iff1 = z->iff2 = 1; z->ei_delay = 1; break;
			}
			break;
		case 4:
			WZ = fetch16(z);
			if (cond(z, y))
			{
				push(z, PC);
				PC = WZ;
				cyc += 7;
			}
			break;
		case 5:
			if (!q)
				push(z, *rp2(z, p, idx));
			else
			{
				/* p == 0: CALL nn; ED, DD, FD are handled before getting here */
				WZ = fetch16(z);
				push(z, PC);
				PC = WZ;
			}
			break;
		case 6:
			alu(z, y, fetch(z));
			break;
		default:
			push(z, PC);
			PC = WZ = y * 8;
			break;
		}
		break;
	}
	return cyc;
}

static int interrupt(z80 *z)
{
	if (z->nmi)
	{
		z->nmi = 0;
		z->halted = 0;
		z->iff1 = 0;
		inc_r(z);
		push(z, PC);
		PC = WZ = 0x66;
		return 11;
	}
	z->halted = 0;
	z->iff1 = z->iff2 = 0;
	inc_r(z);
	switch (z->im)
	{
	case 2:
		push(z, PC);
		PC = WZ = rd16(z, (z->i << 8) | z->irq_vector);
		return 19;
	case 1:
		push(z, PC);
		PC = WZ = 0x38;
		return 13;
	default:
		/* mode 0: only an RST on the bus is supported (0xff, a pulled-up bus, is RST 38) */
		push(z, PC);
		PC = WZ = z->irq_vector & 0x38;
		return 13;
	}
}

int z80_step(z80 *z)
{
	if (z->nmi || (z->irq && z->iff1 && !z->ei_delay))
		return interrupt(z);
	z->ei_delay = 0;
	if (z->halted)
	{
		inc_r(z);
		return 4;
	}

	uint8_t op = fetch(z);
	inc_r(z);
	int idx = 0, extra = 0;
	while (op == 0xdd || op == 0xfd)
	{
		idx = op == 0xdd ? 1 : 2;
		op = fetch(z);
		inc_r(z);
		extra += 4;
	}
	if (op == 0xed)
		return extra + exec_ed(z);
	if (op == 0xcb && idx)
		return extra - 4 + exec_cb(z, idx);
	if (op == 0xcb)
		return exec_cb(z, 0);
	/* exec_main adds 4 for one prefix; more than one costs 4 each */
	return (extra ? extra - 4 : 0) + exec_main(z, op, idx);
}
