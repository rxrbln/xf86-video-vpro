/*
 * vpro_accel.c -- Odyssey/VPro command-FIFO rendering engine.
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * Ported from the Linux fbdev driver drivers/video/fbdev/odyssey.c and
 * odyssey_early.c (c) Stanislaw Skowronek, Johannes Dickgreber,
 * Joshua Kinard, René Rebe, and cross-checked against the IRIX PROM
 * textport code (odsy_tport.c).
 *
 * The Odyssey has no linear framebuffer.  Everything is drawn by feeding
 * GL-like command tokens into the command FIFO (CFIFO) at MMIO+0x110000
 * and register writes into the data FIFO (DFIFO) at MMIO+0x400000.
 *
 * This file is subject to the same license as the rest of the driver.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vpro.h"

/*
 * Pack a signed integer into the 32-bit IEEE-754 float representation the
 * raster front-end expects for glDrawPixels raster positions.  Verbatim
 * from odyssey.c:pack_ieee754().
 */
static CARD32
vpro_pack_ieee754(int val)
{
	CARD32 sign, exp;

	if (!val)
		return 0;

	sign = (val & 0x80000000);
	if (sign)
		val = -val;
	if (val & 0xff000000)
		return 0;

	exp = 150;
	while (!(val & 0x00800000)) {
		val <<= 1;
		exp--;
	}

	return (sign | (exp << 23) | (val & 0x007fffff));
}

/* ------------------------- pipeline flushes ------------------------- */

void
vpro_flush(unsigned long mmio)
{
	vpro_wait_cfifo(mmio);
	VPRO_CFIFO_W(mmio) = 0x00010443;
	VPRO_CFIFO_W(mmio) = 0x000000fa;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010019;
	VPRO_CFIFO_W(mmio) = 0x00010443;
	VPRO_CFIFO_W(mmio) = 0x00000096;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010443;
	VPRO_CFIFO_W(mmio) = 0x000000fa;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010046;
}

void
vpro_smallflush(unsigned long mmio)
{
	vpro_wait_cfifo(mmio);
	VPRO_CFIFO_W(mmio) = 0x00010443;
	VPRO_CFIFO_W(mmio) = 0x000000fa;
	VPRO_CFIFO_W(mmio) = 0x00010046;
	VPRO_CFIFO_W(mmio) = 0x00010046;
}

/* --------------------- one-time hardware bringup -------------------- */

static void
vpro_initbuzzgfe(unsigned long mmio)
{
	VPRO_CFIFO_W(mmio) = 0x20008003;
	VPRO_CFIFO_W(mmio) = 0x21008010;
	VPRO_CFIFO_W(mmio) = 0x22008000;
	VPRO_CFIFO_W(mmio) = 0x23008002;
	VPRO_CFIFO_W(mmio) = 0x2400800c;
	VPRO_CFIFO_W(mmio) = 0x2500800e;
	VPRO_CFIFO_W(mmio) = 0x27008000;
	VPRO_CFIFO_W(mmio) = 0x28008000;
	VPRO_CFIFO_W(mmio) = 0x290080d6;
	VPRO_CFIFO_W(mmio) = 0x2a0080e0;
	VPRO_CFIFO_W(mmio) = 0x2c0080ea;
	VPRO_CFIFO_W(mmio) = 0x2e008380;
	VPRO_CFIFO_W(mmio) = 0x2f008000;
	VPRO_CFIFO_W(mmio) = 0x30008000;
	VPRO_CFIFO_W(mmio) = 0x31008000;
	VPRO_CFIFO_W(mmio) = 0x32008000;
	VPRO_CFIFO_W(mmio) = 0x33008000;
	VPRO_CFIFO_W(mmio) = 0x34008000;
	VPRO_CFIFO_W(mmio) = 0x35008000;
	VPRO_CFIFO_W(mmio) = 0x310081e0;
	vpro_flush(mmio);
}

static void
vpro_initbuzzxform(unsigned long mmio)
{
	VPRO_CFIFO_W(mmio) = 0x9080bda2;
	VPRO_CFIFO_W(mmio) = 0x3f800000;
	VPRO_CFIFO_W(mmio) = 0x3f000000;
	VPRO_CFIFO_W(mmio) = 0xbf800000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x4e000000;
	VPRO_CFIFO_W(mmio) = 0x40400000;
	VPRO_CFIFO_W(mmio) = 0x4e000000;
	VPRO_CFIFO_W(mmio) = 0x4d000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x34008000;
	VPRO_CFIFO_W(mmio) = 0x9080bdc8;
	VPRO_CFIFO_W(mmio) = 0x3f800000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x3f000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x3f800000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x3f000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x3f800000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x3f800000;
	VPRO_CFIFO_W(mmio) = 0x34008010;
	VPRO_CFIFO_W(mmio) = 0x908091df;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x3f800000;
	VPRO_CFIFO_W(mmio) = 0x34008000;
	vpro_flush(mmio);
}

