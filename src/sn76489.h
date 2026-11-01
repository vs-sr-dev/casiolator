/* Casiolator - Texas Instruments SN76489A sound generator
 *
 * Three square-wave tones and a noise generator, 16 attenuation steps of 2 dB each. The
 * counters run at the input clock / 16; a sample is the average of 4 counter steps
 * (clock / 64). From TI's SN76489AN data sheet; the noise register's length and taps are
 * the A part's (17 bits, as on the SN76496). */
#ifndef CL_SN76489_H
#define CL_SN76489_H

#include <stdint.h>

#define SN_DIV 64                   /* input clocks a sample */

typedef struct
{
	uint16_t reg[8];                /* tone 0, vol 0, tone 1, vol 1, tone 2, vol 2, noise, vol 3 */
	uint8_t latched;                /* register the last first byte chose */
	uint16_t count[4];
	uint8_t out[4];
	uint32_t lfsr;
} sn76489;

void sn_reset(sn76489 *s);
void sn_write(sn76489 *s, uint8_t data);
/* one sample, 0..about 4 * 0x1800 (unipolar, as the chip's output) */
int sn_sample(sn76489 *s);

#endif
