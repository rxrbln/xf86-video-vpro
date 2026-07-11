/*
 * vpro_cmap.c -- pseudo-colour palette handling for xf86-video-vpro.
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * Based on impact_cmap.c / newport_cmap.c.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vpro.h"

/*
 * Load a colourmap into the software pseudo-palette.  Entries are packed
 * 0x00BBGGRR to match the per-pixel colour words the CFIFO drawpixels path
 * (and the solid-primitive glColor3ub) consume.
 */
void
VproLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
		LOCO *colors, VisualPtr pVisual)
{
	VproPtr pVpro = VPROPTR(pScrn);

	if (numColors > 256)
		numColors = 256;

	for (; numColors > 0; numColors--, indices++, colors++) {
		unsigned rgb = colors->blue & 0xff00;
		rgb = (rgb << 8) | (colors->green & 0xff00) | (colors->red >> 8);
		pVpro->pseudo_palette[*indices] = rgb;
	}
}