static void
vpro_initbuzzrast(unsigned long mmio)
{
	VPRO_CFIFO_W(mmio) = 0x0001203b;
	VPRO_CFIFO_W(mmio) = 0x00001000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00001000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00001000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00001000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x0001084a;
	VPRO_CFIFO_W(mmio) = 0x00000080;
	VPRO_CFIFO_W(mmio) = 0x00000080;
	VPRO_CFIFO_W(mmio) = 0x00010845;
	VPRO_CFIFO_W(mmio) = 0x000000ff;
	VPRO_CFIFO_W(mmio) = 0x000076ff;
	VPRO_CFIFO_W(mmio) = 0x0001141b;
	VPRO_CFIFO_W(mmio) = 0x00000001;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00011c16;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x03000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00010404;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00011023;
	VPRO_CFIFO_W(mmio) = 0x00ff0ff0;
	VPRO_CFIFO_W(mmio) = 0x00ff0ff0;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x000000ff;
	VPRO_CFIFO_W(mmio) = 0x00011017;
	VPRO_CFIFO_W(mmio) = 0x00002000;
	VPRO_CFIFO_W(mmio) = 0x00000050;
	VPRO_CFIFO_W(mmio) = 0x20004950;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x0001204b;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x004ff3ff;
	VPRO_CFIFO_W(mmio) = 0x00ffffff;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00ffffff;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00ffffff;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	vpro_flush(mmio);
}

static void
vpro_initpbjvc(unsigned long mmio)
{
	int x;

	vpro_wait_dfifo(mmio, 0);
	for (x = 0; x < 16; x++)
		vpro_dfifo_write(mmio, (0x2900 | x), 0x905215a6);

	vpro_wait_dfifo(mmio, 0);
	for (x = 16; x < 32; x++)
		vpro_dfifo_write(mmio, (0x2900 | x), 0x905215a6);

	vpro_wait_dfifo(mmio, 0);
	vpro_dfifo_write(mmio, 0x2581, 0x00000000);
}

static void
vpro_initpbjgamma(unsigned long mmio)
{
	CARD32 i, v;

	for (i = 0; i < 0x200; i++) {
		if ((i & 15) == 0)
			vpro_wait_dfifo(mmio, 0);
		v = (i >> 2);
		v = ((v << 20) | (v << 10) | v);
		vpro_dfifo_write(mmio, (i + 0x1a00), v);
	}

	for (i = 0x200; i < 0x300; i++) {
		if ((i & 15) == 0)
			vpro_wait_dfifo(mmio, 0);
		v = (((i - 0x200) >> 1) + 0x80);
		v = ((v << 20) | (v << 10) | v);
		vpro_dfifo_write(mmio, (i + 0x1a00), v);
	}

	for (i = 0x300; i < 0x600; i++) {
		if ((i & 15) == 0)
			vpro_wait_dfifo(mmio, 0);
		v = ((i - 0x300) + 0x100);
		v = ((v << 20) | (v << 10) | v);
		vpro_dfifo_write(mmio, (i + 0x1a00), v);
	}
}

void
vpro_hwinit(unsigned long mmio)
{
	vpro_initbuzzgfe(mmio);
	vpro_initbuzzxform(mmio);
	vpro_initbuzzrast(mmio);
	vpro_initpbjvc(mmio);
	vpro_initpbjgamma(mmio);
}

/* ------------------------- solid primitives ------------------------ */

/*
 * Emit a glBegin(mode) ... glColor3ub ... vertices ... glEnd batch with an
 * optional non-COPY logic op wrapped around it.
 */
static void
vpro_set_logicop(unsigned long mmio, int logicop)
{
	VPRO_CFIFO_W(mmio) = 0x00010404;
	VPRO_CFIFO_W(mmio) = 0x00100000;
	VPRO_CFIFO_W(mmio) = VPRO_TOK_LOGICOP;
	VPRO_CFIFO_W(mmio) = logicop;
	vpro_smallflush(mmio);
}

