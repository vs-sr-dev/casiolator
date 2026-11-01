/* Casiolator - opening a cartridge (see game.h)
 *
 * What people have on disk, all accepted as they are:
 *  - No-Intro: one zip per game; Loopy images byte-swapped (16-bit little-endian, as MAME
 *    loads them), the 3 MB games as two chips IC104.bin (2 MB) and IC105.bin (1 MB)
 *  - older sets: plain .bin, Loopy images in the CPU's big-endian order, chips joined
 * A Loopy image starts with its ROM start address, 0x0e000080; Magical Shop has no header
 * (the 2023 "header added" fix of it does). */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "game.h"
#include "zip.h"
#include "png.h"
#include "os.h"

#define MAX_ENTRIES 64

uint8_t *file_read(const char *path, size_t *size)
{
	FILE *f = os_fopen(path, "rb");
	if (!f)
		return NULL;
	os_fseek(f, 0, SEEK_END);
	long long n = os_ftell(f);
	os_fseek(f, 0, SEEK_SET);
	if (n <= 0 || n > 64 * 1024 * 1024)
	{
		fclose(f);
		return NULL;
	}
	uint8_t *p = malloc((size_t)n);
	if (p && fread(p, 1, (size_t)n, f) != (size_t)n)
	{
		free(p);
		p = NULL;
	}
	fclose(f);
	*size = (size_t)n;
	return p;
}

static const char *base_name(const char *p)
{
	const char *b = p;
	for (; *p; p++)
		if (*p == '/' || *p == '\\')
			b = p + 1;
	return b;
}

static int has_ext(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);
	if (n < e)
		return 0;
	for (size_t i = 0; i < e; i++)
		if (tolower((unsigned char)name[n - e + i]) != ext[i])
			return 0;
	return 1;
}

/* "Dig Dug (Japan).zip" -> "Dig Dug" */
static void make_title(const char *path, char *out, size_t n)
{
	snprintf(out, n, "%s", base_name(path));
	char *dot = strrchr(out, '.');
	if (dot && strlen(dot) <= 5 && !strchr(dot, ' '))
		*dot = 0;
	char *paren = strstr(out, " (");
	if (paren && paren != out)
		*paren = 0;
}

static int loopy_header(const uint8_t *p, size_t n, int *swapped)
{
	if (n < 4)
		return 0;
	if (p[0] == 0x0e && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x80)
		return *swapped = 0, 1;
	if (p[0] == 0x00 && p[1] == 0x0e && p[2] == 0x80 && p[3] == 0x00)
		return *swapped = 1, 1;
	return 0;
}

static void swap16(uint8_t *p, size_t n)
{
	for (size_t i = 0; i + 1 < n; i += 2)
	{
		uint8_t t = p[i];
		p[i] = p[i + 1];
		p[i + 1] = t;
	}
}

/* decide what the image is; takes ownership of data */
static int classify(game *g, uint8_t *data, size_t size, const char *name, char *err, size_t errn)
{
	int swapped = 0;
	if (size == 0x4000 && crc32_update(0, data, size) == core_pv2000.bios_crc)
	{
		snprintf(err, errn, "%s is the PV-2000 BIOS: keep it beside the games and open a game", name);
		free(data);
		return 0;
	}
	if (loopy_header(data, size, &swapped))
	{
		if (swapped)
			swap16(data, size);
		g->system = SYS_LOOPY;
	}
	else if ((size == 0x2000 || size == 0x4000) && data[0] == 0xc3 && data[2] >= 0xc0)
		g->system = SYS_PV2000;     /* starts with a jump into its own space, 0xc000 up */
	else if (size == 0x2000 || size == 0x4000 || size == 0x8000)
		g->system = SYS_PV1000;
	else if (size >= 0x80000 && size <= 0x400000 && !(size & 0xffff))
	{
		/* headerless Loopy image (Magical Shop): NOP (0x0009) is common in SH code, so
		 * count it in both byte orders; then the header kasami documents for it */
		size_t be = 0, le = 0;
		for (size_t i = 0x400; i + 1 < size && i < 0x40000; i += 2)
		{
			be += data[i] == 0x00 && data[i + 1] == 0x09;
			le += data[i] == 0x09 && data[i + 1] == 0x00;
		}
		if (le > be)
			swap16(data, size);
		static const uint8_t header[28] = {
			0x0e, 0x00, 0x00, 0x80, 0x0e, 0x0f, 0xff, 0xfe, 0x40, 0xf5, 0x4d, 0xd7, 0xff, 0xff, 0xff, 0xff,
			0x02, 0xff, 0xff, 0xff, 0x02, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xfe };
		if (size == 0x100000)
			memcpy(data, header, sizeof(header));
		g->system = SYS_LOOPY;
	}
	else
	{
		snprintf(err, errn, "%s: not a PV-1000, PV-2000 or Loopy cartridge (%u bytes)", name, (unsigned)size);
		free(data);
		return 0;
	}
	g->data = data;
	g->size = size;
	g->crc = crc32_update(0, data, size);
	g->sys = g->system == SYS_PV1000 ? &core_pv1000 : g->system == SYS_PV2000 ? &core_pv2000 : &core_loopy;
	return 1;
}

