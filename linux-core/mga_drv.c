/* mga_drv.c -- Matrox G200/G400 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:56:22 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "mga.h"
#include "drmP.h"
#include "mga_drv.h"

#define DRIVER_AUTHOR		"Gareth Hughes, VA Linux Systems Inc."

#define DRIVER_NAME		"mga"
#define DRIVER_DESC		"Matrox G200/G400"
#define DRIVER_DATE		"20010206"

#define DRIVER_MAJOR		3
#define DRIVER_MINOR		0
#define DRIVER_PATCHLEVEL	0

/* Now that we do this, we can move the DRM(ioctls) array into a
 * template file and have a DRIVER_IOCTLS block at the end.
 */
static drm_ioctl_desc_t		mga_ioctls[] = {
	[DRM_IOCTL_NR(DRM_IOCTL_VERSION)]     = { mga_version,     0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_UNIQUE)]  = { mga_getunique,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAGIC)]   = { mga_getmagic,    0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_IRQ_BUSID)]   = { mga_irq_busid,   0, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_MAP)]     = { mga_getmap,      0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CLIENT)]  = { mga_getclient,   0, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_STATS)]   = { mga_getstats,    0, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_SET_UNIQUE)]  = { mga_setunique,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_BLOCK)]       = { mga_block,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNBLOCK)]     = { mga_unblock,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_CONTROL)]     = { mga_control,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AUTH_MAGIC)]  = { mga_authmagic,   1, 1 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_MAP)]     = { mga_addmap,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_ADD_BUFS)]    = { mga_addbufs,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MARK_BUFS)]   = { mga_markbufs,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_INFO_BUFS)]   = { mga_infobufs,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MAP_BUFS)]    = { mga_mapbufs,     1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FREE_BUFS)]   = { mga_freebufs,    1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_CTX)]     = { mga_addctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_CTX)]      = { mga_rmctx,       1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MOD_CTX)]     = { mga_modctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_GET_CTX)]     = { mga_getctx,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_SWITCH_CTX)]  = { mga_switchctx,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_NEW_CTX)]     = { mga_newctx,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RES_CTX)]     = { mga_resctx,      1, 0 },

	[DRM_IOCTL_NR(DRM_IOCTL_ADD_DRAW)]    = { mga_adddraw,     1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_RM_DRAW)]     = { mga_rmdraw,      1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_DMA)]	      = { mga_dma_buffers, 1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_LOCK)]	      = { mga_lock,        1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_UNLOCK)]      = { mga_unlock,      1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_FINISH)]      = { mga_finish,      1, 0 },

#if defined(CONFIG_AGP) || defined(CONFIG_AGP_MODULE)
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ACQUIRE)] = { mga_agp_acquire, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_RELEASE)] = { mga_agp_release, 1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ENABLE)]  = { mga_agp_enable,  1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_INFO)]    = { mga_agp_info,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_ALLOC)]   = { mga_agp_alloc,   1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_FREE)]    = { mga_agp_free,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_BIND)]    = { mga_agp_bind,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_AGP_UNBIND)]  = { mga_agp_unbind,  1, 1 },
#endif

	[DRM_IOCTL_NR(DRM_IOCTL_MGA_INIT)]    = { mga_dma_init,    1, 1 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_FLUSH)]   = { mga_dma_flush,   1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_RESET)]   = { mga_dma_reset,   1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_SWAP)]    = { mga_dma_swap,    1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_CLEAR)]   = { mga_dma_clear,   1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_VERTEX)]  = { mga_dma_vertex,  1, 0 },
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_ILOAD)]   = { mga_dma_iload,   1, 0 },
#if 0
	[DRM_IOCTL_NR(DRM_IOCTL_MGA_INDICES)] = { mga_indices,     1, 0 },
#endif
};

#define DRIVER_IOCTL_COUNT	DRM_ARRAY_SIZE( mga_ioctls )

#define __HAVE_AGP		1
#define __MUST_HAVE_AGP		1

#define __HAVE_MTRR		1

#define __HAVE_CTX_BITMAP	1

#define __HAVE_DMA		1
#define __HAVE_DMA_IRQ		1

#define __HAVE_COUNTERS         3
#define __HAVE_COUNTER6         _DRM_STAT_IRQ
#define __HAVE_COUNTER7         _DRM_STAT_PRIMARY
#define __HAVE_COUNTER8         _DRM_STAT_SECONDARY

#if 0
#define __HAVE_DMA_QUEUE	1
#define __HAVE_DMA_SCHEDULE	1
#endif

#define __HAVE_DMA_QUIESCENT	1
#define DRIVER_DMA_QUIESCENT()						\
do {									\
	drm_mga_private_t *dev_priv = dev->dev_private;			\
	return mga_do_wait_for_idle( dev_priv );			\
} while (0)

#if 0
#define __HAVE_DRIVER_RELEASE	1
#define DRIVER_RELEASE() do {						\
	mga_reclaim_buffers( dev, priv->pid );				\
	if ( dev->dev_private ) {					\
		drm_mga_private_t *dev_priv = dev->dev_private;		\
		dev_priv->dispatch_status &= MGA_IN_DISPATCH;		\
	}								\
} while (0)
#endif

#define DRIVER_PRETAKEDOWN() do {					\
	if ( dev->dev_private ) mga_do_cleanup_dma( dev );		\
} while (0)

#include "drm_drv.h"
