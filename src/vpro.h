/*
 * vpro.h -- driver private data and prototypes for the
 *           xf86-video-vpro driver (SGI Odyssey / VPro).
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * Structured after the xf86-video-impact driver (c) 2005 peter fuerst,
 * itself derived from the newport driver (c) 2000,2001 Guido Guenther.
 */

#ifndef __VPRO_H__
#define __VPRO_H__

#include "xf86.h"
#include "xf86_OSproc.h"
#include "compiler.h"
#if GET_ABI_MAJOR(ABI_VIDEODRV_VERSION) < 6
#include "xf86Resources.h"
#endif

#include "xf86cmap.h"
#include "xf86Cursor.h"

#include "compat-api.h"
#include "vpro_regs.h"

#if 0
# define DEBUG 1
#endif

#ifdef DEBUG
# define TRACE_ENTER(str)	ErrorF("vpro: " str " %d\n", pScrn->scrnIndex)
# define TRACE_EXIT(str)	ErrorF("vpro: " str " done\n")
# define TRACE(str)		ErrorF("vpro trace: " str "\n")
# define TRACEV(str...)		ErrorF(str)
#else
# define TRACE_ENTER(str)
# define TRACE_EXIT(str)
# define TRACE(str)
# define TRACEV(str...)
#endif

typedef struct {
	unsigned busID;			/* fbN index of the board */
	int devFD;			/* fd of /dev/fbN          */

	/* Virtual base of the mapped register window (mmap of /dev/fbN). */
	unsigned long mmio;

	/* ShadowFB: host-side linear buffer that fb.c renders into.  The
	 * Odyssey has no CPU-visible framebuffer, so this is plain memory
	 * that we upload to the card via the CFIFO on each refresh. */
	pointer ShadowPtr;
	unsigned long ShadowPitch;
	unsigned int Bpp;		/* bytes per pixel */

	/* 8bpp pseudo-colour palette, packed 0x00BBGGRR to match the
	 * per-pixel colour words the CFIFO drawpixels path consumes. */
	unsigned pseudo_palette[256];

	/* wrapped functions */
	CloseScreenProcPtr CloseScreen;

	OptionInfoPtr Options;
} VproRec, *VproPtr;

#define VPROPTR(p)	((VproPtr)((p)->driverPrivate))

/* vpro_cmap.c */
static __inline__ unsigned
VproGetPalReg(VproPtr pVpro, int i)
{
	return pVpro->pseudo_palette[i & 0xff];
}
void VproLoadPalette(ScrnInfoPtr, int numColors, int *indices,
		     LOCO *colors, VisualPtr);

/* vpro_accel.c -- the CFIFO command engine.
 *
 * All routines take the virtual MMIO base ("mmio") as first argument.
 */
void vpro_hwinit(unsigned long mmio);		/* one-time Buzz/PB&J bringup   */
void vpro_flush(unsigned long mmio);		/* full pipeline drain          */
void vpro_smallflush(unsigned long mmio);	/* short pipeline drain         */

/* Solid-colour primitives (colour is packed 0x00BBGGRR). */
void vpro_rect(unsigned long mmio, int x, int y, int w, int h,
	       CARD32 color, int logicop);
void vpro_line(unsigned long mmio, int x1, int y1, int x2, int y2,
	       CARD32 color, int logicop);

/* Upload a rectangle of 32bpp pixels from host memory into the card
 * (glDrawPixels-style PIO blit -- "copy pixel data into the framebuffer
 * VRAM").  src points at the top-left pixel, pitch is in bytes. */
void vpro_drawpixels(unsigned long mmio, int x, int y, int w, int h,
		     const CARD32 *src, int pitch);

/* Screen-to-screen copy (glCopyPixels-style). */
void vpro_copyarea(unsigned long mmio, int sx, int sy,
		   int dx, int dy, int w, int h);

/* ShadowFB refresh entry point (registered with ShadowFBInit). */
void VproRefreshArea(ScrnInfoPtr pScrn, int num, BoxPtr pbox);

#endif /* __VPRO_H__ */
