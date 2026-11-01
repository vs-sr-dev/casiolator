/* Casiolator - the SH7021's on-chip peripherals (0x05fffe00-0x05ffffff)
 *
 * Interrupt controller, 5-channel timer unit (ITU), 4-channel DMA controller, 2 serial
 * channels (SCI), bus state controller, ports and watchdog. Time is in CPU cycles. */
#ifndef CL_SH7021_H
#define CL_SH7021_H

#include <stdint.h>
#include "sh1.h"

/* interrupt sources, in the fixed order that breaks priority ties */
enum {
	INT_IRQ0, INT_IRQ1, INT_IRQ2, INT_IRQ3, INT_IRQ4, INT_IRQ5, INT_IRQ6, INT_IRQ7,
	INT_DEI0, INT_DEI1, INT_DEI2, INT_DEI3,
	INT_IMIA0, INT_IMIB0, INT_OVI0, INT_IMIA1, INT_IMIB1, INT_OVI1,
	INT_IMIA2, INT_IMIB2, INT_OVI2, INT_IMIA3, INT_IMIB3, INT_OVI3,
	INT_IMIA4, INT_IMIB4, INT_OVI4,
	INT_ERI0, INT_RXI0, INT_TXI0, INT_TEI0, INT_ERI1, INT_RXI1, INT_TXI1, INT_TEI1,
	INT_ITI, INT_CMI,
	INT_COUNT
};

typedef struct
{
	uint8_t tcr, tior, tier, tsr;
	uint16_t tcnt, gra, grb, bra, brb;
	uint32_t sub;               /* CPU cycles towards the next count */
} itu_ch;

typedef struct
{
	uint32_t sar, dar;
	uint16_t tcr, chcr;
} dma_ch;

typedef struct
{
	uint8_t smr, brr, scr, tdr, ssr, rdr;
	int tx_busy;                /* a byte is in the shift register */
	uint64_t tx_done;           /* when it has gone out */
	uint8_t tsr;
} sci_ch;

typedef struct sh7021
{
	sh1 *cpu;
	uint64_t now;               /* CPU cycles */

	/* INTC */
	uint16_t ipr[5], icr;
	uint8_t irq_pin[8];         /* IRQ0-7 input levels (1 = asserted) */
	uint8_t irq_edge[8];        /* latched edge requests */
	uint8_t nmi_pending;

	/* ITU */
	uint8_t tstr, tsnc, tmdr, tfcr, tocr;
	itu_ch itu[5];
	uint64_t itu_time;

	/* DMAC */
	dma_ch dma[4];
	uint16_t dmaor;

	/* SCI */
	sci_ch sci[2];

	/* BSC, ports, PFC, watchdog, standby: kept as written */
	uint16_t bsc[10];           /* BCR WCR1 WCR2 WCR3 DCR PCR RCR RTCSR RTCNT RTCOR */
	uint16_t padr, pbdr, paior, pbior, pacr1, pacr2, pbcr1, pbcr2, cascr;
	uint8_t wdt_tcsr, wdt_tcnt, wdt_rstcsr, sbycr;
	uint16_t pa_in, pb_in;      /* what the pins read when they are inputs */

	/* hooks to the rest of the machine */
	void *ctx;
	void (*sci_tx)(void *ctx, int ch, uint8_t byte);       /* a byte left the line */
	/* the DMA moves data on the machine's bus */
	uint8_t (*bus_r8)(void *ctx, uint32_t a);
	uint16_t (*bus_r16)(void *ctx, uint32_t a);
	void (*bus_w8)(void *ctx, uint32_t a, uint8_t v);
	void (*bus_w16)(void *ctx, uint32_t a, uint16_t v);
	int (*bus_cost)(void *ctx, uint32_t a);                 /* cycles per access */
	uint32_t dma_cycles;        /* bus cycles the DMA took from the CPU, for the machine */
	int log_unknown;
} sh7021;

void sh7021_reset(sh7021 *s);
/* bring timers and serial lines up to s->now */
void sh7021_sync(sh7021 *s);
/* CPU cycles from s->now until the next timer or serial event (a large value if none) */
uint32_t sh7021_next_event(sh7021 *s);

uint8_t sh7021_r8(sh7021 *s, uint32_t a);
uint16_t sh7021_r16(sh7021 *s, uint32_t a);
void sh7021_w8(sh7021 *s, uint32_t a, uint8_t v);
void sh7021_w16(sh7021 *s, uint32_t a, uint16_t v);

/* IRQ0-7 pin levels from outside; NMI is an edge */
void sh7021_set_irq(sh7021 *s, int n, int level);
void sh7021_nmi(sh7021 *s);
/* the interrupt the CPU took */
void sh7021_ack(sh7021 *s, int vector);

#endif
