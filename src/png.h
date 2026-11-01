#ifndef CL_PNG_H
#define CL_PNG_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* CRC-32 as PNG and zip use it: start with c = 0 */
uint32_t crc32_update(uint32_t c, const uint8_t *p, size_t n);

/* pixels are 0x00RRGGBB, stride in pixels */
int png_write(const char *path, const uint32_t *pixels, int stride, int w, int h);

#endif
