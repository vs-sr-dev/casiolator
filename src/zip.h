/* Casiolator - reading zip archives (stored and deflated entries) */
#ifndef CL_ZIP_H
#define CL_ZIP_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
	char name[260];             /* path inside the archive, UTF-8 or CP437 as stored */
	uint32_t size;              /* uncompressed */
	uint32_t csize;
	uint32_t crc;
	uint16_t method;            /* 0 stored, 8 deflate */
	uint32_t offset;            /* local header */
} zip_entry;

/* the entries of a zip held in memory (directories left out); count, or -1 if not a zip */
int zip_list(const uint8_t *zip, size_t size, zip_entry *out, int max);
/* the entry's data (malloc'd, CRC checked); NULL if it cannot be read */
uint8_t *zip_extract(const uint8_t *zip, size_t size, const zip_entry *e);

/* raw deflate stream to a buffer of exactly outn bytes; 1 if it decoded cleanly */
int inflate_raw(const uint8_t *in, size_t inn, uint8_t *out, size_t outn);

#endif
