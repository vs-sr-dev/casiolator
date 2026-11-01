/* Casiolator - Hitachi SH-1 CPU core (see sh1.h)
 *
 * c->pc is the address of the instruction being executed; PC-relative operands use pc + 4
 * as the manual defines. A delayed branch runs its slot instruction (which sees its own
 * address + 4 as PC) and then jumps; nothing can interrupt between the two. */
#include <stdio.h>
#include "sh1.h"

#define RN c->r[(op >> 8) & 15]
#define RM c->r[(op >> 4) & 15]
#define R0 c->r[0]
#define R15 c->r[15]

static inline int t_bit(sh1 *c) { return c->sr & SR_T; }
static inline void set_t(sh1 *c, int v) { c->sr = (c->sr & ~SR_T) | (v ? SR_T : 0); }
static inline int q_bit(sh1 *c) { return (c->sr & SR_Q) != 0; }
static inline int m_bit(sh1 *c) { return (c->sr & SR_M) != 0; }
static inline void set_q(sh1 *c, int v) { c->sr = (c->sr & ~SR_Q) | (v ? SR_Q : 0); }
static inline void set_m(sh1 *c, int v) { c->sr = (c->sr & ~SR_M) | (v ? SR_M : 0); }

/* the SH-1 MACH register holds 10 bits; bit 9 reads as the sign of the upper 22 */
static inline uint32_t mach_sx(uint32_t v) { return (v & 0x200) ? (v | 0xfffffc00u) : (v & 0x3ff); }

static inline uint8_t rb(sh1 *c, uint32_t a) { return c->read8(c->ctx, a); }
static inline uint16_t rw(sh1 *c, uint32_t a) { return c->read16(c->ctx, a & ~1u); }
static inline uint32_t rl(sh1 *c, uint32_t a) { return c->read32(c->ctx, a & ~3u); }
static inline void wb(sh1 *c, uint32_t a, uint8_t v) { c->write8(c->ctx, a, v); }
static inline void ww(sh1 *c, uint32_t a, uint16_t v) { c->write16(c->ctx, a & ~1u, v); }
static inline void wl(sh1 *c, uint32_t a, uint32_t v) { c->write32(c->ctx, a & ~3u, v); }

void sh1_reset(sh1 *c)
{
	c->sr = SR_I;                       /* interrupts masked */
	c->vbr = 0;
	c->sleeping = 0;
	c->pc = rl(c, 0);
	R15 = rl(c, 4);
}

/* push SR and the return address, jump through the vector table */
static int exception(sh1 *c, int vec, uint32_t ret)
{
	R15 -= 4;
	wl(c, R15, c->sr);
	R15 -= 4;
	wl(c, R15, ret);
	c->pc = rl(c, c->vbr + vec * 4);
	return 8;
}

static int is_branch(uint16_t op)
{
	switch (op >> 12)
	{
	case 0x0:
		return op == 0x000b || op == 0x002b;                     /* RTS, RTE */
	case 0x4:
		return (op & 0xff) == 0x0b || (op & 0xff) == 0x2b;       /* JSR, JMP */
	case 0x8:
		return ((op >> 8) & 0xf) == 0x9 || ((op >> 8) & 0xf) == 0xb;  /* BT, BF */
	case 0xa: case 0xb:
		return 1;                                                /* BRA, BSR */
	case 0xc:
		return ((op >> 8) & 0xf) == 0x3;                         /* TRAPA */
	default:
		return 0;
	}
}

static int exec(sh1 *c, uint16_t op, int slot);

/* the instruction after a delayed branch at pc; then the caller sets the target */
static int delay_slot(sh1 *c, uint32_t pc)
{
	uint32_t slot = pc + 2;
	uint16_t op = c->fetch(c->ctx, slot);
	c->pc = slot;
	if (is_branch(op))
		return exception(c, VEC_SLOT_ILLEGAL, pc);
	return exec(c, op, 1);
}

static void div1(sh1 *c, uint32_t *rn, uint32_t rm)
{
	int old_q = q_bit(c), q = (*rn >> 31) & 1, tmp1;
	uint32_t tmp0;
	*rn = (*rn << 1) | t_bit(c);
	tmp0 = *rn;
	if (old_q == m_bit(c))
	{
		*rn -= rm;
		tmp1 = *rn > tmp0;
	}
	else
	{
		*rn += rm;
		tmp1 = *rn < tmp0;
	}
	/* the manual's four cases of old Q and M all come down to this */
	q = q ^ tmp1 ^ m_bit(c);
	set_q(c, q);
	set_t(c, q == m_bit(c));
}

