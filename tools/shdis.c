/* Casiolator - disassemble a big-endian SH-1 binary: shdis file base start end */
#include <stdio.h>
#include <stdlib.h>
#include "../src/sh1.h"

int main(int argc, char **argv)
{
	if (argc < 5) { fprintf(stderr, "usage: shdis file base start end\n"); return 2; }
	FILE *f = fopen(argv[1], "rb");
	if (!f) { perror(argv[1]); return 2; }
	static unsigned char b[8 << 20];
	size_t n = fread(b, 1, sizeof(b), f);
	fclose(f);
	unsigned long base = strtoul(argv[2], 0, 0), s = strtoul(argv[3], 0, 0), e = strtoul(argv[4], 0, 0);
	for (unsigned long a = s & ~1ul; a < e && a - base + 1 < n; a += 2)
	{
		unsigned op = (b[a - base] << 8) | b[a - base + 1];
		char t[64];
		sh1_disasm((unsigned short)op, (unsigned)a, t, sizeof(t));
		printf("%08lx  %04x  %s\n", a, op, t);
	}
	return 0;
}
