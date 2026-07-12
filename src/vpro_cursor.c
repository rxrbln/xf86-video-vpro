/*
 * vpro_cursor.c -- hardware cursor for the SGI Odyssey (VPro).
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * The Odyssey has a 32x32 hardware cursor in the display back end (DBE),
 * programmed through the DFIFO like the rest of the DBE.  It consists of:
 *   - a 2-bit-per-pixel glyph plane (index into the cursor colour LUT),
 *   - a 1-bit-per-pixel alpha plane (coverage / transparency),
 *   - a 15-entry colour LUT,
 *   - a position register and an enable/control register.
 *
 * The DBE register addresses and the glyph/alpha/LUT packing were recovered
 * from the IP30 fprom (odsy_init_cursor and the cursor position routine) and
 * cross-checked against the IRIX standalone odsy_init_cursor() in
 * odsy_tport.c.  See vpro_regs.h for the register map.
 *
 * We map an X two-colour cursor (source + mask bitmaps) onto the hardware:
 *   alpha  = mask                      (visible where the mask bit is set)
 *   glyph  = source ? 1 : 2            (LUT index 1 = fg, 2 = bg)
 *   LUT[1] = foreground, LUT[2] = background
 * which mirrors the impact driver's cursor colour assignment.
 *
 * This file is subject to the same license as the rest of the driver.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vpro.h"
#include "cursorstr.h"
#include "servermd.h"

static void VproShowCursor(ScrnInfoPtr pScrn);
static void VproHideCursor(ScrnInfoPtr pScrn);
static void VproSetCursorPosition(ScrnInfoPtr pScrn, int x, int y);
static void VproSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg);
static void VproLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *bits);
static unsigned char *VproRealizeCursor(xf86CursorInfoPtr infoPtr,
					CursorPtr pCurs);

/* ------------------------- DFIFO table upload ---------------------- */

/*
 * Push `count` words to DBE register `reg` as a multi-word packet, then
 * stream the payload as 64-bit DFIFO pairs.  Follows the packet framing of
 * the IRIX odsy_init_cursor(): an even count uses a pair of packet headers
 * (count 0, then count N); an odd count packs the first payload word with
 * the header.
 */
static void
vpro_cursor_table(unsigned long mmio, CARD32 reg, const CARD32 *data,
		  int count)
{
	int i;

	vpro_wait_dfifo(mmio, 0);

	if (count & 1) {
		VPRO_DFIFO_D(mmio) =
			((CARD64)VPRO_DBE_HDR(reg, count) << 32) | data[0];
		i = 1;
	} else {
		VPRO_DFIFO_D(mmio) =
			((CARD64)VPRO_DBE_HDR(reg, 0) << 32) |
			VPRO_DBE_HDR(reg, count);
		i = 0;
	}

	for (; i + 1 < count; i += 2) {
		vpro_wait_dfifo(mmio, 0);
		VPRO_DFIFO_D(mmio) = ((CARD64)data[i] << 32) | data[i + 1];
	}
}

/* ------------------------- X cursor interface ---------------------- */

Bool
VproHWCursorInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	VproPtr pVpro = VPROPTR(pScrn);
	xf86CursorInfoPtr infoPtr;

	infoPtr = xf86CreateCursorInfoRec();
	if (!infoPtr)
		return FALSE;

	pVpro->CursorInfoRec = infoPtr;
	infoPtr->MaxWidth = VPRO_CURSOR_SIZE;
	infoPtr->MaxHeight = VPRO_CURSOR_SIZE;
	infoPtr->Flags = HARDWARE_CURSOR_TRUECOLOR_AT_8BPP |
			 HARDWARE_CURSOR_SOURCE_MASK_INTERLEAVE_1;

	infoPtr->SetCursorColors = VproSetCursorColors;
	infoPtr->SetCursorPosition = VproSetCursorPosition;
	infoPtr->LoadCursorImage = VproLoadCursorImage;
	infoPtr->HideCursor = VproHideCursor;
	infoPtr->ShowCursor = VproShowCursor;
	infoPtr->RealizeCursor = VproRealizeCursor;
	infoPtr->UseHWCursor = NULL;

	return xf86InitCursor(pScreen, infoPtr);
}

static void
VproShowCursor(ScrnInfoPtr pScrn)
{
	VproPtr pVpro = VPROPTR(pScrn);

	vpro_wait_dfifo(pVpro->mmio, 0);
	vpro_dfifo_write(pVpro->mmio, VPRO_DBE_CURSOR_CONTROL,
			 VPRO_CURSOR_CONTROL_ON);
}

static void
VproHideCursor(ScrnInfoPtr pScrn)
{
	VproPtr pVpro = VPROPTR(pScrn);

	vpro_wait_dfifo(pVpro->mmio, 0);
	vpro_dfifo_write(pVpro->mmio, VPRO_DBE_CURSOR_CONTROL,
			 VPRO_CURSOR_CONTROL_OFF);
}