static int ignorable(const char *name)
{
	static const char *const skip[] = { ".txt", ".nfo", ".diz", ".xml", ".dat", ".jpg", ".png", ".pdf", ".md" };
	for (size_t i = 0; i < sizeof(skip) / sizeof(skip[0]); i++)
		if (has_ext(name, skip[i]))
			return 1;
	return 0;
}

static int open_zip(const uint8_t *zip, size_t size, game *g, const char *path, char *err, size_t errn)
{
	static zip_entry e[MAX_ENTRIES];
	int n = zip_list(zip, size, e, MAX_ENTRIES);
	if (n < 0)
	{
		snprintf(err, errn, "%s: damaged zip file", base_name(path));
		return 0;
	}
	int ic104 = -1, ic105 = -1, roms = 0, zips = 0, pick = -1;
	for (int i = 0; i < n; i++)
	{
		const char *b = base_name(e[i].name);
		if (ignorable(b))
			continue;
		if (has_ext(b, ".zip"))
		{
			zips++;
			continue;
		}
		roms++;
		if (pick < 0)
			pick = i;
		for (const char *c = b; *c; c++)
		{
			if (!strncmp(c, "IC104", 5) || !strncmp(c, "ic104", 5))
				ic104 = i;
			if (!strncmp(c, "IC105", 5) || !strncmp(c, "ic105", 5))
				ic105 = i;
		}
	}
	if (ic104 >= 0 && ic105 >= 0)
	{
		/* Loopy cartridge with two mask ROMs: IC104 first */
		uint8_t *a = zip_extract(zip, size, &e[ic104]), *b = zip_extract(zip, size, &e[ic105]);
		uint8_t *all = a && b ? malloc(e[ic104].size + e[ic105].size) : NULL;
		if (!all)
		{
			free(a);
			free(b);
			snprintf(err, errn, "%s: cannot read the ROM chips in it", base_name(path));
			return 0;
		}
		memcpy(all, a, e[ic104].size);
		memcpy(all + e[ic104].size, b, e[ic105].size);
		free(a);
		free(b);
		return classify(g, all, e[ic104].size + e[ic105].size, base_name(path), err, errn);
	}
	if (roms == 0)
	{
		if (zips > 1)
			snprintf(err, errn, "%s holds a whole collection: unpack it and open one game", base_name(path));
		else
			snprintf(err, errn, "%s: no cartridge inside", base_name(path));
		return 0;
	}
	if (roms > 1)
	{
		snprintf(err, errn, "%s holds more than one file: unpack it and open the game", base_name(path));
		return 0;
	}
	uint8_t *data = zip_extract(zip, size, &e[pick]);
	if (!data)
	{
		snprintf(err, errn, "%s: cannot unpack %s", base_name(path), e[pick].name);
		return 0;
	}
	return classify(g, data, e[pick].size, base_name(e[pick].name), err, errn);
}

