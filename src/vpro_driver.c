/*
 * vpro_driver.c -- Xorg driver for the SGI Odyssey (VPro) graphics board.
 *
 * Copyright (c) 2026 René Rebe <rene@exactcode.de>
 *
 * Structured after the xf86-video-impact driver (c) 2005 peter fuerst,
 * which is based on the newport driver (c) 2000,2001 Guido Guenther.
 *
 * The Odyssey has no CPU-visible linear framebuffer; it is driven purely
 * through a command FIFO.  We therefore use a host-side ShadowFB that the
 * fb layer renders into, and upload the dirty regions to the card through
 * the CFIFO (see vpro_accel.c).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/mman.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "vpro.h"

#include "mipointer.h"
#include "micmap.h"
#include "fb.h"
#include "shadowfb.h"

#define VPRO_MAX_BOARDS		3

#define VPRO_VERSION		1000
#define VPRO_NAME		"VPRO"
#define VPRO_DRIVER_NAME	"vpro"
#define VPRO_MAJOR_VERSION	PACKAGE_VERSION_MAJOR
#define VPRO_MINOR_VERSION	PACKAGE_VERSION_MINOR
#define VPRO_PATCHLEVEL		PACKAGE_VERSION_PATCHLEVEL

/* Prototypes ------------------------------------------------------- */
static void	VproIdentify(int flags);
static const OptionInfoRec *VproAvailableOptions(int chipid, int busid);
static Bool	VproProbe(DriverPtr drv, int flags);
static Bool	VproPreInit(ScrnInfoPtr pScrn, int flags);
static Bool	VproScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool	VproEnterVT(VT_FUNC_ARGS_DECL);
static void	VproLeaveVT(VT_FUNC_ARGS_DECL);
static Bool	VproCloseScreen(CLOSE_SCREEN_ARGS_DECL);
static Bool	VproSaveScreen(ScreenPtr pScreen, int mode);
static unsigned	VproHWProbe(unsigned char ids[], int lim);
static Bool	VproModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode);
static Bool	VproMapRegs(ScrnInfoPtr pScrn);
static void	VproUnmapRegs(ScrnInfoPtr pScrn);
static Bool	VproAllocShadow(ScrnInfoPtr pScrn);
static void	VproFreeShadow(ScrnInfoPtr pScrn);
/* ------------------------------------------------------------------ */

_X_EXPORT DriverRec VPRO = {
	VPRO_VERSION,
	VPRO_DRIVER_NAME,
	VproIdentify,
	VproProbe,
	VproAvailableOptions,
	NULL,
	0
};

/* Supported "chipsets" */
#define CHIP_ODYSSEY		0x1

static SymTabRec VproChipsets[] = {
	{ CHIP_ODYSSEY, "Odyssey" },
	{ -1, NULL }
};

#ifdef XFree86LOADER

static MODULESETUPPROTO(vproSetup);

