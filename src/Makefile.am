#  xf86-video-vpro src/Makefile.am
#  Based on xf86-video-impact (Copyright 2005 Adam Jackson).
#
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end

AM_CFLAGS = @XORG_CFLAGS@
vpro_drv_la_LTLIBRARIES = vpro_drv.la
vpro_drv_la_LDFLAGS = -module -avoid-version
vpro_drv_ladir = @moduledir@/drivers

vpro_drv_la_SOURCES = \
         vpro.h \
         vpro_regs.h \
         compat-api.h \
         vpro_driver.c \
         vpro_accel.c \
         vpro_modeset.c \
         vpro_cursor.c \
         vpro_cmap.c
