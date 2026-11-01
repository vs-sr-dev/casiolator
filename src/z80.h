/* Casiolator - Zilog Z80 interpreter
 *
 * Instruction-stepped, with T-state counts, the undocumented flags (X/Y, MEMPTR) and the
 * undocumented opcodes. Checked with zexdoc/zexall (tests/zextest.c). */
#ifndef CL_Z80_H
#define CL_Z80_H

#include <stdint.h>

/* register pair; the hosts we build for are little-endian */
typedef union { uint16_t w; struct { uint8_t l, h; } b; } z80_pair;

typedef struct z80
{
	z80_pair af, bc, de, hl, ix, iy, sp, pc, wz;
	z80_pair af2, bc2, de2, hl2;
	uint8_t i, r;
	uint8_t iff1, iff2, im, halted;
	uint8_t ei_delay;         /* EI just executed: no interrupt before the next instruction */
	uint8_t irq;              /* level of the /INT line */
	uint8_t nmi;              /* NMI edge pending */
	uint8_t irq_vector;       /* what the bus holds during the interrupt acknowledge */

	void *ctx;
	uint8_t (*read)(void *ctx, uint16_t addr);
	void (*write)(void *ctx, uint16_t addr, uint8_t v);
	uint8_t (*in)(void *ctx, uint16_t port);
	void (*out)(void *ctx, uint16_t port, uint8_t v);
} z80;

void z80_init(z80 *z);
void z80_reset(z80 *z);
/* one instruction (or an interrupt acknowledge, or one HALT cycle); T-states taken */
int z80_step(z80 *z);

#endif
