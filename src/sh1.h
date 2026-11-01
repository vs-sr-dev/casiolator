/* Casiolator - Hitachi SH-1 CPU core (the SH7021 in the Casio Loopy)
 *
 * Written from Hitachi's SH-1/SH-2 Programming Manual (1995, 3rd edition): the SH-1
 * instruction set only (SH-2 additions are illegal instructions, as on the chip), MAC.W
 * accumulating into 42 bits, delayed branches with the slot instruction run before the
 * branch completes. */
#ifndef CL_SH1_H
#define CL_SH1_H

#include <stdint.h>

#define SR_T 0x001
#define SR_S 0x002
#define SR_I 0x0f0
#define SR_Q 0x100
#define SR_M 0x200
#define SR_MASK 0x3f3

/* exception vector numbers */
enum { VEC_POWERON_PC = 0, VEC_POWERON_SP = 1, VEC_ILLEGAL = 4, VEC_SLOT_ILLEGAL = 6,
       VEC_CPU_ADDRESS = 9, VEC_DMA_ADDRESS = 10, VEC_NMI = 11 };

typedef struct sh1
{
	uint32_t r[16];
	uint32_t sr, gbr, vbr, mach, macl, pr, pc;
	int sleeping;

	/* the highest interrupt request (level 1-15, 16 for NMI) and its vector, kept up to
	 * date by the interrupt controller; accepted when the level is above SR.I (NMI always) */
	int irq_level, irq_vector;

	/* cycles taken beyond one per bus access, added by the bus callbacks */
	uint32_t wait;

	void *ctx;
	uint16_t (*fetch)(void *ctx, uint32_t a);
	uint8_t (*read8)(void *ctx, uint32_t a);
	uint16_t (*read16)(void *ctx, uint32_t a);
	uint32_t (*read32)(void *ctx, uint32_t a);
	void (*write8)(void *ctx, uint32_t a, uint8_t v);
	void (*write16)(void *ctx, uint32_t a, uint16_t v);
	void (*write32)(void *ctx, uint32_t a, uint32_t v);
	/* an interrupt was taken: the controller clears edge requests and recomputes */
	void (*irq_ack)(void *ctx, int vector);
} sh1;

/* power-on reset: PC and SP from the vector table */
void sh1_reset(sh1 *c);
/* one instruction (with its delay slot), an exception, or a sleeping cycle; cycles taken */
int sh1_step(sh1 *c);
/* disassemble the instruction op at pc into out */
void sh1_disasm(uint16_t op, uint32_t pc, char *out, int n);

#endif
