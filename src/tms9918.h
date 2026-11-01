/* Casiolator - Texas Instruments TMS9918A / TMS9928A video display processor
 *
 * 16 KB of VRAM behind two ports, 8 write-only registers, a status register. Graphics I
 * and II, multicolour and text modes, 32 sprites (4 a line, 8x8 or 16x16, magnified),
 * the fifth-sprite and collision flags. Drawn a line at a time from the state at the
 * line's start. From TI's TMS9918A/TMS9928A/TMS9929A data manual. */
#ifndef CL_TMS9918_H
#define CL_TMS9918_H

#include <stdint.h>

#define TMS_W 256                   /* active pixels */
#define TMS_H 192                   /* active lines */

typedef struct
{
	uint8_t vram[0x4000];
	uint8_t reg[8];
	uint8_t status;                 /* F, 5S, C, fifth sprite number */
	uint8_t latch, second;          /* first byte of a control write; 1 while it waits */
	uint8_t buffer;                 /* read-ahead */
	uint16_t addr;
} tms9918;

void tms_reset(tms9918 *v);
/* port 0 data, port 1 control/status */
uint8_t tms_read(tms9918 *v, int port);
void tms_write(tms9918 *v, int port, uint8_t data);
/* the /INT output: frame flag set and interrupts enabled */
int tms_int(const tms9918 *v);
/* sets the frame flag (the end of the last active line) */
void tms_frame(tms9918 *v);
/* active line y (0..191), 256 pixels 0x00RRGGBB; also the sprite flags */
void tms_line(tms9918 *v, int y, uint32_t *out);
/* the border colour */
uint32_t tms_backdrop(const tms9918 *v);

#endif
