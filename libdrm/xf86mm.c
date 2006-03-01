/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Steamboat Springs, CO.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE 
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * 
 **************************************************************************/

#ifdef HAVE_XORG_CONFIG_H
#include <xorg-config.h>
#endif

#include "xf86drm.h"

#ifdef XFree86Server
# include "xf86.h"
# include "xf86_OSproc.h"
# include "drm.h"
# include "xf86_ansic.h"
# define _DRM_MALLOC xalloc
# define _DRM_FREE   xfree
# ifndef XFree86LOADER
#  include <sys/mman.h>
# endif
#else
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#endif

static drm_handle_t drmMMSareaHandle;
static volatile drm_mm_sarea_t *drmMMSarea = NULL;

/*
 * Initialize the memory manager. This is typically done by the X server.
 * Give offset and size for video Ram and page offset and number of pages
 * in the TT Aperture that can be used by the managed region. This function
 * also sets up and maps the memory manager SAREA for the calling client.
 */

int
drmMMInit(int drmFD, unsigned long vRamOffs, unsigned long vRamSize,
    unsigned long ttPageOffs, unsigned long ttNumPages)
{
    drm_mm_init_arg_t ma;
    drmAddress sa;
    int ret;

    memset(&ma, 0, sizeof(ma));

    ma.vr_offset_lo = vRamOffs & 0xFFFFFFFFU;
    ma.vr_size_lo = vRamSize & 0xFFFFFFFFU;
    ma.tt_p_offset_lo = ttPageOffs & 0xFFFFFFFFU;
    ma.tt_p_size_lo = ttNumPages & 0xFFFFFFFFU;
    if (sizeof(vRamOffs) == 8) {
	int shift = 32;

	ma.vr_offset_hi = vRamOffs >> shift;
	ma.vr_size_hi = vRamSize >> shift;
	ma.tt_p_offset_hi = ttPageOffs >> shift;
	ma.tt_p_size_hi = ttNumPages >> shift;
	ma.op = mm_init;
    }

    ret = ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma);

    if (ret) {
	drmMsg("drmMMInit: failed: %s\n", strerror(errno));
	return -errno;
    }

    drmMMSareaHandle = ma.mm_sarea;
    if (drmMap(drmFD, ma.mm_sarea, DRM_MM_SAREA_SIZE, &sa)) {
	ret = -errno;
	drmMsg("drmMMInit: could not map mm sarea: %s\n", strerror(errno));
	ma.op = mm_takedown;
	ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma);
	return ret;
    }
    drmMMSarea = (drm_mm_sarea_t *) sa;

    return 0;
}

/*
 * Take down the memory manager. Typically on X server exit.
 */

int
drmMMTakedown(int drmFD)
{
    drm_mm_init_arg_t ma;
    int ret = 0;

    ma.op = mm_takedown;
    if (ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma)) {
	drmMsg("drmMMTakedown: failed: %s\n", strerror(errno));
	ret = -errno;
    }

    drmMMSarea = NULL;
    return ret;
}

/*
 * Helper to map the memory manager for the client unless it has not
 * already been done.
 */

static void
drmMapMMSarea(int drmFD, drm_handle_t handle)
{
    if (drmMMSarea)
	return;

    if (drmMap(drmFD, handle, DRM_MM_SAREA_SIZE, (drmAddress) & drmMMSarea)) {
	drmMsg("drmMapMMSarea: failed: %s\n", strerror(errno));
	drmMMSarea = NULL;
	return;
    }
    drmMMSareaHandle = handle;
    printf("Mapped sarea\n");
}


int
drmEmitFence(int drmFD, unsigned fence_type, drm_fence_t * fence)
{
    drm_fence_arg_t arg;


    arg.op = emit_fence;
    arg.fence_type = fence_type;

    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	drmMsg("drmEmitFence: failed: %s\n", strerror(errno));
	return -errno;
    }

    fence->fence_type = fence_type;
    fence->fence_seq = arg.fence_seq;
    drmMapMMSarea(drmFD, arg.mm_sarea);

    return 0;
}

int
drmWaitFence(int drmFD, drm_fence_t fence)
{
    drm_fence_arg_t arg;

    if (drmMMSarea
	&& (drmMMSarea->retired[fence.fence_type & DRM_FENCE_MASK] -
	    fence.fence_seq) <
	(1 << 23))
	return 0;

    arg.op = wait_fence;
    arg.fence_type = fence.fence_type;
    arg.fence_seq = fence.fence_seq;
    
    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	drmMsg("drmWaitFence: failed: %s\n", strerror(errno));
	
	return -errno;
    }
    drmMapMMSarea(drmFD, arg.mm_sarea);

    return 0;
}

/*
 * Test if a fence is retired. This might give the result that the fence is not
 * retired while it really is. Set really to TRUE to have the kernel called to
 * really check and update the sarea for the specific fence type.
 */

int
drmTestFence(int drmFD, drm_fence_t fence, int really, int *retired)
{
    drm_fence_arg_t arg;

    if (drmMMSarea
	&& (drmMMSarea->retired[fence.fence_type & DRM_FENCE_MASK] -
	    fence.fence_seq) <
	(1 << 23)) {
	*retired = 1;
	return 0;
    }

    arg.op = test_fence;
    arg.fence_type = fence.fence_type;
    arg.fence_seq = fence.fence_seq;

    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	return -errno;
    }
    *retired = arg.ret;
    drmMapMMSarea(drmFD, arg.mm_sarea);

    return 0;
}
