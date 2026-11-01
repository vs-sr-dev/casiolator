/* Casiolator - runs a CP/M .com Z80 exerciser (zexdoc, zexall) on the Z80 core
 *
 * usage: zextest file.com
 * BDOS functions 2 (print E) and 9 (print the string at DE up to '$') are trapped at
 * 0xfe00; a jump to 0 ends the run. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/z80.h"

static uint8_t mem[65536];

static uint8_t rd(void *c, uint16_t a) { return mem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { mem[a] = v; }
static uint8_t in(void *c, uint16_t p) { return 0xff; }
static void out(void *c, uint16_t p, uint8_t v) { }

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		fprintf(stderr, "usage: zextest file.com\n");
		return 2;
	}
	FILE *f = fopen(argv[1], "rb");
	if (!f)
	{
		perror(argv[1]);
		return 2;
	}
	size_t n = fread(mem + 0x100, 1, 0x10000 - 0x100, f);
	fclose(f);
	if (!n)
		return 2;

	mem[0] = 0x76;                        /* HALT */
	mem[5] = 0xc3;                        /* JP 0xfe00: BDOS, also the top of the TPA */
	mem[6] = 0x00;
	mem[7] = 0xfe;
	mem[0xfe00] = 0xc9;                   /* RET */

	z80 z = { 0 };
	z.read = rd;
	z.write = wr;
	z.in = in;
	z.out = out;
	z80_init(&z);
	z.pc.w = 0x100;
	z.sp.w = 0xfe00;

	unsigned long long cycles = 0;
	for (;;)
	{
		if (z.pc.w == 0xfe00)
		{
			if (z.bc.b.l == 2)
				putchar(z.de.b.l);
			else if (z.bc.b.l == 9)
				for (uint16_t a = z.de.w; mem[a] != '$'; a++)
					putchar(mem[a]);
			fflush(stdout);
		}
		if (z.pc.w == 0)
			break;
		cycles += z80_step(&z);
	}
	printf("\n%llu T-states\n", cycles);
	return 0;
}