static void
vpro_clear_logicop(unsigned long mmio)
{
	VPRO_CFIFO_W(mmio) = 0x00010404;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = VPRO_TOK_LOGICOP;
	VPRO_CFIFO_W(mmio) = VPRO_LO_COPY;
	vpro_smallflush(mmio);
}

static __inline__ void
vpro_color3ub(unsigned long mmio, CARD32 c)
{
	VPRO_CFIFO_W(mmio) = VPRO_TOK_COLOR3UB;
	VPRO_CFIFO_W(mmio) = (c & 255);
	VPRO_CFIFO_W(mmio) = ((c >> 8) & 255);
	VPRO_CFIFO_W(mmio) = ((c >> 16) & 255);
}

static __inline__ void
vpro_vertex2i(unsigned long mmio, int x, int y)
{
	VPRO_CFIFO_W(mmio) = VPRO_TOK_VERTEX2I;
	VPRO_CFIFO_W(mmio) = x;
	VPRO_CFIFO_W(mmio) = y;
}

/*
 * Solid filled rectangle at (x,y) size (w,h) in colour c (0x00BBGGRR),
 * drawn as a GL_QUADS.  Port of odyssey_rect().
 */
void
vpro_rect(unsigned long mmio, int x, int y, int w, int h,
	  CARD32 c, int logicop)
{
	if (logicop != VPRO_LO_COPY)
		vpro_set_logicop(mmio, logicop);

	VPRO_CFIFO_W(mmio) = VPRO_TOK_BEGIN;
	VPRO_CFIFO_W(mmio) = VPRO_PRIM_QUADS;
	vpro_color3ub(mmio, c);
	vpro_vertex2i(mmio, x,     y);
	vpro_vertex2i(mmio, x + w, y);
	vpro_vertex2i(mmio, x + w, y + h);
	vpro_vertex2i(mmio, x,     y + h);
	VPRO_CFIFO_W(mmio) = VPRO_TOK_END;
	vpro_smallflush(mmio);

	if (logicop != VPRO_LO_COPY)
		vpro_clear_logicop(mmio);
}

/*
 * Solid line from (x1,y1) to (x2,y2) in colour c, drawn as a GL_LINES
 * segment.  The IRIX textport (odsy_sboxfi) uses the same vertex token
 * stream with BEGIN_PRIM_LINES for its one-pixel-wide rules.
 */
void
vpro_line(unsigned long mmio, int x1, int y1, int x2, int y2,
	  CARD32 c, int logicop)
{
	if (logicop != VPRO_LO_COPY)
		vpro_set_logicop(mmio, logicop);

	VPRO_CFIFO_W(mmio) = VPRO_TOK_BEGIN;
	VPRO_CFIFO_W(mmio) = VPRO_PRIM_LINES;
	vpro_color3ub(mmio, c);
	vpro_vertex2i(mmio, x1, y1);
	vpro_vertex2i(mmio, x2, y2);
	VPRO_CFIFO_W(mmio) = VPRO_TOK_END;
	vpro_smallflush(mmio);

	if (logicop != VPRO_LO_COPY)
		vpro_clear_logicop(mmio);
}

/* ----------------- pixel upload (host -> VRAM) --------------------- */

/*
 * Upload w*h 32bpp pixels from host memory into the framebuffer, starting
 * at screen position (x,y).  This is the glDrawPixels-style PIO path and
 * is how the ShadowFB refresh copies pixel data into the card's VRAM.
 *
 * Ported from odyssey_imageblit_8bpp(), but the source is already true
 * colour so each pixel word is sent directly instead of via a palette.
 * Pixels are streamed in runs of at most 14 per 0x00014011 token.
 */
