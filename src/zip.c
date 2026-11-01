/* Casiolator - zip reading (see zip.h); the inflater follows RFC 1951 in the way of
 * Mark Adler's puff: canonical Huffman codes decoded a bit at a time, which is plenty for
 * cartridges of a few megabytes. */
#include <stdlib.h>
#include <string.h>
#include "zip.h"
#include "png.h"

/* ---- inflate ---- */

typedef struct
{
	const uint8_t *in;
	size_t inn, inpos;
	uint32_t bitbuf;
	int bitcnt;
	uint8_t *out;
	size_t outn, outpos;
	int error;
} inf_state;

typedef struct
{
	short count[16];            /* codes of each length */
	short symbol[288];          /* symbols ordered by code */
} huffman;

static int bits(inf_state *s, int need)
{
	uint32_t val = s->bitbuf;
	while (s->bitcnt < need)
	{
		if (s->inpos >= s->inn)
		{
			s->error = 1;
			return 0;
		}
		val |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
		s->bitcnt += 8;
	}
	s->bitbuf = val >> need;
	s->bitcnt -= need;
	return (int)(val & ((1u << need) - 1));
}

static int decode(inf_state *s, const huffman *h)
{
	int code = 0, first = 0, index = 0;
	for (int len = 1; len < 16; len++)
	{
		code |= bits(s, 1);
		int count = h->count[len];
		if (code - count < first)
			return h->symbol[index + (code - first)];
		index += count;
		first += count;
		first <<= 1;
		code <<= 1;
	}
	s->error = 1;
	return 0;
}

/* 0 if the lengths make a complete or a single-code set */
static int construct(huffman *h, const short *length, int n)
{
	short offs[16];
	memset(h->count, 0, sizeof(h->count));
	for (int sym = 0; sym < n; sym++)
		h->count[length[sym]]++;
	if (h->count[0] == n)
		return 0;
	int left = 1;
	for (int len = 1; len < 16; len++)
	{
		left <<= 1;
		left -= h->count[len];
		if (left < 0)
			return -1;
	}
	offs[1] = 0;
	for (int len = 1; len < 15; len++)
		offs[len + 1] = offs[len] + h->count[len];
	for (int sym = 0; sym < n; sym++)
		if (length[sym])
			h->symbol[offs[length[sym]]++] = (short)sym;
	return left;
}

static int put(inf_state *s, uint8_t b)
{
	if (s->outpos >= s->outn)
		return s->error = 1, 0;
	s->out[s->outpos++] = b;
	return 1;
}

static void codes(inf_state *s, const huffman *lencode, const huffman *distcode)
{
	static const short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
		35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
	static const short lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
		3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
	static const short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
		257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
	static const short dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
		7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

	for (;;)
	{
		int sym = decode(s, lencode);
		if (s->error)
			return;
		if (sym < 256)
		{
			if (!put(s, (uint8_t)sym))
				return;
		}
		else if (sym == 256)
			return;
		else
		{
			sym -= 257;
			if (sym >= 29)
			{
				s->error = 1;
				return;
			}
			int len = lbase[sym] + bits(s, lext[sym]);
			int dsym = decode(s, distcode);
			if (s->error || dsym >= 30)
			{
				s->error = 1;
				return;
			}
			size_t dist = dbase[dsym] + bits(s, dext[dsym]);
			if (dist > s->outpos)
			{
				s->error = 1;
				return;
			}
			while (len--)
				if (!put(s, s->out[s->outpos - dist]))
					return;
		}
	}
}

static void stored(inf_state *s)
{
	s->bitbuf = 0;
	s->bitcnt = 0;
	if (s->inpos + 4 > s->inn)
	{
		s->error = 1;
		return;
	}
	unsigned len = s->in[s->inpos] | (s->in[s->inpos + 1] << 8);
	unsigned nlen = s->in[s->inpos + 2] | (s->in[s->inpos + 3] << 8);
	s->inpos += 4;
	if (len != (~nlen & 0xffff) || s->inpos + len > s->inn || s->outpos + len > s->outn)
	{
		s->error = 1;
		return;
	}
	memcpy(s->out + s->outpos, s->in + s->inpos, len);
	s->inpos += len;
	s->outpos += len;
}

static void fixed(inf_state *s)
{
	static huffman lencode, distcode;
	static int built;
	if (!built)
	{
		short lengths[288];
		int sym = 0;
		for (; sym < 144; sym++) lengths[sym] = 8;
		for (; sym < 256; sym++) lengths[sym] = 9;
		for (; sym < 280; sym++) lengths[sym] = 7;
		for (; sym < 288; sym++) lengths[sym] = 8;
		construct(&lencode, lengths, 288);
		for (sym = 0; sym < 30; sym++) lengths[sym] = 5;
		construct(&distcode, lengths, 30);
		built = 1;
	}
	codes(s, &lencode, &distcode);
}

