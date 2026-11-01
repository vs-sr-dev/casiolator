/* Casiolator - opening a cartridge: plain or zipped, whichever system it belongs to */
#ifndef CL_GAME_H
#define CL_GAME_H

#include <stddef.h>
#include <stdint.h>
#include "core.h"

enum { SYS_NONE, SYS_PV1000, SYS_LOOPY };

typedef struct
{
	int system;
	const core *sys;            /* NULL if the system has no core yet */
	uint8_t *data;              /* the cartridge; Loopy images big-endian, chips joined */
	size_t size;
	uint32_t crc;
	char title[256];            /* file name without extension and region tags */
} game;

/* 1 if path is a cartridge (or a zip with one); otherwise 0 and a message in err */
int game_open(const char *path, game *g, char *err, size_t errn);
void game_free(game *g);

/* a BIOS image from a plain file or a zip: 1 if it has the expected CRC, 2 if only the size
 * matches (another dump), 0 and a message if it is not usable; *out is malloc'd */
int bios_open(const char *path, uint32_t want_crc, size_t want_size, uint8_t **out, size_t *out_size,
	char *err, size_t errn);

/* whole file into memory (malloc'd), NULL if it cannot be read */
uint8_t *file_read(const char *path, size_t *size);

#endif