void
vpro_drawpixels(unsigned long mmio, int x, int y, int w, int h,
		const CARD32 *src, int pitch)
{
	int i, j, l;
	const CARD32 *row;

	/* set up the raster rectangle for the PIO blit */
	vpro_smallflush(mmio);
	VPRO_CFIFO_W(mmio) = 0x00010405;
	VPRO_CFIFO_W(mmio) = 0x00002400;
	VPRO_CFIFO_W(mmio) = VPRO_TOK_COLOR3UB;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00011453;
	VPRO_CFIFO_W(mmio) = 0x00000002;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	vpro_flush(mmio);
	VPRO_CFIFO_W(mmio) = 0x2900812f;
	VPRO_CFIFO_W(mmio) = VPRO_TOK_BEGIN;
	VPRO_CFIFO_W(mmio) = 0x0000000a;
	VPRO_CFIFO_W(mmio) = 0xcf80a92f;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = vpro_pack_ieee754(x);
	VPRO_CFIFO_W(mmio) = vpro_pack_ieee754(y);
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = vpro_pack_ieee754(x + w);
	VPRO_CFIFO_W(mmio) = vpro_pack_ieee754(y + h);
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = VPRO_TOK_VERTEX2I;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00004570;
	VPRO_CFIFO_W(mmio) = 0x0f00104c;
	VPRO_CFIFO_W(mmio) = 0x00000071;

	for (j = 0; j < h; j++) {
		VPRO_CFIFO_W(mmio) = 0x00004570;
		VPRO_CFIFO_W(mmio) = 0x0fd1104c;
		VPRO_CFIFO_W(mmio) = 0x00000071;
		row = (const CARD32 *)((const char *)src + (long)j * pitch);
		i = w;
		while (i > 0) {
			l = ((i > 14) ? 14 : i);
			i -= l;
			VPRO_CFIFO_W(mmio) = (0x00014011 | (l << 10));
			while (l--)
				VPRO_CFIFO_W(mmio) = *(row++);
		}
	}

	VPRO_CFIFO_W(mmio) = VPRO_TOK_END;
	vpro_smallflush(mmio);
	VPRO_CFIFO_W(mmio) = 0x290080d6;
	VPRO_CFIFO_W(mmio) = 0x00011453;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00000000;
	VPRO_CFIFO_W(mmio) = 0x00010405;
	VPRO_CFIFO_W(mmio) = 0x00002000;
	vpro_flush(mmio);
}

/* ------------------- screen-to-screen copy ------------------------- */

/*
 * Screen-to-screen block copy (glCopyPixels-style).  Ported from
 * odyssey_copyarea().  Note: with the ShadowFB design copies are resolved
 * in host memory by fb.c and re-uploaded, so this is provided mainly for
 * completeness / future XAA use.
 */
void
vpro_copyarea(unsigned long mmio, int sx, int sy, int dx, int dy,
	      int w, int h)
{
	if (w < 1 || h < 1)
		return;

	vpro_flush(mmio);
	VPRO_CFIFO_W(mmio) = 0x00010658;
	VPRO_CFIFO_W(mmio) = 0x00120000;
	VPRO_CFIFO_W(mmio) = 0x00002031;
	VPRO_CFIFO_W(mmio) = 0x00002000;
	VPRO_CFIFO_W(mmio) = (sx | (sy << 16));		/* source origin */
	VPRO_CFIFO_W(mmio) = 0x80502050;
	VPRO_CFIFO_W(mmio) = (w | (h << 16));		/* size          */
	VPRO_CFIFO_W(mmio) = 0x82223042;
	VPRO_CFIFO_W(mmio) = 0x00002000;
	VPRO_CFIFO_W(mmio) = (dx | (dy << 16));		/* dest origin   */
	VPRO_CFIFO_W(mmio) = 0x3222204b;
	vpro_flush(mmio);
}

/* --------------------- ShadowFB refresh ---------------------------- */

/*
 * Push the dirty boxes reported by ShadowFB into the card.  Each box is a
 * rectangle of the host shadow buffer; we upload it with vpro_drawpixels.
 */
void
VproRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox)
{
	VproPtr pVpro = VPROPTR(pScrn);
	unsigned long mmio = pVpro->mmio;
	unsigned long pitch = pVpro->ShadowPitch;

	for (; num-- > 0; pbox++) {
		int x1 = pbox->x1, y1 = pbox->y1;
		int x2 = pbox->x2, y2 = pbox->y2;
		const CARD32 *src;

		/* Box is the half-open range [x1,x2) x [y1,y2). */
		if (x1 < 0) x1 = 0;
		if (y1 < 0) y1 = 0;
		if (x2 > VPRO_FIXED_W_SCRN) x2 = VPRO_FIXED_W_SCRN;
		if (y2 > VPRO_FIXED_H_SCRN) y2 = VPRO_FIXED_H_SCRN;
		if (x2 <= x1 || y2 <= y1)
			continue;

		src = (const CARD32 *)((char *)pVpro->ShadowPtr +
				       (long)y1 * pitch + (long)x1 * 4);

		vpro_drawpixels(mmio, x1, y1, x2 - x1, y2 - y1, src, pitch);
	}
}