static void
VproSetCursorPosition(ScrnInfoPtr pScrn, int x, int y)
{
	VproPtr pVpro = VPROPTR(pScrn);

	/* cursor_xy: x in the high 16 bits, y in the low 16 bits. */
	vpro_wait_dfifo(pVpro->mmio, 0);
	vpro_dfifo_write(pVpro->mmio, VPRO_DBE_CURSOR_XY,
			 ((x & 0xffff) << 16) | (y & 0xffff));
}

/*
 * Build a cursor LUT entry from a packed 0xRRGGBB colour.  The hardware
 * entry is (r << 22) | (g << 12) | (b << 2) (IRIX ARRAY_TO_ENTRY).
 */
static __inline__ CARD32
vpro_cursor_lut_entry(int rgb)
{
	CARD32 r = (rgb >> 16) & 0xff;
	CARD32 g = (rgb >> 8) & 0xff;
	CARD32 b = (rgb >> 0) & 0xff;

	return (r << 22) | (g << 12) | (b << 2);
}

static void
VproSetCursorColors(ScrnInfoPtr pScrn, int bg, int fg)
{
	VproPtr pVpro = VPROPTR(pScrn);
	CARD32 lut[VPRO_CURSOR_LUT_ENTRIES];
	int i;

	for (i = 0; i < VPRO_CURSOR_LUT_ENTRIES; i++)
		lut[i] = 0;
	lut[1] = vpro_cursor_lut_entry(fg);	/* glyph index 1 = foreground */
	lut[2] = vpro_cursor_lut_entry(bg);	/* glyph index 2 = background */

	vpro_cursor_table(pVpro->mmio, VPRO_DBE_CURSOR_LUT, lut,
			  VPRO_CURSOR_LUT_ENTRIES);
}

/*
 * Read the bit for pixel (x,y) from an X cursor bitmap (source or mask).
 */
static __inline__ int
vpro_bitmap_bit(const unsigned char *base, int stride, int x, int y)
{
	const unsigned char *b = base + y * stride + (x >> 3);

	return (*b >> (7 - (x & 7))) & 1;
}

/*
 * Convert an X source/mask cursor into the hardware glyph + alpha planes.
 * The returned blob is [ glyph (128 words) | alpha (64 words) ]; it is handed
 * back to us by the server in VproLoadCursorImage().
 *
 * glyph = 4 bits/pixel (8 pixels per word, 4 words per row): LUT index
 *         1 = foreground, 2 = background, 0 = transparent.
 * alpha = 2 bits/pixel (16 pixels per word, 2 words per row): 3 = opaque,
 *         0 = transparent.
 * Pixel 0 (leftmost) occupies the least-significant field of each word,
 * matching the on-wire arrow data in the IRIX odsy_init_cursor().
 */
static unsigned char *
VproRealizeCursor(xf86CursorInfoPtr infoPtr, CursorPtr pCurs)
{
	CARD32 *mem, *glyph, *alpha;
	const unsigned char *src, *msk;
	int y, x, w, h, stride;

	mem = calloc(VPRO_CURSOR_GLYPH_WORDS + VPRO_CURSOR_ALPHA_WORDS,
		     sizeof(CARD32));
	if (!mem)
		return NULL;

	glyph = mem;
	alpha = mem + VPRO_CURSOR_GLYPH_WORDS;

	src = (const unsigned char *)pCurs->bits->source;
	msk = (const unsigned char *)pCurs->bits->mask;

	w = pCurs->bits->width;
	h = pCurs->bits->height;
	if (w > VPRO_CURSOR_SIZE)
		w = VPRO_CURSOR_SIZE;
	if (h > VPRO_CURSOR_SIZE)
		h = VPRO_CURSOR_SIZE;
	stride = BitmapBytePad(pCurs->bits->width);

	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			CARD32 idx;

			if (!vpro_bitmap_bit(msk, stride, x, y))
				continue;	/* transparent */

			/* alpha: 2bpp, 16 px/word -> opaque (3). */
			alpha[y * 2 + (x >> 4)] |=
				(CARD32)3 << ((x & 15) * 2);

			/* glyph: 4bpp, 8 px/word -> fg (1) or bg (2). */
			idx = vpro_bitmap_bit(src, stride, x, y) ? 1 : 2;
			glyph[y * 4 + (x >> 3)] |= idx << ((x & 7) * 4);
		}
	}

	return (unsigned char *)mem;
}

static void
VproLoadCursorImage(ScrnInfoPtr pScrn, unsigned char *bits)
{
	VproPtr pVpro = VPROPTR(pScrn);
	const CARD32 *glyph = (const CARD32 *)bits;
	const CARD32 *alpha = glyph + VPRO_CURSOR_GLYPH_WORDS;

	vpro_cursor_table(pVpro->mmio, VPRO_DBE_CURSOR_GLYPH, glyph,
			  VPRO_CURSOR_GLYPH_WORDS);
	vpro_cursor_table(pVpro->mmio, VPRO_DBE_CURSOR_ALPHA, alpha,
			  VPRO_CURSOR_ALPHA_WORDS);
}