static XF86ModuleVersionInfo vproVersRec = {
	"vpro",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	VPRO_MAJOR_VERSION, VPRO_MINOR_VERSION, VPRO_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData vproModuleData = { &vproVersRec, vproSetup, NULL };

static pointer
vproSetup(pointer module, pointer opts, int *errmaj, int *errmin)
{
	static Bool setupDone = FALSE;

	if (!setupDone) {
		setupDone = TRUE;
		xf86AddDriver(&VPRO, module, 0);
		return (pointer)1;
	} else {
		if (errmaj)
			*errmaj = LDR_ONCEONLY;
		return 0;
	}
}

#endif /* XFree86LOADER */

typedef enum {
	OPTION_BUS_ID
} VproOpts;

static const OptionInfoRec VproOptions[] = {
	{ OPTION_BUS_ID, "BusID", OPTV_INTEGER, {0}, FALSE },
	{ -1, NULL, OPTV_NONE, {0}, FALSE }
};

/* ------------------------------------------------------------------ */

static Bool
VproGetRec(ScrnInfoPtr pScrn)
{
	VproPtr pVpro;
	int i, j;

	if (pScrn->driverPrivate)
		return TRUE;

	pScrn->driverPrivate = xnfcalloc(sizeof(VproRec), 1);
	pVpro = VPROPTR(pScrn);
	memset(pVpro, 0, sizeof(*pVpro));
	pVpro->devFD = -1;

	for (i = j = 0; i < 256; i++, j += 0x010101)
		pVpro->pseudo_palette[i] = j;

	return TRUE;
}

static void
VproFreeRec(ScrnInfoPtr pScrn)
{
	free(pScrn->driverPrivate);
	pScrn->driverPrivate = 0;
}

static void
VproIdentify(int flags)
{
	xf86PrintChipsets(VPRO_NAME,
			  "driver for the SGI Odyssey (VPro) graphics board",
			  VproChipsets);
}

static const OptionInfoRec *
VproAvailableOptions(int chipid, int busid)
{
	return VproOptions;
}

/*
 * Probe for Odyssey boards by scanning /proc/fb for the "Odyssey" id
 * registered by the kernel odyssey fbdev driver.
 */
static unsigned
VproHWProbe(unsigned char ids[], int lim)
{
	FILE *fb;
	unsigned found = 0;

	if (VPRO_MAX_BOARDS < lim)
		lim = VPRO_MAX_BOARDS;

	if ((fb = fopen("/proc/fb", "r"))) {
		char line[80];
		while (found < lim && fgets(line, sizeof(line), fb)) {
			char *s;
			unsigned i = strtoul(line, &s, 10);
			if (!strncmp(s, " Odyssey", 8))
				ids[found++] = i;
		}
		fclose(fb);
	}
	return found;
}

static Bool
VproProbe(DriverPtr drv, int flags)
{
	int numDevSections, numUsed, i, j, busID;
	Bool foundScreen = FALSE;
	GDevPtr *devSections;
	unsigned char ids[VPRO_MAX_BOARDS];

	if ((numDevSections = xf86MatchDevice(VPRO_DRIVER_NAME, &devSections)) <= 0)
		return FALSE;
	numUsed = VproHWProbe(ids, VPRO_MAX_BOARDS);
	if (numUsed <= 0) {
		free(devSections);
		return FALSE;
	}

	if (flags & PROBE_DETECT)
		foundScreen = TRUE;
	else {
		for (i = 0; i < numDevSections; i++) {
			GDevPtr dev = devSections[i];
			busID = xf86SetIntOption(dev->options, "BusID", 0);

			for (j = 0; j < numUsed; j++)
				if (busID == ids[j]) {
					int entity;
					ScrnInfoPtr pScrn;

					entity = xf86ClaimNoSlot(drv, 0, dev, TRUE);
					pScrn = xf86AllocateScreen(drv, 0);
					xf86AddEntityToScreen(pScrn, entity);
					pScrn->driverVersion = VPRO_VERSION;
					pScrn->driverName = VPRO_DRIVER_NAME;
					pScrn->name = VPRO_NAME;
					pScrn->Probe = VproProbe;
					pScrn->PreInit = VproPreInit;
					pScrn->ScreenInit = VproScreenInit;
					pScrn->EnterVT = VproEnterVT;
					pScrn->LeaveVT = VproLeaveVT;
					pScrn->driverPrivate = (void *)(long)busID;
					foundScreen = TRUE;
					break;
				}
		}
	}
	free(devSections);
	return foundScreen;
}

static Bool
VproPreInit(ScrnInfoPtr pScrn, int flags)
{
	long busID;
	int i;
	VproPtr pVpro;
	ClockRangePtr clockRanges;

	if (flags & PROBE_DETECT)
		return FALSE;

	if (pScrn->numEntities != 1)
		return FALSE;

	busID = (long)pScrn->driverPrivate;
	pScrn->driverPrivate = 0;

	pScrn->monitor = pScrn->confScreen->monitor;

	/* The Odyssey is a fixed 24bpp (packed in 32bpp) truecolour device. */
	if (!xf86SetDepthBpp(pScrn, 24, 24, 32, Support32bppFb))
		return FALSE;

	if (pScrn->depth != 24) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Given depth (%d) is not supported by the vpro driver\n",
			   pScrn->depth);
		return FALSE;
	}

	{
		rgb weights = {8, 8, 8};
		rgb masks = {0x0000ff, 0x00ff00, 0xff0000};
		if (!xf86SetWeight(pScrn, weights, masks))
			return FALSE;
	}

	xf86PrintDepthBpp(pScrn);

	if (!xf86SetDefaultVisual(pScrn, -1))
		return FALSE;
	if (pScrn->defaultVisual != TrueColor) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "Default visual (%s) is not TrueColor\n",
			   xf86GetVisualName(pScrn->defaultVisual));
		return FALSE;
	}

	{
		Gamma zeros = {0.0, 0.0, 0.0};
		if (!xf86SetGamma(pScrn, zeros))
			return FALSE;
	}

	if (!VproGetRec(pScrn))
		return FALSE;
	pVpro = VPROPTR(pScrn);
	pVpro->busID = busID & 0xffff;

	pScrn->progClock = TRUE;

	xf86CollectOptions(pScrn, NULL);
	if (!(pVpro->Options = malloc(sizeof(VproOptions))))
		goto out_freerec;
	memcpy(pVpro->Options, VproOptions, sizeof(VproOptions));
	xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, pVpro->Options);

	pScrn->videoRam = VPRO_FIXED_W_SCRN * VPRO_FIXED_H_SCRN * 4 / 1024;

	xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		   "SGI Odyssey (VPro) on /dev/fb%d\n", pVpro->busID);

	/* Only the fixed 1280x1024 panel timing is supported. */
	clockRanges = xnfalloc(sizeof(ClockRange));
	clockRanges->next = 0;
	clockRanges->minClock = 10000;
	clockRanges->maxClock = 300000;
	clockRanges->clockIndex = -1;
	clockRanges->interlaceAllowed = FALSE;
	clockRanges->doubleScanAllowed = FALSE;

	i = xf86ValidateModes(pScrn, pScrn->monitor->Modes,
			      pScrn->display->modes, clockRanges,
			      NULL, 256, VPRO_FIXED_W_SCRN,
			      pScrn->bitsPerPixel, 128, VPRO_FIXED_H_SCRN,
			      pScrn->display->virtualX,
			      pScrn->display->virtualY,
			      pScrn->videoRam * 1024,
			      LOOKUP_BEST_REFRESH);
	if (i == -1)
		goto out_freeopt;

	xf86PruneDriverModes(pScrn);
	if (!i || !pScrn->modes) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "No valid modes found\n");
		goto out_freeopt;
	}

	xf86SetCrtcForModes(pScrn, 0);
	pScrn->currentMode = pScrn->modes;
	xf86PrintModes(pScrn);
	xf86SetDpi(pScrn, 0, 0);

	if (!xf86LoadSubModule(pScrn, "fb"))
		goto out_freeopt;
	if (!xf86LoadSubModule(pScrn, "shadowfb"))
		goto out_freeopt;

	return TRUE;