static void dynamic(inf_state *s)
{
	static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
	short lengths[320];
	huffman lencode, distcode;

	int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
	if (s->error || nlen > 286 || ndist > 30)
	{
		s->error = 1;
		return;
	}
	int index;
	for (index = 0; index < ncode; index++)
		lengths[order[index]] = (short)bits(s, 3);
	for (; index < 19; index++)
		lengths[order[index]] = 0;
	if (construct(&lencode, lengths, 19) != 0)
	{
		s->error = 1;
		return;
	}
	index = 0;
	while (index < nlen + ndist)
	{
		int sym = decode(s, &lencode);
		if (s->error)
			return;
		if (sym < 16)
			lengths[index++] = (short)sym;
		else
		{
			int len = 0, rep;
			if (sym == 16)
			{
				if (index == 0)
				{
					s->error = 1;
					return;
				}
				len = lengths[index - 1];
				rep = 3 + bits(s, 2);
			}
			else if (sym == 17)
				rep = 3 + bits(s, 3);
			else
				rep = 11 + bits(s, 7);
			if (index + rep > nlen + ndist)
			{
				s->error = 1;
				return;
			}
			while (rep--)
				lengths[index++] = (short)len;
		}
	}
	if (lengths[256] == 0)
	{
		s->error = 1;
		return;
	}
	int err = construct(&lencode, lengths, nlen);
	if (err < 0 || (err > 0 && nlen - lencode.count[0] != 1))
	{
		s->error = 1;
		return;
	}
	err = construct(&distcode, lengths + nlen, ndist);
	if (err < 0 || (err > 0 && ndist - distcode.count[0] != 1))
	{
		s->error = 1;
		return;
	}
	codes(s, &lencode, &distcode);
}

int inflate_raw(const uint8_t *in, size_t inn, uint8_t *out, size_t outn)
{
	inf_state s = { in, inn, 0, 0, 0, out, outn, 0, 0 };
	int last;
	do
	{
		last = bits(&s, 1);
		int type = bits(&s, 2);
		if (s.error)
			return 0;
		if (type == 0)
			stored(&s);
		else if (type == 1)
			fixed(&s);
		else if (type == 2)
			dynamic(&s);
		else
			return 0;
		if (s.error)
			return 0;
	} while (!last);
	return s.outpos == outn;
}

/* ---- zip ---- */

static uint32_t le16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int zip_list(const uint8_t *zip, size_t size, zip_entry *out, int max)
{
	if (size < 22 || le32(zip) != 0x04034b50)
		return -1;
	/* end of central directory: the last signature within the comment's reach */
	size_t eocd = 0;
	int found = 0;
	for (size_t i = size - 22 + 1; i-- > 0 && size - i <= 22 + 65535;)
		if (le32(zip + i) == 0x06054b50)
		{
			eocd = i;
			found = 1;
			break;
		}
	if (!found)
		return -1;
	uint32_t entries = le16(zip + eocd + 10), cd = le32(zip + eocd + 16);
	int n = 0;
	size_t p = cd;
	for (uint32_t i = 0; i < entries && n < max; i++)
	{
		if (p + 46 > size || le32(zip + p) != 0x02014b50)
			break;
		uint32_t nlen = le16(zip + p + 28), xlen = le16(zip + p + 30), clen = le16(zip + p + 32);
		if (p + 46 + nlen > size)
			break;
		zip_entry *e = &out[n];
		size_t k = nlen < sizeof(e->name) - 1 ? nlen : sizeof(e->name) - 1;
		memcpy(e->name, zip + p + 46, k);
		e->name[k] = 0;
		e->method = (uint16_t)le16(zip + p + 10);
		e->crc = le32(zip + p + 16);
		e->csize = le32(zip + p + 20);
		e->size = le32(zip + p + 24);
		e->offset = le32(zip + p + 42);
		if (k && e->name[k - 1] != '/')
			n++;
		p += 46 + nlen + xlen + clen;
	}
	return n;
}

uint8_t *zip_extract(const uint8_t *zip, size_t size, const zip_entry *e)
{
	size_t p = e->offset;
	if (p + 30 > size || le32(zip + p) != 0x04034b50)
		return NULL;
	size_t data = p + 30 + le16(zip + p + 26) + le16(zip + p + 28);
	if (data + e->csize > size)
		return NULL;
	uint8_t *out = malloc(e->size ? e->size : 1);
	if (!out)
		return NULL;
	int ok = 0;
	if (e->method == 0 && e->csize == e->size)
	{
		memcpy(out, zip + data, e->size);
		ok = 1;
	}
	else if (e->method == 8)
		ok = inflate_raw(zip + data, e->csize, out, e->size);
	if (!ok || crc32_update(0, out, e->size) != e->crc)
	{
		free(out);
		return NULL;
	}
	return out;
}
