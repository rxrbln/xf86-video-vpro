/*
 * vpro_regs.h -- register and command-FIFO definitions for the
 *                SGI Odyssey (VPro) graphics board.
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * The register offsets, FIFO layout and the GL-like command tokens used
 * here were recovered from the Linux fbdev driver
 *   drivers/video/fbdev/odyssey.c  and  odyssey_early.c
 * (Copyright (c) Stanislaw Skowronek, Johannes Dickgreber, Joshua Kinard,
 *  René Rebe) and cross-checked against the IRIX PROM sources in
 *   stand/arcs/lib/libsk/graphics/ODYSSEY (odsy_tport.c, odsy_flow.c).
 *
 * The Odyssey is a dual-chip OpenGL implementation ("Buzz" geometry /
 * raster front end + "PB&J" back end).  It has NO CPU-visible linear
 * framebuffer: everything -- clears, rectangles, lines, and pixel
 * uploads -- is programmed by feeding a stream of GL-like command tokens
 * into the command FIFO (CFIFO) and register writes into the data FIFO
 * (DFIFO).
 *
 * This file is subject to the same license as the rest of the driver.
 */

#ifndef __VPRO_REGS_H__
#define __VPRO_REGS_H__

#include "xf86.h"

/* Size of the memory-mapped register window (mmap of /dev/fbN, offset 0). */
#define VPRO_MMIO_SIZE		0x410000

/* Fixed panel geometry of the Odyssey/VPro. */
#define VPRO_FIXED_W_SCRN	1280
#define VPRO_FIXED_H_SCRN	1024

/* Xtalk identity (informational). */
#define VPRO_XTALK_MFGR		0x023
#define VPRO_XTALK_PART		0xc013

/*
 * Convenient access macros.  "mmio" is the virtual base address of the
 * mapped register window as returned by mmap().
 */
#define VPRO_REG64(mmio, off)	(*(volatile CARD64 *)((char *)(mmio) + (off)))
#define VPRO_REG32(mmio, off)	(*(volatile CARD32 *)((char *)(mmio) + (off)))

/* Command FIFO (write 32-bit tokens here). */
#define VPRO_CFIFO_D(mmio)	VPRO_REG64(mmio, 0x110000)
#define VPRO_CFIFO_W(mmio)	VPRO_REG32(mmio, 0x110000)

/* Data FIFO (used to write PB&J back-end registers: gamma, clut, ...). */
#define VPRO_DFIFO_D(mmio)	VPRO_REG64(mmio, 0x400000)
#define VPRO_DFIFO_W(mmio)	VPRO_REG32(mmio, 0x400000)

/* Status / FIFO-level registers. */
#define VPRO_STATUS0(mmio)	VPRO_REG32(mmio, 0x001064)
#define VPRO_STATUS0_CFIFO_HW	0x00008000	/* command FIFO high water */
#define VPRO_STATUS0_CFIFO_LW	0x00020000	/* command FIFO low water  */
#define VPRO_DBESTAT(mmio)	VPRO_REG32(mmio, 0x00106c)

/*
 * PP1 raster-op / logic-op selectors (Odyssey "logicop").
 *   SI = source invert, DI = dest invert, RI = result invert.
 */
#define VPRO_LO_AND		0x01
#define VPRO_LO_SI_AND		0x04
#define VPRO_LO_DI_AND		0x02
#define VPRO_LO_RI_AND		0x0e
#define VPRO_LO_OR		0x07
#define VPRO_LO_SI_OR		0x0d
#define VPRO_LO_DI_OR		0x0b
#define VPRO_LO_RI_OR		0x08
#define VPRO_LO_XOR		0x06
#define VPRO_LO_RI_XOR		0x09
#define VPRO_LO_NOP		0x05
#define VPRO_LO_RI_NOP		0x0a
#define VPRO_LO_COPY		0x03
#define VPRO_LO_RI_COPY		0x0c
#define VPRO_LO_CLEAR		0x00
#define VPRO_LO_SET		0x0f

/*
 * GL-like command tokens fed into the CFIFO.  These opaque magic numbers
 * come straight from the IRIX microcode command stream; the comments name
 * the OpenGL primitive they encode.
 */
#define VPRO_TOK_BEGIN		0x00014400	/* glBegin,  next word = mode */
#define VPRO_TOK_END		0x00014001	/* glEnd                      */
#define VPRO_TOK_COLOR3UB	0xc580cc08	/* glColor3ub, next 3 words   */
#define VPRO_TOK_VERTEX2I	0x8080c800	/* glVertex2i, next 2 words   */
#define VPRO_TOK_LOGICOP	0x00010422	/* glLogicOp,  next word = op */

#define VPRO_PRIM_POINTS	0x00000000
#define VPRO_PRIM_LINES		0x00000001
#define VPRO_PRIM_LINE_STRIP	0x00000003
#define VPRO_PRIM_QUADS		0x00000007