/* a chip of a split Loopy cartridge opened on its own: look for its partner beside it */
static int open_split(const char *path, uint8_t *data, size_t size, game *g, char *err, size_t errn)
{
	const char *b = base_name(path);
	const char *tag = strstr(b, "IC104") ? "IC104" : strstr(b, "ic104") ? "ic104"
		: strstr(b, "IC105") ? "IC105" : strstr(b, "ic105") ? "ic105" : NULL;
	if (!tag)
		return -1;
	char other[4096];
	snprintf(other, sizeof(other), "%s", path);
	char *t = other + (b - path) + (strstr(b, tag) - b);
	t[4] = t[4] == '4' ? '5' : '4';
	size_t osize;
	uint8_t *odata = file_read(other, &osize);
	if (!odata)
	{
		snprintf(err, errn, "%s is half of a cartridge: %s should be beside it", b, base_name(other));
		free(data);
		return 0;
	}
	const int first = tag[4] == '4';
	uint8_t *all = malloc(size + osize);
	if (!all)
	{
		free(data);
		free(odata);
		return 0;
	}
	memcpy(all, first ? data : odata, first ? size : osize);
	memcpy(all + (first ? size : osize), first ? odata : data, first ? osize : size);
	free(data);
	free(odata);
	return classify(g, all, size + osize, b, err, errn);
}

int game_open(const char *path, game *g, char *err, size_t errn)
{
	memset(g, 0, sizeof(*g));
	size_t size;
	uint8_t *data = file_read(path, &size);
	if (!data)
	{
		snprintf(err, errn, "cannot read %s", base_name(path));
		return 0;
	}
	int ok;
	if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4)
	{
		ok = open_zip(data, size, g, path, err, errn);
		free(data);
	}
	else if ((ok = open_split(path, data, size, g, err, errn)) < 0)
		ok = classify(g, data, size, base_name(path), err, errn);
	if (!ok)
		return 0;

	/* the title: from the zip, or from the folder for a chip of a split set */
	const char *b = base_name(path);
	if (!strncmp(b, "IC10", 4) || !strncmp(b, "ic10", 4))
	{
		char dir[4096];
		snprintf(dir, sizeof(dir), "%.*s", (int)(b - path > 0 ? b - path - 1 : 0), path);
		make_title(dir, g->title, sizeof(g->title));
	}
	else
		make_title(path, g->title, sizeof(g->title));
	return 1;
}

int bios_open(const char *path, uint32_t want_crc, size_t want_size, uint8_t **out, size_t *out_size,
	char *err, size_t errn)
{
	size_t size;
	uint8_t *data = file_read(path, &size);
	*out = NULL;
	if (!data)
	{
		snprintf(err, errn, "cannot read %s", base_name(path));
		return 0;
	}
	if (size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4)
	{
		/* a zip (MAME's casloopy.zip, say): the entry with the right CRC, else the size */
		static zip_entry e[MAX_ENTRIES];
		const int n = zip_list(data, size, e, MAX_ENTRIES);
		int pick = -1;
		for (int i = 0; i < n && pick < 0; i++)
			if (e[i].crc == want_crc)
				pick = i;
		for (int i = 0; i < n && pick < 0; i++)
			if (e[i].size == want_size)
				pick = i;
		uint8_t *b = pick >= 0 ? zip_extract(data, size, &e[pick]) : NULL;
		free(data);
		if (!b)
		{
			snprintf(err, errn, "%s has no %u KB BIOS in it", base_name(path), (unsigned)(want_size / 1024));
			return 0;
		}
		data = b;
		size = e[pick].size;
	}
	if (size != want_size)
	{
		snprintf(err, errn, "%s is not the BIOS (%u KB, it should be %u KB)", base_name(path),
			(unsigned)(size / 1024), (unsigned)(want_size / 1024));
		free(data);
		return 0;
	}
	*out = data;
	*out_size = size;
	return crc32_update(0, data, size) == want_crc ? 1 : 2;
}

void game_free(game *g)
{
	free(g->data);
	memset(g, 0, sizeof(*g));
}
