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
