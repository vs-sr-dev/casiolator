/* Casiolator - Casio Loopy machine internals shared by loopy.c and vdp.c */
#ifndef CL_LOOPY_H
#define CL_LOOPY_H

#include <stdint.h>
#include "sh1.h"
#include "sh7021.h"

#define LOOPY_W 256
#define LOOPY_H 240              /* the framebuffer holds the 240-line mode; 224 uses the top */

typedef struct vdp
{
	uint8_t bitmap[0x20000];
	uint8_t tile[0x10000];
	uint8_t oam[0x200];
	uint8_t pal[0x200];
	uint8_t capture[0x200];
	uint16_t reg[0x4000];        /* 0x0c058000-0x0c05ffff as words */
	uint16_t sync_shift, sound_ctrl;
	uint16_t latch;              /* last value on the bus: what unmapped addresses read */
	uint16_t bm_latch_color[4];  /* bitmap colour latch state */
	int line;                    /* frame line 0-262 being drawn */
	int height;                  /* 224 or 240 */
	uint32_t fb[LOOPY_W * LOOPY_H];
} vdp;

/* register offsets (from 0x0c058000), in words */
#define VR(off) (((off) - 0x58000) >> 1)
#define V_MODE        VR(0x58000)
#define V_TRIGGER     VR(0x58006)
#define V_SYNC_IRQ    VR(0x58008)
#define V_BM_SCROLLX  VR(0x59000)
#define V_BM_SCROLLY  VR(0x59008)
#define V_BM_POSX     VR(0x59010)
#define V_BM_POSY     VR(0x59018)
#define V_BM_WIDTH    VR(0x59020)
#define V_BM_HEIGHT   VR(0x59028)
#define V_BM_CTRL     VR(0x59030)
#define V_BM_SUBPAL   VR(0x59040)
#define V_BM_LATCH    VR(0x59050)
#define V_BG_CTRL     VR(0x5a000)
#define V_BG_SCROLL   VR(0x5a002)  /* BG0 X, BG0 Y, BG1 X, BG1 Y */
#define V_BG_SUBPAL   VR(0x5a00a)
#define V_OBJ_CTRL    VR(0x5a010)
#define V_OBJ_SUBPAL  VR(0x5a012)
#define V_CHAR_SPLIT  VR(0x5a020)
#define V_BLEND       VR(0x5b000)
#define V_LAYER_CTRL  VR(0x5b002)
#define V_SCREEN_CTRL VR(0x5b004)
#define V_BACKDROP_B  VR(0x5b006)
#define V_BACKDROP_A  VR(0x5b008)
#define V_CAPTURE_CTRL VR(0x5b00a)
#define V_IRQ_CTRL    VR(0x5c000)
#define V_IRQ_HCMP    VR(0x5c002)
#define V_IRQ_VCMP    VR(0x5c004)
#define V_PRINT_TEMP  VR(0x5d000)
#define V_CONTROL_IN  VR(0x5d010)
#define V_PRINT_STAT  VR(0x5d030)
#define V_PRINT_DATA  VR(0x5d040)
#define V_PRINT_MOTOR VR(0x5d042)
#define V_PRINT_HEAD  VR(0x5d044)
#define V_MOUSE       VR(0x5d050)
#define V_CONTROL_OUT VR(0x5d054)
#define V_BM_MEM_CTRL VR(0x5e000)
#define V_FILL_MASK   VR(0x5e002)
#define V_FILL_VALUE  VR(0x5e004)

/* draw frame line `line` (0-262) into the framebuffer if it is visible */
void vdp_render_line(vdp *v, int line);
/* RGB555 to 0x00RRGGBB */
uint32_t vdp_rgb(uint16_t c);

#endif
