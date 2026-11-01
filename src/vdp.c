/* Casiolator - Casio Loopy VDP (RH-7500) rendering, after kasami's Hardware Megadoc
 *
 * Each line: the eight layers (BG0 BG1 BM0-3 OBJ0 OBJ1) are drawn into line buffers of
 * palette indices with the screen (A, B) each pixel goes to; each screen takes the front
 * opaque pixel in the priority order, or its backdrop; then the screens are blended. */
#include <string.h>
#include "loopy.h"

enum { L_OBJ0, L_OBJ1, L_BG0, L_BG1, L_BM0, L_BM1, L_BM2, L_BM3, L_COUNT };

/* screen bits of a pixel */
#define SA 1
#define SB 2

typedef struct
{
	uint8_t idx[LOOPY_W];       /* palette index, 0 = transparent */
	uint8_t scr[LOOPY_W];       /* SA / SB, 0x80: the colour is in direct[] */
	uint16_t direct[LOOPY_W];   /* a bitmap's latched colour */
} linebuf;

uint32_t vdp_rgb(uint16_t c)
{
	const uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
	return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

static uint16_t pal16(const vdp *v, int i)
{
	return (uint16_t)((v->pal[i * 2] << 8) | v->pal[i * 2 + 1]);
}

/* a layer's screen-control field (upper bit A, lower bit B) as SA/SB */
static int screens(int field)
{
	return ((field & 2) ? SA : 0) | ((field & 1) ? SB : 0);
}

/* ---- characters ---- */

static const uint16_t s_char_base[8] = { 0x4000, 0x2000, 0x2000, 0x1000, 0x2000, 0x1000, 0x1000, 0x0800 };

/* pixel (x, y) of the tile/object made of characters starting at chr, in a grid 8 wide */
static int char_pixel(const vdp *v, unsigned chr, int x, int y, int bpp8)
{
	const unsigned base = s_char_base[v->reg[V_BG_CTRL] & 7];
	const unsigned cx = (chr & 7) + (x >> 3), cy = (chr >> 3) + (y >> 3);
	const unsigned n = (cy << 3) | (cx & 7);
	const int px = x & 7, py = y & 7;
	if (bpp8)
		return v->tile[(base + n * 64 + py * 8 + px) & 0xffff];
	const unsigned split = (v->reg[V_CHAR_SPLIT] & 0x7f) * 512;
	const uint8_t b = v->tile[(base + split + n * 32 + py * 4 + (px >> 1)) & 0xffff];
	return (px & 1) ? (b & 15) : (b >> 4);
}

/* ---- backgrounds ---- */

static void draw_bg(const vdp *v, int layer, int y, linebuf *lb)
{
	const uint16_t ctrl = v->reg[V_BG_CTRL];
	const int lay = ctrl & 7;
	const int mw = (lay < 4) ? 64 : 32, mh = (lay == 0 || lay == 1 || lay == 4 || lay == 5) ? 64 : 32;
	const int shared = lay & 1;
	const unsigned map = (layer == 1 && !shared) ? (unsigned)mw * mh * 2 : 0;
	const int tsz = layer == 0 ? (ctrl >> 6) & 3 : (ctrl >> 4) & 3;
	const int ts = 8 << tsz;
	const int bpp8 = layer == 0 && (ctrl & 8);
	const uint16_t subpal = v->reg[V_BG_SUBPAL + layer];
	const int sx = v->reg[V_BG_SCROLL + layer * 2] & 0xfff, sy = v->reg[V_BG_SCROLL + layer * 2 + 1] & 0xfff;
	const int wpx = mw * ts, hpx = mh * ts;
	const int py = (sy + y) % hpx;

	for (int x = 0; x < LOOPY_W; x++)
	{
		const int px = (sx + x) % wpx;
		const unsigned cell = map + (unsigned)((py / ts) * mw + px / ts) * 2;
		const uint16_t e = (uint16_t)((v->tile[cell & 0xffff] << 8) | v->tile[(cell + 1) & 0xffff]);
		int tx = px % ts, ty = py % ts;
		if (e & 0x4000) tx = ts - 1 - tx;
		if (e & 0x8000) ty = ts - 1 - ty;
		const int p = char_pixel(v, e & 0x7ff, tx, ty, bpp8);
		if (!p)
			continue;
		lb->idx[x] = bpp8 ? (uint8_t)p : (uint8_t)((((subpal >> (((e >> 12) & 3) * 4)) & 15) << 4) | p);
		lb->scr[x] = (e & 0x0800) ? SB : SA;
	}
}

/* ---- objects ---- */

static void draw_obj(const vdp *v, int y, linebuf *o0, linebuf *o1)
{
	static const uint8_t wch[4] = { 1, 2, 2, 4 }, hch[4] = { 1, 2, 4, 4 };
	const uint16_t ctrl = v->reg[V_OBJ_CTRL];
	const int split = ctrl & 0xff;
	const uint16_t lctrl = v->reg[V_LAYER_CTRL];
	const int scr0 = screens((lctrl >> 12) & 3), scr1 = screens((lctrl >> 14) & 3);
	const int en0 = (lctrl & 0x40) && scr0, en1 = (lctrl & 0x80) && scr1;

	/* lower OAM entries in front: draw from the back so they win */
	for (int i = 127; i >= 0; i--)
	{
		const uint8_t *e = v->oam + i * 4;
		const int layer = ((uint8_t)(i - split) & 0x80) ? 1 : 0;
		if (layer ? !en1 : !en0)
			continue;
		const uint16_t attr = (uint16_t)((e[2] << 8) | e[3]);
		const int size = (attr >> 10) & 3;
		const int w = wch[size] * 8, h = hch[size] * 8;
		int oy = e[1] | ((attr & 0x200) ? 0x100 : 0);
		int ox = attr & 0x1ff;
		if (oy & 0x100) oy -= 0x200;
		if (ox & 0x100) ox -= 0x200;
		int ty = y - oy;
		if (ty < 0 || ty >= h)
			continue;
		if (attr & 0x8000)
			ty = h - 1 - ty;
		const int bpp8 = layer == 0 && (ctrl & 0x4000);
		const unsigned th = layer == 0 ? (ctrl >> 11) & 7 : (ctrl >> 8) & 7;
		const unsigned chr = (th << 8) | e[0];
		const uint16_t subpal = v->reg[V_OBJ_SUBPAL + layer];
		const int sp = ((subpal >> (((attr >> 12) & 3) * 4)) & 15) << 4;
		linebuf *lb = layer ? o1 : o0;
		for (int tx = 0; tx < w; tx++)
		{
			const int x = ox + tx;
			if (x < 0 || x >= LOOPY_W)
				continue;
			const int p = char_pixel(v, chr, (attr & 0x4000) ? w - 1 - tx : tx, ty, bpp8);
			if (!p)
				continue;
			lb->idx[x] = bpp8 ? (uint8_t)p : (uint8_t)(sp | p);
			lb->scr[x] = (uint8_t)(layer ? scr1 : scr0);
		}
	}
}

/* ---- bitmaps ---- */

static void draw_bm(vdp *v, int n, int y, linebuf *lb)
{
	const int mode = v->reg[V_BM_CTRL] & 7;
	const int bpp4 = mode >= 2 && mode <= 4;
	int posy = v->reg[V_BM_POSY + n] & 0x1ff, posx = v->reg[V_BM_POSX + n] & 0x1ff;
	if (posy & 0x100) posy -= 0x200;
	if (posx & 0x100) posx -= 0x200;
	const int ly = y - posy;
	if (ly < 0 || ly > (v->reg[V_BM_HEIGHT + n] & 0xff))
		return;
	const int startx = v->reg[V_BM_WIDTH + n] >> 8, endx = v->reg[V_BM_WIDTH + n] & 0xff;
	const int sx = v->reg[V_BM_SCROLLX + n] & 0x1ff;
	int vy = (v->reg[V_BM_SCROLLY + n] + ly) & 0x1ff;
	int area_x = 0;
	switch (mode)
	{
	case 0: case 2: case 5:
		vy = (v->reg[V_BM_SCROLLY + n] & 0x100) | ((v->reg[V_BM_SCROLLY + n] + ly) & 0xff);
		break;
	case 3:
		area_x = sx & 0x100;
		break;
	}
	const uint8_t *row = v->bitmap + (unsigned)vy * 256;
	const int sp = bpp4 ? ((v->reg[V_BM_SUBPAL] >> ((3 - n) * 4)) & 15) << 4 : 0;
	const uint16_t latch_ctl = v->reg[V_BM_LATCH + n];
	const int latch_en = latch_ctl & 0x100;
	const int thrs = bpp4 ? latch_ctl & 15 : latch_ctl & 0xff;
	const int top = bpp4 ? 15 : 255;
	uint8_t buf[LOOPY_W];

	/* the layer's own 256-pixel buffer, from the scroll position */
	for (int i = 0; i < LOOPY_W; i++)
	{
		int p;
		if (bpp4)
		{
			const int vx = mode == 3 ? area_x | ((sx + i) & 0xff) : (sx + i) & 0x1ff;
			const uint8_t b = row[vx >> 1];
			p = (vx & 1) ? (b & 15) : (b >> 4);
		}
		else
			p = row[(sx + i) & 0xff];
		buf[i] = (uint8_t)p;
	}
	const int scr = screens(n < 2 ? (v->reg[V_LAYER_CTRL] >> 8) & 3 : (v->reg[V_LAYER_CTRL] >> 10) & 3);
	uint16_t latch = v->bm_latch_color[n];
	for (int i = 0; i <= endx && i < LOOPY_W; i++)
	{
		int p = buf[i];
		int use_latch = 0;
		if (latch_en)
		{
			if (p < thrs && p)
				latch = pal16(v, bpp4 ? sp | p : p);
			else if (p == top && i != 255)
				use_latch = 1;
		}
		const int x = posx + i;
		if (i < startx || x < 0 || x >= LOOPY_W || !p)
			continue;
		if (use_latch)
		{
			lb->idx[x] = 0xff;
			lb->scr[x] = (uint8_t)(scr | 0x80);
			lb->direct[x] = latch;
			continue;
		}
		lb->idx[x] = bpp4 ? (uint8_t)(sp | p) : (uint8_t)p;
		lb->scr[x] = (uint8_t)scr;
	}
	v->bm_latch_color[n] = latch;
}

/* ---- composition ---- */

/* layer order front to back for each PRIO value (twelve slots, empty ones left out) */
static void prio_order(int prio, int *order)
{
	int slot[13];
	for (int i = 0; i < 13; i++)
		slot[i] = -1;
	static const int obj0_slot[4] = { 1, 6, 9, 12 };
	slot[obj0_slot[(prio >> 2) & 3]] = L_OBJ0;
	slot[2] = L_OBJ1;
	slot[(prio & 2) ? 3 : 10] = L_BG0;
	slot[11] = L_BG1;
	const int bm01 = (prio & 1) ? 7 : 4, bm23 = (prio & 1) ? 4 : 7;
	slot[bm01] = L_BM0;
	slot[bm01 + 1] = L_BM1;
	slot[bm23] = L_BM2;
	slot[bm23 + 1] = L_BM3;
	int k = 0;
	for (int i = 1; i <= 12; i++)
		if (slot[i] >= 0)
			order[k++] = slot[i];
}

static uint32_t add_rgb(uint16_t a, uint16_t b, int sub, int half)
{
	int ch[3];
	for (int i = 0; i < 3; i++)
	{
		const int ca = (a >> (10 - i * 5)) & 31, cb = (b >> (10 - i * 5)) & 31;
		int r = sub ? ca - cb : ca + cb;
		if (half)
			r >>= 1;
		ch[i] = r < 0 ? 0 : r > 31 ? 31 : r;
	}
	return vdp_rgb((uint16_t)(ch[0] << 10 | ch[1] << 5 | ch[2]));
}

void vdp_render_line(vdp *v, int y)
{
	if (y < 0 || y >= v->height || y >= LOOPY_H)
		return;
	static linebuf lb[L_COUNT];
	memset(lb, 0, sizeof(lb));
	const uint16_t lctrl = v->reg[V_LAYER_CTRL];
	if (lctrl & 0x01) draw_bg(v, 0, y, &lb[L_BG0]);
	if (lctrl & 0x02) draw_bg(v, 1, y, &lb[L_BG1]);
	for (int n = 0; n < 4; n++)
		if (lctrl & (4 << n))
			draw_bm(v, n, y, &lb[L_BM0 + n]);
	draw_obj(v, y, &lb[L_OBJ0], &lb[L_OBJ1]);

	const uint16_t sc = v->reg[V_SCREEN_CTRL];
	int order[8];
	prio_order(sc & 15, order);
	const uint16_t back_a = v->reg[V_BACKDROP_A] & 0x7fff, back_b = v->reg[V_BACKDROP_B] & 0x7fff;
	const int a_on = sc & 0x40, b_on = sc & 0x20, b_solid = sc & 0x10;
	const int blend = v->reg[V_BLEND] & 7, sub = sc & 0x80;
	uint32_t *out = v->fb + y * LOOPY_W;

	for (int x = 0; x < LOOPY_W; x++)
	{
		/* front pixel of each screen; -1 = backdrop */
		int ca = -1, cb = -1, la = -1, lbn = -1;
		for (int k = 0; k < 8 && (ca < 0 || cb < 0); k++)
		{
			const linebuf *b = &lb[order[k]];
			if (!b->idx[x])
				continue;
			if (ca < 0 && (b->scr[x] & SA)) { ca = b->idx[x]; la = order[k]; }
			if (cb < 0 && (b->scr[x] & SB)) { cb = b->idx[x]; lbn = order[k]; }
		}
		/* colours: a latched bitmap colour stands in for the index */
		uint16_t A = ca < 0 ? back_a : (lb[la].scr[x] & 0x80) ? lb[la].direct[x] : pal16(v, ca);
		uint16_t B = cb < 0 ? back_b : (lb[lbn].scr[x] & 0x80) ? lb[lbn].direct[x] : pal16(v, cb);
		if (b_solid)
		{
			B = back_b;
			cb = -1;
		}
		if (!a_on) A = 0;
		if (!b_on) B = 0;
		switch (blend)
		{
		case 0: out[x] = add_rgb(A, B, sub, 0); break;
		case 1: out[x] = add_rgb(A, B, sub, 1); break;
		case 3: out[x] = add_rgb(A, B, 0, 1); break;       /* hi-res: both halves averaged */
		case 4: out[x] = vdp_rgb((b_on && cb >= 0) ? B : A); break;
		case 5: out[x] = vdp_rgb((a_on && ca >= 0) ? A : B); break;
		default: out[x] = vdp_rgb(A); break;
		}
	}
}
