/* Casiolator - minimal PNG writer (RGB8, uncompressed deflate blocks, no dependencies) */
#include <stdlib.h>
#include "png.h"
#include "os.h"

static uint32_t crc_table[256];

uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n)
{
	if (!crc_table[1])
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t v = i;
			for (int k = 0; k < 8; k++) v = (v & 1) ? 0xedb88320u ^ (v >> 1) : v >> 1;
			crc_table[i] = v;
		}
	c = ~c;
	while (n--) c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return ~c;
}

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
	uint8_t hdr[8];
	put32(hdr, len);
	memcpy(hdr + 4, type, 4);
	fwrite(hdr, 1, 8, f);
	if (len) fwrite(data, 1, len, f);
	uint32_t c = crc32_update(0, hdr + 4, 4);
	c = crc32_update(c, data, len);
	put32(hdr, c);
	fwrite(hdr, 1, 4, f);
}

int png_write(const char *path, const uint32_t *pixels, int stride, int w, int h)
{
	FILE *f = os_fopen(path, "wb");
	if (!f) return 0;
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	fwrite(sig, 1, 8, f);

	uint8_t ihdr[13];
	put32(ihdr, w); put32(ihdr + 4, h);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   // 8-bit RGB
	chunk(f, "IHDR", ihdr, 13);

	/* raw scanlines (filter 0) wrapped in stored deflate blocks */
	const size_t raw_len = (size_t)h * (1 + 3 * w);
	uint8_t *raw = malloc(raw_len);
	uint8_t *p = raw;
	for (int y = 0; y < h; y++)
	{
		*p++ = 0;
		for (int x = 0; x < w; x++)
		{
			uint32_t v = pixels[y * stride + x];
			*p++ = v >> 16; *p++ = v >> 8; *p++ = v;
		}
	}
	const size_t nblocks = (raw_len + 65534) / 65535;
	const size_t z_len = 2 + raw_len + nblocks * 5 + 4;
	uint8_t *z = malloc(z_len), *q = z;
	*q++ = 0x78; *q++ = 0x01;
	uint32_t a = 1, b = 0;
	for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
	for (size_t off = 0; off < raw_len; off += 65535)
	{
		size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
		*q++ = (off + n == raw_len) ? 1 : 0;
		*q++ = n & 0xff; *q++ = n >> 8; *q++ = ~n & 0xff; *q++ = (~n >> 8) & 0xff;
		memcpy(q, raw + off, n); q += n;
	}
	put32(q, (b << 16) | a);
	chunk(f, "IDAT", z, (uint32_t)z_len);
	chunk(f, "IEND", NULL, 0);
	free(raw); free(z);
	return fclose(f) == 0;
}