/*
 * DBE (Display Back End) register addresses.
 *
 * DBE registers -- the video timing generator (VTG), the internal DAC, the
 * datapath/state-machine and the scanout DMA geometry -- are programmed by
 * pushing packets into the DFIFO.  A single-register write is
 *     DFIFO_D = ((CARD64)(0x30000001 | (reg << 14)) << 32) | value;
 * which is exactly what vpro_dfifo_write() below emits (and what the kernel
 * odyssey_dfifo_write() uses for cmap/gamma).
 *
 * These addresses were recovered by disassembling the IP30 (Octane) PROM
 * routines odsyLoadTimingTable() and odsyInitDBE().  cmap_clut/gamma match
 * the values already used by the Linux driver, and cursor_control (0x2581)
 * cross-checks against odyssey_initpbjvc() which clears exactly that reg.
 */
#define VPRO_DBE_GAMMA			0x1a00	/* gamma ramp base    */
#define VPRO_DBE_VTG_FRAMETABLE		0x2400	/* VTG table stream port */
#define VPRO_DBE_VTG_CONTROL		0x2481
#define VPRO_DBE_VTG_INITIALSTATE	0x2482
#define VPRO_DBE_VTG_ENABLE		0x2483
#define VPRO_DBE_CURSOR_CONTROL		0x2581
#define VPRO_DBE_PBJ_DPATH_CTL		0x2600
#define VPRO_DBE_SM_CONFIG		0x2680
#define VPRO_DBE_DAC_CONTROL		0x26c0
#define VPRO_DBE_PBJ_HIF_CONTROL	0x2700
#define VPRO_DBE_GEN_LPFD		0x2780	/* genlock block (12 regs) */
#define VPRO_DBE_DMA_WIDTHPIXELS	0x2802
#define VPRO_DBE_DMA_HEIGHTPIXELS	0x2803
#define VPRO_DBE_DMA_INTERLACEMODE	0x2804
#define VPRO_DBE_DMA_DUALHEADMODE	0x2805
#define VPRO_DBE_DMA_FLDSEQMODE		0x2806
#define VPRO_DBE_CMAP_CLUT		0x2900	/* colour LUT base    */
#define VPRO_DBE_XMAP_CONFIG		0x2929
#define VPRO_DBE_BUZZ_HIF_CONFIG	0x2b00

/* VTG_control bits */
#define VPRO_VTG_CONTROL_RESET		0x00000001
#define VPRO_VTG_CONTROL_RUN		0x000001b8	/* |= before start */

/*
 * Hardware cursor DBE registers (recovered from the IP30 fprom
 * odsy_init_cursor / the cursor position routine, cross-checked against the
 * IRIX standalone odsy_init_cursor() in odsy_tport.c).
 *
 * The Odyssey cursor is 32x32.  The glyph is 2 bits/pixel (index into the
 * cursor LUT), the alpha plane is 1 bit/pixel (coverage), and the LUT holds
 * the colours.  Glyph/alpha/LUT are pushed as multi-word DBE packets; the
 * position and control are single-register writes.
 */
/*
 * The glyph is 4 bits per pixel (index into the 15-entry LUT; index 15
 * inverts the covered pixel) and the alpha plane is 2 bits per pixel
 * (coverage: 3 = opaque, 0 = transparent).  Both were confirmed from the
 * fprom stream lengths (glyph 128 words, alpha 64 words) and the standalone
 * odsy_init_cursor() arrow data in odsy_tport.c.
 */
#define VPRO_DBE_CURSOR_GLYPH		0x2500	/* 128 words (32x32, 4bpp) */
#define VPRO_DBE_CURSOR_XY		0x2580	/* (x<<16)|(y&0xffff)      */
/*      VPRO_DBE_CURSOR_CONTROL		0x2581 -- defined above          */
#define VPRO_DBE_CURSOR_LUT		0x2590	/* 15 colour entries       */
#define VPRO_DBE_CURSOR_ALPHA		0x25c0	/* 64 words (32x32, 2bpp)  */

#define VPRO_CURSOR_SIZE		32
#define VPRO_CURSOR_GLYPH_WORDS		128	/* 4 words/row * 32 rows   */
#define VPRO_CURSOR_ALPHA_WORDS		64	/* 2 words/row * 32 rows   */
#define VPRO_CURSOR_LUT_ENTRIES		15

/* cursor_control: bit0 = delay_enable, bit1 = enable. */
#define VPRO_CURSOR_CONTROL_ON		0x00000003
#define VPRO_CURSOR_CONTROL_OFF		0x00000000

/*
 * DBE packet header for a write of `count` words to DBE register `reg`
 * (count == 1 for a single register; see vpro_dfifo_write()).  Recovered
 * form: header = 0x30000000 | (reg << 14) | count.
 */
#define VPRO_DBE_HDR(reg, count)	(0x30000000u | ((reg) << 14) | (count))

/*
 * Poll helpers.  Mirror odyssey_wait_cfifo()/odyssey_wait_dfifo() from the
 * kernel header <video/odyssey.h>.
 */
static __inline__ void
vpro_wait_cfifo(unsigned long mmio)
{
	while (!(VPRO_STATUS0(mmio) & VPRO_STATUS0_CFIFO_LW))
		;
}

static __inline__ void
vpro_wait_dfifo(unsigned long mmio, int lw)
{
	while ((VPRO_DBESTAT(mmio) & 0x7f) > lw)
		;
}

static __inline__ void
vpro_dfifo_write(unsigned long mmio, CARD32 reg, CARD32 val)
{
	VPRO_DFIFO_D(mmio) =
		(((CARD64)(0x30000001 | (reg << 14)) << 32) | val);
}

#endif /* __VPRO_REGS_H__ */
