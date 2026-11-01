#ifndef CL_FONT_H
#define CL_FONT_H

#include <stdint.h>

#define FONT_W 6     /* advance, 5 + 1 spacing */
#define FONT_H 9     /* line height, 7 + 2 */

/* draw text, returns the x after the last glyph; colours are 0xAARRGGBB */
int font_draw(uint32_t *buf, int stride, int w, int h, int x, int y, const char *s, uint32_t color, int scale);
void fill_rect(uint32_t *buf, int stride, int w, int h, int x, int y, int rw, int rh, uint32_t color);

#endif
