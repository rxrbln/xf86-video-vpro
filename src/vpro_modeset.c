/*
 * vpro_modeset.c -- video timing / modeset for the SGI Odyssey (VPro).
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * The Odyssey display back end (DBE) is programmed through the DFIFO: the
 * Video Timing Generator (VTG), the internal DAC, the datapath/state
 * machine and the scanout DMA geometry are all set by pushing DBE register
 * packets.  This mirrors the IRIX PROM routine odsyLoadTimingTable(); the
 * DBE register addresses were recovered from the IP30 fprom disassembly
 * (see vpro_regs.h) and the per-mode scalar values come from the IRIX
 * source odsy_timings.c (odsy_default_timing1).
 *
 * Scope: the boot PROM already programs the PLL (over I2C) and uploads the
 * VTG frame/line tables for the native 1280x1024@60 panel.  This routine
 * re-establishes the DBE control/DAC/DMA state for that mode -- which is
 * what is needed to guarantee a known-good display on mode init and on VT
 * switch-in -- without touching the I2C PLL or re-streaming the VTG tables.
 * Alternate pixel clocks/resolutions would additionally need the I2C PLL
 * sequence and a VTG table stream through VPRO_DBE_VTG_FRAMETABLE (0x2400);
 * that is deliberately left out until it can be validated on hardware.
 *
 * This file is subject to the same license as the rest of the driver.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vpro.h"

/*
 * A single Odyssey display timing.  The scalar values are those loaded by
 * the IRIX PROM for the corresponding panel (odsy_timings.c).
 */
typedef struct {
	int w, h;			/* active pixels            */
	int refresh;			/* Hz                       */
	CARD32 vtg_control;		/* VTG_Control (| RUN bits) */
	CARD32 vtg_init;		/* VTG_initialState         */
	CARD32 vtg_enable;		/* VTG_Chan_En              */
	CARD32 dac_control;
	CARD32 dp_control;		/* PBJ datapath control     */
	CARD32 sm_control;		/* PBJ state machine config */
	CARD32 flags;			/* interlace/dualhead/fldseq*/
} VproTiming;

/* 1280x1024 @ 60Hz -- odsy_default_timing1. */
static const VproTiming vpro_timing_1280x1024_60 = {
	.w		= 1280,
	.h		= 1024,
	.refresh	= 60,
	.vtg_control	= 0,			/* RUN bits OR'd in below */
	.vtg_init	= 0,
	.vtg_enable	= 2096959,		/* 0x1ffdff               */
	.dac_control	= 6,
	.dp_control	= 1,
	.sm_control	= 269615104,		/* 0x1013fc00             */
	.flags		= 0,
};

/* Throttle a run of DBE register writes so the DFIFO never overflows. */
static __inline__ void
vpro_dbe_write(unsigned long mmio, CARD32 reg, CARD32 val)
{
	vpro_wait_dfifo(mmio, 0);
	vpro_dfifo_write(mmio, reg, val);
}

/*
 * Program the DBE for the given timing.  Follows the register order of the
 * IRIX odsyLoadTimingTable() tail: stop the VTG, push the timing/DAC/DMA
 * registers, then start the VTG.
 */
static void
vpro_load_timing(unsigned long mmio, const VproTiming *t)
{
	CARD32 mode;

	/* Stop the VTG before touching its state. */
	vpro_dbe_write(mmio, VPRO_DBE_VTG_CONTROL, VPRO_VTG_CONTROL_RESET);

	/*
	 * (The VTG frame/line tables are left as the PROM loaded them.  For
	 * alternate resolutions they would be streamed here through
	 * VPRO_DBE_VTG_FRAMETABLE with a multi-word DBE packet.)
	 */

	/* VTG timing generator. */
	vpro_dbe_write(mmio, VPRO_DBE_VTG_CONTROL,
		       t->vtg_control | VPRO_VTG_CONTROL_RUN);
	vpro_dbe_write(mmio, VPRO_DBE_VTG_INITIALSTATE, t->vtg_init);
	vpro_dbe_write(mmio, VPRO_DBE_VTG_ENABLE, t->vtg_enable);

	/* Scanout DMA geometry. */
	vpro_dbe_write(mmio, VPRO_DBE_DMA_WIDTHPIXELS, t->w);
	vpro_dbe_write(mmio, VPRO_DBE_DMA_HEIGHTPIXELS, t->h);
	mode = t->flags & 0x3;			/* interlace mode/line */
	vpro_dbe_write(mmio, VPRO_DBE_DMA_INTERLACEMODE, mode);
	mode = t->flags & 0x10;			/* dual head           */
	vpro_dbe_write(mmio, VPRO_DBE_DMA_DUALHEADMODE, mode);
	mode = t->flags & 0x20;			/* field sequential    */
	vpro_dbe_write(mmio, VPRO_DBE_DMA_FLDSEQMODE, mode);

	/* XMAP + cursor. */
	vpro_dbe_write(mmio, VPRO_DBE_XMAP_CONFIG, t->flags & 0x20);
	vpro_dbe_write(mmio, VPRO_DBE_CURSOR_CONTROL, 0);

	/* Internal DAC, datapath and state machine. */
	vpro_dbe_write(mmio, VPRO_DBE_DAC_CONTROL, t->dac_control);
	vpro_dbe_write(mmio, VPRO_DBE_PBJ_DPATH_CTL, t->dp_control);
	vpro_dbe_write(mmio, VPRO_DBE_SM_CONFIG, t->sm_control);

	vpro_wait_dfifo(mmio, 0);
}

/*
 * Public entry: program the panel timing for the requested mode.  Only the
 * native 1280x1024 panel timing is supported; other geometries are
 * rejected so callers fall back cleanly.
 */
Bool
VproSetMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
	VproPtr pVpro = VPROPTR(pScrn);

	if (mode->HDisplay != VPRO_FIXED_W_SCRN ||
	    mode->VDisplay != VPRO_FIXED_H_SCRN) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "vpro: unsupported mode %dx%d (only %dx%d)\n",
			   mode->HDisplay, mode->VDisplay,
			   VPRO_FIXED_W_SCRN, VPRO_FIXED_H_SCRN);
		return FALSE;
	}

	vpro_load_timing(pVpro->mmio, &vpro_timing_1280x1024_60);
	return TRUE;
}