static void mac_w(sh1 *c, int16_t a, int16_t b)
{
	int32_t prod = (int32_t)a * b;
	if (c->sr & SR_S)
	{
		int64_t sum = (int64_t)(int32_t)c->macl + prod;
		if (sum > 0x7fffffffLL)
		{
			c->macl = 0x7fffffff;
			c->mach |= 1;
		}
		else if (sum < -0x80000000LL)
		{
			c->macl = 0x80000000u;
			c->mach |= 1;
		}
		else
			c->macl = (uint32_t)sum;
		return;
	}
	int64_t acc = ((int64_t)(int32_t)mach_sx(c->mach) << 32) | c->macl;
	acc += prod;
	c->macl = (uint32_t)acc;
	c->mach = mach_sx((uint32_t)(acc >> 32) & 0x3ff);
}

/* one instruction; on return c->pc is the next instruction to run */
static int exec(sh1 *c, uint16_t op, int slot)
{
	const uint32_t pc = c->pc;
	const int n = (op >> 8) & 15, m = (op >> 4) & 15;
	int cyc = 1;
	c->pc = pc + 2;

	switch (op >> 12)
	{
	case 0x0:
		switch (op & 0xf)
		{
		case 0x2:
			switch (op & 0xf0)
			{
			case 0x00: RN = c->sr; return 1;
			case 0x10: RN = c->gbr; return 1;
			case 0x20: RN = c->vbr; return 1;
			}
			break;
		case 0x4: wb(c, R0 + RN, (uint8_t)RM); return 1;
		case 0x5: ww(c, R0 + RN, (uint16_t)RM); return 1;
		case 0x6: wl(c, R0 + RN, RM); return 1;
		case 0x8:
			switch (op)
			{
			case 0x0008: set_t(c, 0); return 1;
			case 0x0018: set_t(c, 1); return 1;
			case 0x0028: c->mach = c->macl = 0; return 1;
			}
			break;
		case 0x9:
			switch (op & 0xf0)
			{
			case 0x00: if (op == 0x0009) return 1; break;                     /* NOP */
			case 0x10: if (op == 0x0019) { c->sr &= ~(SR_M | SR_Q | SR_T); return 1; } break;
			case 0x20: RN = t_bit(c); return 1;                               /* MOVT */
			}
			break;
		case 0xa:
			switch (op & 0xf0)
			{
			case 0x00: RN = mach_sx(c->mach); return 1;
			case 0x10: RN = c->macl; return 1;
			case 0x20: RN = c->pr; return 1;
			}
			break;
		case 0xb:
			if (slot)
				break;
			switch (op)
			{
			case 0x000b: { /* RTS */
				uint32_t target = c->pr;
				cyc = 2 + delay_slot(c, pc);
				c->pc = target;
				return cyc;
			}
			case 0x001b: /* SLEEP */
				c->sleeping = 1;
				return 3;
			case 0x002b: { /* RTE */
				uint32_t target = rl(c, R15);
				R15 += 4;
				c->sr = rl(c, R15) & SR_MASK;
				R15 += 4;
				cyc = 4 + delay_slot(c, pc);
				c->pc = target;
				return cyc;
			}
			}
			break;
		case 0xc: RN = (int8_t)rb(c, R0 + RM); return 1;
		case 0xd: RN = (int16_t)rw(c, R0 + RM); return 1;
		case 0xe: RN = rl(c, R0 + RM); return 1;
		}
		break;

	case 0x1: wl(c, RN + (op & 15) * 4, RM); return 1;

	case 0x2:
		switch (op & 0xf)
		{
		case 0x0: wb(c, RN, (uint8_t)RM); return 1;
		case 0x1: ww(c, RN, (uint16_t)RM); return 1;
		case 0x2: wl(c, RN, RM); return 1;
		case 0x4: { uint32_t v = RM; RN -= 1; wb(c, RN, (uint8_t)v); return 1; }
		case 0x5: { uint32_t v = RM; RN -= 2; ww(c, RN, (uint16_t)v); return 1; }
		case 0x6: { uint32_t v = RM; RN -= 4; wl(c, RN, v); return 1; }
		case 0x7: /* DIV0S */
			set_q(c, RN >> 31);
			set_m(c, RM >> 31);
			set_t(c, (RN ^ RM) >> 31);
			return 1;
		case 0x8: set_t(c, (RN & RM) == 0); return 1;
		case 0x9: RN &= RM; return 1;
		case 0xa: RN ^= RM; return 1;
		case 0xb: RN |= RM; return 1;
		case 0xc: { /* CMP/STR */
			uint32_t x = RN ^ RM;
			set_t(c, !(x & 0xff000000) || !(x & 0xff0000) || !(x & 0xff00) || !(x & 0xff));
			return 1;
		}
		case 0xd: RN = (RN >> 16) | (RM << 16); return 1;                        /* XTRCT */
		case 0xe: c->macl = (uint32_t)(uint16_t)RN * (uint16_t)RM; return 2;    /* MULU.W */
		case 0xf: c->macl = (uint32_t)((int32_t)(int16_t)RN * (int16_t)RM); return 2;   /* MULS.W */
		}
		break;

	case 0x3:
		switch (op & 0xf)
		{
		case 0x0: set_t(c, RN == RM); return 1;
		case 0x2: set_t(c, RN >= RM); return 1;
		case 0x3: set_t(c, (int32_t)RN >= (int32_t)RM); return 1;
		case 0x4: div1(c, &c->r[n], RM); return 1;
		case 0x6: set_t(c, RN > RM); return 1;
		case 0x7: set_t(c, (int32_t)RN > (int32_t)RM); return 1;
		case 0x8: RN -= RM; return 1;
		case 0xa: { /* SUBC */
			uint32_t a = RN, b = RM, r = a - b - t_bit(c);
			set_t(c, a < b || (a == b && t_bit(c)));
			RN = r;
			return 1;
		}
		case 0xb: { /* SUBV */
			uint32_t a = RN, b = RM, r = a - b;
			set_t(c, ((a ^ b) & (a ^ r)) >> 31);
			RN = r;
			return 1;
		}
		case 0xc: RN += RM; return 1;
		case 0xe: { /* ADDC */
			uint64_t r = (uint64_t)RN + RM + t_bit(c);
			RN = (uint32_t)r;
			set_t(c, r >> 32);
			return 1;
		}
		case 0xf: { /* ADDV */
			uint32_t a = RN, b = RM, r = a + b;
			set_t(c, (~(a ^ b) & (a ^ r)) >> 31);
			RN = r;
			return 1;
		}
		}
		break;

	case 0x4:
		if ((op & 0xf) == 0xf)
		{
			/* MAC.W @Rm+,@Rn+ */
			int16_t b = (int16_t)rw(c, RN);
			RN += 2;
			int16_t a = (int16_t)rw(c, RM);
			RM += 2;
			mac_w(c, a, b);
			return 3;
		}
		switch (op & 0xff)
		{
		case 0x00: case 0x20: set_t(c, RN >> 31); RN <<= 1; return 1;                 /* SHLL, SHAL */
		case 0x01: set_t(c, RN & 1); RN >>= 1; return 1;                              /* SHLR */
		case 0x21: set_t(c, RN & 1); RN = (uint32_t)((int32_t)RN >> 1); return 1;     /* SHAR */
		case 0x04: set_t(c, RN >> 31); RN = (RN << 1) | (RN >> 31); return 1;         /* ROTL */
		case 0x05: set_t(c, RN & 1); RN = (RN >> 1) | (RN << 31); return 1;           /* ROTR */
		case 0x24: { int t = RN >> 31; RN = (RN << 1) | t_bit(c); set_t(c, t); return 1; }          /* ROTCL */
		case 0x25: { int t = RN & 1; RN = (RN >> 1) | ((uint32_t)t_bit(c) << 31); set_t(c, t); return 1; } /* ROTCR */
		case 0x08: RN <<= 2; return 1;
		case 0x09: RN >>= 2; return 1;
		case 0x18: RN <<= 8; return 1;
		case 0x19: RN >>= 8; return 1;
		case 0x28: RN <<= 16; return 1;
		case 0x29: RN >>= 16; return 1;
		case 0x11: set_t(c, (int32_t)RN >= 0); return 1;                              /* CMP/PZ */
		case 0x15: set_t(c, (int32_t)RN > 0); return 1;                               /* CMP/PL */
		case 0x02: RN -= 4; wl(c, RN, mach_sx(c->mach)); return 1;
		case 0x12: RN -= 4; wl(c, RN, c->macl); return 1;
		case 0x22: RN -= 4; wl(c, RN, c->pr); return 1;
		case 0x03: RN -= 4; wl(c, RN, c->sr); return 2;
		case 0x13: RN -= 4; wl(c, RN, c->gbr); return 2;
		case 0x23: RN -= 4; wl(c, RN, c->vbr); return 2;
		case 0x06: c->mach = rl(c, RN) & 0x3ff; RN += 4; return 1;
		case 0x16: c->macl = rl(c, RN); RN += 4; return 1;
		case 0x26: c->pr = rl(c, RN); RN += 4; return 1;
		case 0x07: c->sr = rl(c, RN) & SR_MASK; RN += 4; return 3;
		case 0x17: c->gbr = rl(c, RN); RN += 4; return 3;
		case 0x27: c->vbr = rl(c, RN); RN += 4; return 3;
		case 0x0a: c->mach = RN & 0x3ff; return 1;
		case 0x1a: c->macl = RN; return 1;
		case 0x2a: c->pr = RN; return 1;
		case 0x0e: c->sr = RN & SR_MASK; return 1;
		case 0x1e: c->gbr = RN; return 1;
		case 0x2e: c->vbr = RN; return 1;
		case 0x1b: { /* TAS.B */
			uint8_t v = rb(c, RN);
			set_t(c, v == 0);
			wb(c, RN, v | 0x80);
			return 4;
		}
		case 0x0b: /* JSR */
			if (slot)
				break;
			{
				uint32_t target = RN;
				c->pr = pc + 4;
				cyc = 2 + delay_slot(c, pc);
				c->pc = target;
				return cyc;
			}
		case 0x2b: /* JMP */
			if (slot)
				break;
			{
				uint32_t target = RN;
				cyc = 2 + delay_slot(c, pc);
				c->pc = target;
				return cyc;
			}
		}
		break;

	case 0x5: RN = rl(c, RM + (op & 15) * 4); return 1;

	case 0x6:
		switch (op & 0xf)
		{
		case 0x0: RN = (int8_t)rb(c, RM); return 1;
		case 0x1: RN = (int16_t)rw(c, RM); return 1;
		case 0x2: RN = rl(c, RM); return 1;
		case 0x3: RN = RM; return 1;
		case 0x4: { uint32_t v = (int8_t)rb(c, RM); if (n != m) RM += 1; RN = v; return 1; }
		case 0x5: { uint32_t v = (int16_t)rw(c, RM); if (n != m) RM += 2; RN = v; return 1; }
		case 0x6: { uint32_t v = rl(c, RM); if (n != m) RM += 4; RN = v; return 1; }
		case 0x7: RN = ~RM; return 1;
		case 0x8: RN = (RM & 0xffff0000u) | ((RM & 0xff) << 8) | ((RM >> 8) & 0xff); return 1;
		case 0x9: RN = (RM << 16) | (RM >> 16); return 1;
		case 0xa: { /* NEGC */
			uint32_t b = RM, r = 0 - b - t_bit(c);
			set_t(c, b != 0 || t_bit(c));
			RN = r;
			return 1;
		}
		case 0xb: RN = 0 - RM; return 1;
		case 0xc: RN = RM & 0xff; return 1;
		case 0xd: RN = RM & 0xffff; return 1;
		case 0xe: RN = (int8_t)RM; return 1;
		case 0xf: RN = (int16_t)RM; return 1;
		}
		break;

	case 0x7: RN += (int8_t)op; return 1;

	case 0x8:
		switch (n)
		{
		case 0x0: wb(c, RM + (op & 15), (uint8_t)R0); return 1;
		case 0x1: ww(c, RM + (op & 15) * 2, (uint16_t)R0); return 1;
		case 0x4: R0 = (int8_t)rb(c, RM + (op & 15)); return 1;
		case 0x5: R0 = (int16_t)rw(c, RM + (op & 15) * 2); return 1;
		case 0x8: set_t(c, R0 == (uint32_t)(int8_t)op); return 1;
		case 0x9: /* BT */
			if (slot)
				break;
			if (t_bit(c))
			{
				c->pc = pc + 4 + (int8_t)op * 2;
				return 3;
			}
			return 1;
		case 0xb: /* BF */
			if (slot)
				break;
			if (!t_bit(c))
			{
				c->pc = pc + 4 + (int8_t)op * 2;
				return 3;
			}
			return 1;
		}
		break;

	case 0x9: RN = (int16_t)rw(c, pc + 4 + (op & 0xff) * 2); return 1;

	case 0xa: /* BRA */
		if (slot)
			break;
		{
			int32_t d = op & 0xfff;
			if (d & 0x800)
				d -= 0x1000;
			uint32_t target = pc + 4 + d * 2;
			cyc = 2 + delay_slot(c, pc);
			c->pc = target;
			return cyc;
		}

	case 0xb: /* BSR */
		if (slot)
			break;
		{
			int32_t d = op & 0xfff;
			if (d & 0x800)
				d -= 0x1000;
			uint32_t target = pc + 4 + d * 2;
			c->pr = pc + 4;
			cyc = 2 + delay_slot(c, pc);
			c->pc = target;
			return cyc;
		}

	case 0xc:
	{
		const uint32_t d = op & 0xff;
		switch (n)
		{
		case 0x0: wb(c, c->gbr + d, (uint8_t)R0); return 1;
		case 0x1: ww(c, c->gbr + d * 2, (uint16_t)R0); return 1;
		case 0x2: wl(c, c->gbr + d * 4, R0); return 1;
		case 0x3: /* TRAPA */
			if (slot)
				break;
			return exception(c, (int)d, pc + 2);
		case 0x4: R0 = (int8_t)rb(c, c->gbr + d); return 1;
		case 0x5: R0 = (int16_t)rw(c, c->gbr + d * 2); return 1;
		case 0x6: R0 = rl(c, c->gbr + d * 4); return 1;
		case 0x7: R0 = ((pc + 4) & ~3u) + d * 4; return 1;      /* MOVA */
		case 0x8: set_t(c, (R0 & d) == 0); return 1;
		case 0x9: R0 &= d; return 1;
		case 0xa: R0 ^= d; return 1;
		case 0xb: R0 |= d; return 1;
		case 0xc: set_t(c, (rb(c, c->gbr + R0) & d) == 0); return 3;
		case 0xd: { uint32_t a = c->gbr + R0; wb(c, a, rb(c, a) & d); return 3; }
		case 0xe: { uint32_t a = c->gbr + R0; wb(c, a, rb(c, a) ^ d); return 3; }
		case 0xf: { uint32_t a = c->gbr + R0; wb(c, a, rb(c, a) | d); return 3; }
		}
		break;
	}

	case 0xd: RN = rl(c, ((pc + 4) & ~3u) + (op & 0xff) * 4); return 1;

	case 0xe: RN = (int8_t)op; return 1;
	}

	/* not an SH-1 instruction (SH-2 ones included) */
	if (slot)
		return exception(c, VEC_SLOT_ILLEGAL, pc - 2);
	return exception(c, VEC_ILLEGAL, pc);
}