out_freeopt:
	free(pVpro->Options);
out_freerec:
	VproFreeRec(pScrn);
	return FALSE;
}

static Bool
VproScreenInit(SCREEN_INIT_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	VproPtr pVpro = VPROPTR(pScrn);
	VisualPtr visual;
	int i;

	if (!VproMapRegs(pScrn))
		return FALSE;

	if (!VproAllocShadow(pScrn)) {
		VproUnmapRegs(pScrn);
		return FALSE;
	}

	miClearVisualTypes();
	if (!miSetVisualTypes(pScrn->depth, TrueColorMask,
			      pScrn->rgbBits, pScrn->defaultVisual))
		goto out_fail;
	miSetPixmapDepths();

	if (!VproModeInit(pScrn, pScrn->currentMode))
		goto out_fail;

	if (!fbScreenInit(pScreen, pVpro->ShadowPtr,
			  pScrn->virtualX, pScrn->virtualY,
			  pScrn->xDpi, pScrn->yDpi,
			  pScrn->displayWidth, pScrn->bitsPerPixel))
		goto out_fail;

	for (i = 0, visual = pScreen->visuals; i < pScreen->numVisuals;
	     i++, visual++)
		if ((visual->class | DynamicClass) == DirectColor) {
			visual->offsetRed = pScrn->offset.red;
			visual->offsetGreen = pScrn->offset.green;
			visual->offsetBlue = pScrn->offset.blue;
			visual->redMask = pScrn->mask.red;
			visual->greenMask = pScrn->mask.green;
			visual->blueMask = pScrn->mask.blue;
		}

	fbPictureInit(pScreen, 0, 0);
	xf86SetBackingStore(pScreen);
	xf86SetBlackWhitePixels(pScreen);

	if (!miDCInitialize(pScreen, xf86GetPointerScreenFuncs()))
		goto out_fail;

	/* Set up the hardware cursor; on failure keep the software cursor. */
	if (!VproHWCursorInit(pScreen))
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
			   "hardware cursor initialization failed,"
			   " using software cursor\n");

	if (!miCreateDefColormap(pScreen))
		goto out_fail;
	if (!xf86HandleColormaps(pScreen, 256, 8, VproLoadPalette, 0,
				 CMAP_RELOAD_ON_MODE_SWITCH))
		goto out_fail;

	/* Push everything fb renders into the card via the CFIFO. */
	ShadowFBInit(pScreen, VproRefreshArea);

	pScreen->SaveScreen = VproSaveScreen;
	pVpro->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = VproCloseScreen;

	if (serverGeneration == 1)
		xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);

	return TRUE;

out_fail:
	VproFreeShadow(pScrn);
	VproUnmapRegs(pScrn);
	return FALSE;
}

/* This programs the actual mode; the panel timing itself is set up by the
 * kernel odyssey fbdev driver, so here we only run the Buzz/PB&J command
 * bringup and clear the screen. */
