/* xf86drmCompat.c -- User-level interface to old DRM devices
 *
 * Copyright 2000 VA Linx Systems, Inc., Fremont, California.
 * Copyright 2002 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Original Authors:
 *   Gareth Hughes <gareth@valinux.com>
 *   Kevin E. Martin <martin@valinux.com>
 *
 * Backwards compatability modules broken out by:
 *   Jens Owen <jens@tungstengraphics.com>
 *
 */
/* $XFree86: xc/programs/Xserver/hw/xfree86/os-support/linux/drm/xf86drm.c,v 1.4 2001/08/27 17:40:59 dawes Exp $ */

#ifdef XFree86Server
# include "xf86.h"
# include "xf86_OSproc.h"
# include "xf86_ansic.h"
# define _DRM_MALLOC xalloc
# define _DRM_FREE   xfree
# ifndef XFree86LOADER
#  include <sys/mman.h>
# endif
#else
# include <stdio.h>
# include <stdlib.h>
# include <unistd.h>
# include <string.h>
# include <ctype.h>
# include <fcntl.h>
# include <errno.h>
# include <signal.h>
# include <sys/types.h>
# include <sys/ioctl.h>
# include <sys/mman.h>
# include <sys/time.h>
# ifdef DRM_USE_MALLOC
#  define _DRM_MALLOC malloc
#  define _DRM_FREE   free
extern int xf86InstallSIGIOHandler(int fd, void (*f)(int, void *), void *);
extern int xf86RemoveSIGIOHandler(int fd);
# else
#  include <X11/Xlibint.h>
#  define _DRM_MALLOC Xmalloc
#  define _DRM_FREE   Xfree
# endif
#endif

/* Not all systems have MAP_FAILED defined */
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#ifdef __linux__
#include <sys/sysmacros.h>	/* for makedev() */
#endif
#include "xf86drm.h"
#include "xf86drmCompat.h"
#include "drm.h"


/* WARNING: Do not change, or add, anything to this file.  It is only provided
 * for backwards compatability with the old driver specific DRM extensions
 * found in XFree86 4.0, 4.1 and 4.2.
 */


int drmRadeonInitCP( int fd, drmCompatRadeonInit *info )
{
   drm_radeon_init_t init;

   memset( &init, 0, sizeof(drm_radeon_init_t) );

   init.func			= RADEON_INIT_CP;
   init.sarea_priv_offset	= info->sarea_priv_offset;
   init.is_pci			= info->is_pci;
   init.cp_mode			= info->cp_mode;
   init.agp_size		= info->agp_size;
   init.ring_size		= info->ring_size;
   init.usec_timeout		= info->usec_timeout;

   init.fb_bpp			= info->fb_bpp;
   init.front_offset		= info->front_offset;
   init.front_pitch		= info->front_pitch;
   init.back_offset		= info->back_offset;
   init.back_pitch		= info->back_pitch;

   init.depth_bpp		= info->depth_bpp;
   init.depth_offset		= info->depth_offset;
   init.depth_pitch		= info->depth_pitch;

   init.fb_offset		= info->fb_offset;
   init.mmio_offset		= info->mmio_offset;
   init.ring_offset		= info->ring_offset;
   init.ring_rptr_offset	= info->ring_rptr_offset;
   init.buffers_offset		= info->buffers_offset;
   init.agp_textures_offset	= info->agp_textures_offset;

   if ( ioctl( fd, DRM_IOCTL_RADEON_CP_INIT, &init ) ) {
      return -errno;
   } else {
      return 0;
   }
}