int sh1_step(sh1 *c)
{
	if (c->irq_level > (int)((c->sr >> 4) & 15))
	{
		const int vec = c->irq_vector, level = c->irq_level;
		c->sleeping = 0;
		int cyc = exception(c, vec, c->pc);
		if (level <= 15)
			c->sr = (c->sr & ~SR_I) | (level << 4);
		else
			c->sr |= SR_I;                  /* NMI: everything else masked */
		if (c->irq_ack)
			c->irq_ack(c->ctx, vec);
		return cyc;
	}
	if (c->sleeping)
		return 1;
	c->wait = 0;
	uint16_t op = c->fetch(c->ctx, c->pc);
	int cyc = exec(c, op, 0);
	return cyc + (int)c->wait;
}

/* ---- disassembler ---- */

void sh1_disasm(uint16_t op, uint32_t pc, char *o, int sz)
{
	const int n = (op >> 8) & 15, m = (op >> 4) & 15, d4 = op & 15, d8 = op & 0xff;
	const int s8 = (int8_t)op;
	int s12 = op & 0xfff;
	if (s12 & 0x800)
		s12 -= 0x1000;
#define P(...) snprintf(o, sz, __VA_ARGS__)
	switch (op >> 12)
	{
	case 0x0:
		switch (op & 0xf)
		{
		case 0x2: if (m < 3) { P("stc %s,r%d", m == 0 ? "sr" : m == 1 ? "gbr" : "vbr", n); return; } break;
		case 0x4: P("mov.b r%d,@(r0,r%d)", m, n); return;
		case 0x5: P("mov.w r%d,@(r0,r%d)", m, n); return;
		case 0x6: P("mov.l r%d,@(r0,r%d)", m, n); return;
		case 0x8: if (op == 8) { P("clrt"); return; } if (op == 0x18) { P("sett"); return; } if (op == 0x28) { P("clrmac"); return; } break;
		case 0x9: if (op == 9) { P("nop"); return; } if (op == 0x19) { P("div0u"); return; } if (m == 2) { P("movt r%d", n); return; } break;
		case 0xa: if (m < 3) { P("sts %s,r%d", m == 0 ? "mach" : m == 1 ? "macl" : "pr", n); return; } break;
		case 0xb: if (op == 0xb) { P("rts"); return; } if (op == 0x1b) { P("sleep"); return; } if (op == 0x2b) { P("rte"); return; } break;
		case 0xc: P("mov.b @(r0,r%d),r%d", m, n); return;
		case 0xd: P("mov.w @(r0,r%d),r%d", m, n); return;
		case 0xe: P("mov.l @(r0,r%d),r%d", m, n); return;
		}
		break;
	case 0x1: P("mov.l r%d,@(%d,r%d)", m, d4 * 4, n); return;
	case 0x2: {
		static const char *const t[16] = { "mov.b r%d,@r%d", "mov.w r%d,@r%d", "mov.l r%d,@r%d", 0,
			"mov.b r%d,@-r%d", "mov.w r%d,@-r%d", "mov.l r%d,@-r%d", "div0s r%d,r%d", "tst r%d,r%d",
			"and r%d,r%d", "xor r%d,r%d", "or r%d,r%d", "cmp/str r%d,r%d", "xtrct r%d,r%d",
			"mulu.w r%d,r%d", "muls.w r%d,r%d" };
		if (t[d4]) { P(t[d4], m, n); return; }
		break;
	}
	case 0x3: {
		static const char *const t[16] = { "cmp/eq", 0, "cmp/hs", "cmp/ge", "div1", 0, "cmp/hi", "cmp/gt",
			"sub", 0, "subc", "subv", "add", 0, "addc", "addv" };
		if (t[d4]) { P("%s r%d,r%d", t[d4], m, n); return; }
		break;
	}
	case 0x4:
		if (d4 == 0xf) { P("mac.w @r%d+,@r%d+", m, n); return; }
		switch (d8)
		{
		case 0x00: P("shll r%d", n); return;
		case 0x01: P("shlr r%d", n); return;
		case 0x20: P("shal r%d", n); return;
		case 0x21: P("shar r%d", n); return;
		case 0x04: P("rotl r%d", n); return;
		case 0x05: P("rotr r%d", n); return;
		case 0x24: P("rotcl r%d", n); return;
		case 0x25: P("rotcr r%d", n); return;
		case 0x08: P("shll2 r%d", n); return;
		case 0x09: P("shlr2 r%d", n); return;
		case 0x18: P("shll8 r%d", n); return;
		case 0x19: P("shlr8 r%d", n); return;
		case 0x28: P("shll16 r%d", n); return;
		case 0x29: P("shlr16 r%d", n); return;
		case 0x11: P("cmp/pz r%d", n); return;
		case 0x15: P("cmp/pl r%d", n); return;
		case 0x02: P("sts.l mach,@-r%d", n); return;
		case 0x12: P("sts.l macl,@-r%d", n); return;
		case 0x22: P("sts.l pr,@-r%d", n); return;
		case 0x03: P("stc.l sr,@-r%d", n); return;
		case 0x13: P("stc.l gbr,@-r%d", n); return;
		case 0x23: P("stc.l vbr,@-r%d", n); return;
		case 0x06: P("lds.l @r%d+,mach", n); return;
		case 0x16: P("lds.l @r%d+,macl", n); return;
		case 0x26: P("lds.l @r%d+,pr", n); return;
		case 0x07: P("ldc.l @r%d+,sr", n); return;
		case 0x17: P("ldc.l @r%d+,gbr", n); return;
		case 0x27: P("ldc.l @r%d+,vbr", n); return;
		case 0x0a: P("lds r%d,mach", n); return;
		case 0x1a: P("lds r%d,macl", n); return;
		case 0x2a: P("lds r%d,pr", n); return;
		case 0x0e: P("ldc r%d,sr", n); return;
		case 0x1e: P("ldc r%d,gbr", n); return;
		case 0x2e: P("ldc r%d,vbr", n); return;
		case 0x1b: P("tas.b @r%d", n); return;
		case 0x0b: P("jsr @r%d", n); return;
		case 0x2b: P("jmp @r%d", n); return;
		}
		break;
	case 0x5: P("mov.l @(%d,r%d),r%d", d4 * 4, m, n); return;
	case 0x6: {
		static const char *const t[16] = { "mov.b @r%d,r%d", "mov.w @r%d,r%d", "mov.l @r%d,r%d", "mov r%d,r%d",
			"mov.b @r%d+,r%d", "mov.w @r%d+,r%d", "mov.l @r%d+,r%d", "not r%d,r%d", "swap.b r%d,r%d",
			"swap.w r%d,r%d", "negc r%d,r%d", "neg r%d,r%d", "extu.b r%d,r%d", "extu.w r%d,r%d",
			"exts.b r%d,r%d", "exts.w r%d,r%d" };
		P(t[d4], m, n);
		return;
	}
	case 0x7: P("add #%d,r%d", s8, n); return;
	case 0x8:
		switch (n)
		{
		case 0x0: P("mov.b r0,@(%d,r%d)", d4, m); return;
		case 0x1: P("mov.w r0,@(%d,r%d)", d4 * 2, m); return;
		case 0x4: P("mov.b @(%d,r%d),r0", d4, m); return;
		case 0x5: P("mov.w @(%d,r%d),r0", d4 * 2, m); return;
		case 0x8: P("cmp/eq #%d,r0", s8); return;
		case 0x9: P("bt 0x%08x", pc + 4 + s8 * 2); return;
		case 0xb: P("bf 0x%08x", pc + 4 + s8 * 2); return;
		}
		break;
	case 0x9: P("mov.w @(0x%08x),r%d", pc + 4 + d8 * 2, n); return;
	case 0xa: P("bra 0x%08x", pc + 4 + s12 * 2); return;
	case 0xb: P("bsr 0x%08x", pc + 4 + s12 * 2); return;
	case 0xc: {
		switch (n)
		{
		case 0x0: P("mov.b r0,@(%d,gbr)", d8); return;
		case 0x1: P("mov.w r0,@(%d,gbr)", d8 * 2); return;
		case 0x2: P("mov.l r0,@(%d,gbr)", d8 * 4); return;
		case 0x3: P("trapa #%d", d8); return;
		case 0x4: P("mov.b @(%d,gbr),r0", d8); return;
		case 0x5: P("mov.w @(%d,gbr),r0", d8 * 2); return;
		case 0x6: P("mov.l @(%d,gbr),r0", d8 * 4); return;
		case 0x7: P("mova @(0x%08x),r0", ((pc + 4) & ~3u) + d8 * 4); return;
		case 0x8: P("tst #0x%02x,r0", d8); return;
		case 0x9: P("and #0x%02x,r0", d8); return;
		case 0xa: P("xor #0x%02x,r0", d8); return;
		case 0xb: P("or #0x%02x,r0", d8); return;
		case 0xc: P("tst.b #0x%02x,@(r0,gbr)", d8); return;
		case 0xd: P("and.b #0x%02x,@(r0,gbr)", d8); return;
		case 0xe: P("xor.b #0x%02x,@(r0,gbr)", d8); return;
		case 0xf: P("or.b #0x%02x,@(r0,gbr)", d8); return;
		}
		break;
	}
	case 0xd: P("mov.l @(0x%08x),r%d", ((pc + 4) & ~3u) + d8 * 4, n); return;
	case 0xe: P("mov #%d,r%d", s8, n); return;
	}
	P(".word 0x%04x", op);
#undef P
}