static Bool
VproModeInit(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
	VproPtr pVpro = VPROPTR(pScrn);

	pScrn->vtSema = TRUE;

	/* Program the DBE video timing generator / DAC for this mode
	 * (mirrors the IRIX PROM odsyLoadTimingTable sequence). */
	if (!VproSetMode(pScrn, mode)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "vpro: hardware mode initialization failed\n");
		return FALSE;
	}

	/* Bring up the Buzz/PB&J command engine and clear the screen. */
	vpro_hwinit(pVpro->mmio);
	vpro_rect(pVpro->mmio, 0, 0, VPRO_FIXED_W_SCRN, VPRO_FIXED_H_SCRN,
		  0x000000, VPRO_LO_COPY);
	return TRUE;
}

static Bool
VproEnterVT(VT_FUNC_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	VproPtr pVpro = VPROPTR(pScrn);
	Bool ret = VproModeInit(pScrn, pScrn->currentMode);

	/* Repaint the whole screen from the shadow on VT switch-in. */
	if (ret) {
		BoxRec box = { 0, 0, VPRO_FIXED_W_SCRN, VPRO_FIXED_H_SCRN };
		VproRefreshArea(pScrn, 1, &box);
	}
	(void)pVpro;
	return ret;
}

static void
VproLeaveVT(VT_FUNC_ARGS_DECL)
{
	SCRN_INFO_PTR(arg);
	pScrn->vtSema = FALSE;
}

static Bool
VproCloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	VproPtr pVpro = VPROPTR(pScrn);

	if (pVpro->CursorInfoRec) {
		xf86DestroyCursorInfoRec(pVpro->CursorInfoRec);
		pVpro->CursorInfoRec = NULL;
	}

	VproFreeShadow(pScrn);
	VproUnmapRegs(pScrn);
	pScrn->vtSema = FALSE;

	if (pScreen->CloseScreen == VproCloseScreen)
		pScreen->CloseScreen = pVpro->CloseScreen;
	return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

static Bool
VproSaveScreen(ScreenPtr pScreen, int mode)
{
	/* TODO: drive DPMS / blanking through the VC3 back end. */
	return TRUE;
}

/*
 * Map the Odyssey register window.  We open /dev/fbN and mmap offset 0,
 * which the kernel odyssey driver maps to the HEART XTalk MMIO space; an
 * uncached mapping is required, which "/dev/fbN" provides for us.
 */
static Bool
VproMapRegs(ScrnInfoPtr pScrn)
{
	VproPtr pVpro = VPROPTR(pScrn);
	void *base;

	if (pVpro->mmio)
		return TRUE;

	if (pVpro->devFD < 0) {
		char devfb[20];
		snprintf(devfb, sizeof(devfb), "/dev/fb%d", pVpro->busID);
		pVpro->devFD = open(devfb, O_RDWR);
	}
	if (pVpro->devFD < 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "failed to open /dev/fb%d (%s)\n",
			   pVpro->busID, strerror(errno));
		return FALSE;
	}

	base = mmap(NULL, VPRO_MMIO_SIZE, PROT_READ | PROT_WRITE,
		    MAP_SHARED, pVpro->devFD, 0);
	if (base == MAP_FAILED) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "could not mmap card registers (%s)\n",
			   strerror(errno));
		pVpro->mmio = 0;
		return FALSE;
	}
	pVpro->mmio = (unsigned long)base;
	return TRUE;
}

static void
VproUnmapRegs(ScrnInfoPtr pScrn)
{
	VproPtr pVpro = VPROPTR(pScrn);

	if (pVpro->mmio)
		munmap((void *)pVpro->mmio, VPRO_MMIO_SIZE);
	pVpro->mmio = 0;

	if (pVpro->devFD >= 0) {
		close(pVpro->devFD);
		pVpro->devFD = -1;
	}
}

/*
 * Allocate the host-side shadow framebuffer.  Unlike the impact driver,
 * the Odyssey exposes no linear VRAM, so this is plain host memory.
 */
static Bool
VproAllocShadow(ScrnInfoPtr pScrn)
{
	VproPtr pVpro = VPROPTR(pScrn);

	if (pVpro->ShadowPtr)
		return TRUE;

	pVpro->Bpp = pScrn->bitsPerPixel >> 3;
	pVpro->ShadowPitch =
		(pScrn->virtualX * pVpro->Bpp + 3) & ~3L;
	pScrn->displayWidth = pVpro->ShadowPitch / pVpro->Bpp;

	pVpro->ShadowPtr = calloc(pVpro->ShadowPitch, pScrn->virtualY);
	if (!pVpro->ShadowPtr) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
			   "failed to allocate %lu byte shadow buffer\n",
			   pVpro->ShadowPitch * pScrn->virtualY);
		return FALSE;
	}
	return TRUE;
}

static void
VproFreeShadow(ScrnInfoPtr pScrn)
{
	VproPtr pVpro = VPROPTR(pScrn);

	if (pVpro->ShadowPtr)
		free(pVpro->ShadowPtr);
	pVpro->ShadowPtr = 0;
}
