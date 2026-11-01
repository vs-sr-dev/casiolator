/* Casiolator - SN76489A sound generator (see sn76489.h)
 *
 * Writes: 1rrrdddd latches register rrr and sets its low 4 bits; 0-dddddd sets the high 6
 * bits of a tone period (any other register takes the low bits again). A tone flips its
 * output every period counts (period 0 counts as 0x400). The noise register: bit 2 white
 * (else periodic), bits 0-1 the shift rate: clock / 512, 1024, 2048, or tone 2's. Writing
 * it restarts the shift register. */
#include <string.h>
#include "sn76489.h"

#define LFSR_TOP 0x10000            /* 17-bit register, fed at the top; white noise
                                     * feeds back bits 2 xor 3 */

static const uint16_t volume[16] = {
	0x1800, 0x1312, 0x0f2c, 0x0c15, 0x099f, 0x07a9, 0x061a, 0x04da,
	0x03db, 0x0311, 0x0270, 0x01ef, 0x0189, 0x0138, 0x00f7, 0
};

void sn_reset(sn76489 *s)
{
	memset(s, 0, sizeof(*s));
	for (int i = 1; i < 8; i += 2)
		s->reg[i] = 15;             /* silent */
	s->lfsr = LFSR_TOP;
}

void sn_write(sn76489 *s, uint8_t data)
{
	if (data & 0x80)
	{
		s->latched = (data >> 4) & 7;
		const int r = s->latched;
		s->reg[r] = (s->reg[r] & 0x3f0) | (data & 15);
	}
	else
	{
		const int r = s->latched;
		if (r & 1 || r == 6)
			s->reg[r] = data & 15;
		else
			s->reg[r] = (s->reg[r] & 15) | ((data & 0x3f) << 4);
	}
	if (s->latched == 6)
	{
		s->reg[6] &= 7;
		s->lfsr = LFSR_TOP;
	}
}

static uint16_t period(const sn76489 *s, int ch)
{
	uint16_t p;
	if (ch < 3)
		p = s->reg[ch * 2];
	else
	{
		const int rate = s->reg[6] & 3;
		p = rate == 3 ? s->reg[4] : 0x10 << rate;
	}
	return p ? p : 0x400;
}

int sn_sample(sn76489 *s)
{
	int sum = 0;
	for (int step = 0; step < SN_DIV / 16; step++)
	{
		for (int ch = 0; ch < 4; ch++)
		{
			if (s->count[ch] > 0)
				s->count[ch]--;
			if (s->count[ch])
				continue;
			s->count[ch] = period(s, ch);
			s->out[ch] ^= 1;
			if (ch == 3 && s->out[3])
			{
				/* the shift register steps once a period of the noise square */
				const uint32_t in = s->reg[6] & 4 ? ((s->lfsr >> 2) ^ (s->lfsr >> 3)) & 1 : s->lfsr & 1;
				s->lfsr = (s->lfsr >> 1) | (in ? LFSR_TOP : 0);
			}
		}
		for (int ch = 0; ch < 3; ch++)
			if (s->out[ch])
				sum += volume[s->reg[ch * 2 + 1]];
		if (s->lfsr & 1)
			sum += volume[s->reg[7]];
	}
	return sum / (SN_DIV / 16);
}
