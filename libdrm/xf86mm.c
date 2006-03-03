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

/*
 * TODO before drop:
 * 
 * Sarea spinlock
 * Kernel garbage eviction, icluding keeping track of validations.
 * Kernel flag handling.
 * Kernel argument ioctl handling.
 * Clean up Mesa layer interface (bufmgr.c)
 * Kernel init ioctl.
 * Kernel hashing mechanism.
 *
 * FUTURE TODO:
 *
 * managed pools.
 * documentation.
 * VRAM
 */

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

/*
 * static kernel info.
 */

static drm_handle_t drmMMSareaHandle;
static volatile drm_mm_sarea_t *drmMMSarea = NULL;
static int kernelOnEmitFence = 0;
static unsigned numFenceTypes;
static unsigned pageSize;

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
drmEmitFence(int drmFD, unsigned fenceType, drmFence * fence)
{
    drm_fence_arg_t arg;

    if (drmMMSarea && !kernelOnEmitFence) {
	fence->fenceType = fenceType;
	fence->fenceSeq = drmMMSarea->emitted[fenceType & DRM_FENCE_MASK];
	return 0;
    }

    arg.op = emit_fence;
    arg.fence_type = fenceType;

    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	drmMsg("drmEmitFence: failed: %s\n", strerror(errno));
	return -errno;
    }

    fence->fenceType = fenceType;
    fence->fenceSeq = arg.fence_seq;
    drmMapMMSarea(drmFD, arg.mm_sarea);

    return 0;
}

int
drmWaitFence(int drmFD, drmFence fence)
{
    drm_fence_arg_t arg;

    if (drmMMSarea
	&& (drmMMSarea->retired[fence.fenceType & DRM_FENCE_MASK] -
	    fence.fenceSeq) < (1 << 23))
	return 0;

    arg.op = wait_fence;
    arg.fence_type = fence.fenceType;
    arg.fence_seq = fence.fenceSeq;

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
drmTestFence(int drmFD, drmFence fence, int really, int *retired)
{
    drm_fence_arg_t arg;

    if (drmMMSarea
	&& (drmMMSarea->retired[fence.fenceType & DRM_FENCE_MASK] -
	    fence.fenceSeq) < (1 << 23)) {
	*retired = 1;
	return 0;
    }

    arg.op = test_fence;
    arg.fence_type = fence.fenceType;
    arg.fence_seq = fence.fenceSeq;

    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	return -errno;
    }
    *retired = arg.ret;
    drmMapMMSarea(drmFD, arg.mm_sarea);

    return 0;
}

int
drmMMAllocBufferPool(int drmFD, MMPoolTypes type,
    unsigned flags, unsigned long size, drmSize bufferSize, drmMMPool * pool)
{

    drm_ttm_arg_t arg;
    unsigned pageSize = getpagesize();
    drmMMBuf *buffers;

    if (type != mmPoolRing) {
	drmMsg("drmMMAllocBufferPool: Unimplemented pool type\n");
	return -1;
    }

    if (!(flags & (DRM_MM_VRAM | DRM_MM_TT))) {
	drmMsg("drmMMAllocBufferPool: Pool not in VRAM or TT\n");
	return -1;
    }

    if (!(flags & DRM_MM_VRAM)) {
	drmMsg("drmMMAllocBufferPool: VRAM not implemented yet\n");
	return -1;
    }

    size = (size + pageSize - 1) / pageSize;

    arg.op = ttm_add;
    arg.size = size;
    arg.num_bufs = 0;

    if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
	drmMsg("drmMMAllocBufferPool: %s\n", strerror(errno));
	return -errno;
    }

    pool->kernelPool = arg.handle;
    if (drmMap(drmFD, pool->kernelPool, size, (drmAddress) & pool->virtual)) {
	drmMsg("drmMMAllocBufferPool: Could not map pool: %s\n",
	    strerror(errno));
	arg.op = ttm_remove;
	ioctl(drmFD, DRM_IOCTL_TTM, &arg);
	return -errno;
    }
    pool->pinned = 0;
    if (flags & (DRM_MM_NO_EVICT | DRM_MM_NO_MOVE)) {
	drm_ttm_buf_arg_t ba;

	ba.op = ttm_validate;
	ba.ttm_handle = arg.handle;
	ba.flags = DRM_MM_TT | DRM_MM_NO_EVICT | DRM_MM_NO_MOVE;
	ba.num_pages = size / pageSize;
	ba.next = NULL;
	arg.first = &ba;
	arg.num_bufs = 1;
	arg.op = ttm_bufs;

	if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
	    drmMsg("drmMMAllocBufferPool: Could not validate pool: %s\n",
		strerror(errno));
	    arg.op = ttm_remove;
	    ioctl(drmFD, DRM_IOCTL_TTM, &arg);
	    return -errno;
	}
	pool->kernelBuf = ba.region_handle;
	pool->pinned = 1;
    }

    pool->type = mmPoolRing;
    pool->bufSize = (bufferSize + pageSize - 1) / pageSize;
    pool->numBufs = pool->size / pool->bufSize;
    pool->buffers = (drmMMBuf *) calloc(pool->numBufs, sizeof(drmMMBuf));
    if (!pool->buffers) {
	drmMsg("drmMMAllocBufferPool: Could not allocate buffers\n");
	drmMMDestroyBufferPool(drmFD, pool);
	return -1;
    }
    pool->head = 0;
    pool->tail = 0;
    pool->free = pool->numBufs;
    return 0;
}

int
drmMMDestroyBufferPool(int drmFD, drmMMPool * pool)
{
    drm_ttm_arg_t arg;

    if (pool->buffers)
	free(pool->buffers);

    if (drmUnmap(pool->virtual, pool->size)) {
	drmMsg("drmMMAllocBufferPool: Could not unmap pool: %s\n",
	    strerror(errno));
    }
    arg.op = ttm_remove;
    arg.handle = pool->kernelPool;
    arg.num_bufs = 0;

/*
 * Note that the kernel will wait for fences before
 * pool buffers are removed from TT.
 */

    if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
	drmMsg("drmMMDestroyBufferPool: Could not destroy pool: %s\n",
	    strerror(errno));
	return -errno;
    }
    return 0;
}

int
drmMMMapBuffer(int drmFD, drmMMBuf * buf)
{

    int ret;
    drmAddress *addr;

    buf->virtual = NULL;

    if (buf->pool) {
	buf->virtual = buf->pool->virtual + buf->poolOffs;
	return 0;
    }

    if (buf->flags & DRM_MM_VRAM) {
	drmMsg("drmMMMapBuffer: VRAM not implemented yet.\n");
	return -1;
    }

    ret = drmMap(drmFD, buf->kernelPool, buf->size, addr);
    if (!ret)
	buf->virtual = *addr;

    buf->mapped = 1;
    return ret;
}

int
drmMMUnmapBuffer(int drmFD, drmMMBuf * buf)
{
    int ret;

    if (buf->pool) {
	buf->virtual = NULL;
	return 0;
    }

    ret = drmUnmap(buf->virtual, buf->size);
    if (!ret)
	buf->virtual = NULL;

    return ret;
}

static int
drmFreeRetired(int drmFD, drmMMPool * pool, int lookAhead)
{
    int retired;
    int ret;
    drmMMBuf *curBuf;

    curBuf = pool->buffers + pool->tail;

    if (curBuf->fenced) {
	ret = drmTestFence(drmFD, curBuf->fence, 0, &retired);
	if (ret)
	    return ret;
    }

    if (curBuf->fenced && !retired) {
	if (lookAhead > pool->numBufs - 1)
	    lookAhead = pool->numBufs - 1;

	if (pool->tail + lookAhead >= pool->numBufs)
	    lookAhead -= pool->numBufs;

	ret = drmWaitFence(drmFD, (curBuf + lookAhead)->fence);
	retired = 1;
	if (ret)
	    return ret;
    }

    while (retired) {
	curBuf->fenced = 0;
	pool->free++;
	pool->tail++;
	if (pool->tail >= pool->numBufs)
	    pool->tail = 0;

	curBuf = pool->buffers + pool->tail;
	if (!curBuf->fenced)
	    break;
	drmTestFence(drmFD, curBuf->fence, 0, &retired);
    }

    return 0;
}

drmMMBuf *
drmMMAllocBuffer(int drmFD, drmMMPool * pool, int lookAhead)
{
    drmMMBuf *tmp;

    if (pool) {

	while (!pool->free) {

	    if (drmFreeRetired(drmFD, pool, lookAhead)) {
		drmMsg
		    ("drmMMAllocBuffer: Could not free retired buffers: %s\n",
		    strerror(errno));
		return NULL;
	    }
	}
	if (tmp->inUse) {
	    fprintf(stderr,
		"drmMMAllocBuffer: APPLICATION BUG. Full ring is in use.\n");
	    tmp->inUse = 0;
	}
	tmp = pool->buffers + pool->head;
	tmp->pool = pool;
	tmp->poolOffs = pool->head * pool->bufSize;
	tmp->flags = pool->flags & ~DRM_MM_NEW;
	pool->free--;
	if (pool->head >= pool->numBufs) {
	    pool->head = 0;
	}

    } else {

	tmp = (drmMMBuf *) malloc(sizeof(*tmp));
	if (!tmp) {
	    drmMsg("drmMMAllocBuffer: Could not free retired buffers: %s\n",
		strerror(errno));
	    return NULL;
	}
	tmp->fenced = 0;
	tmp->pool = 0;
	tmp->flags = DRM_MM_NEW;

    }
    tmp->inUse = 1;
    return tmp;
}

int
drmFreeBuffer(int drmFD, drmMMBuf * buf)
{
    if (buf && !buf->pool) {
	if (buf->flags != DRM_MM_NEW) {
	    drm_ttm_arg_t arg;

	    arg.op = ttm_remove;
	    arg.num_bufs = 0;
	    arg.handle = buf->kernelPool;

	    /*
	     * FIXME: This will hang until the buffer's fence is retired.
	     * Needs a kernel side fix.
	     */

	    if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
		drmMsg("drmFreeBuffer: Could not destroy buffer: %s\n",
		    strerror(errno));
		return -errno;
	    }
	}
    } else if (buf->pool) {
	buf->inUse = 0;
    }
}

/*
 * Determine if we need to call kernel to validate this buffer.
 * -1 means never validate, Buffer is pinned.
 * 0 means no, but if other buffers are validated, validate this one to.
 * 1 means yes.
 *
 * We need to call kernel if either of the below is true. 
 * 1) The buffer is new and has never been validated before. (DRM_MM_NEW).
 * 2) There has been a huge number of validations since this buffer was last 
 *    validated.
 * 3) The kernel has evicted buffers since this one was last validated.
 * 4) We can't read the MM Sarea.
 */

static inline int
drmCheckValidation(unsigned flags, drmMMBuf * buf)
{
    if (buf->pool && buf->pool->pinned)
	return -1;
    if (buf->flags & DRM_MM_NEW)
	return 1;
    if (!drmMMSarea)
	return 1;
    if ((drmMMSarea->validation_seq - buf->lastValSeq) >= (DRM_MM_CLEAN - 1))
	return 1;
    if ((drmMMSarea->evict_tt_seq - buf->lastValSeq - 1) <= DRM_MM_WRAP)
	return 1;
    return 0;
}

int
drmMMValidateBuffers(int drmFD, drmMMBufList * head)
{
    int needsValid = 0;
    drmMMBufList *cur = head->next;
    drm_ttm_buf_arg_t *vl;
    drm_ttm_buf_arg_t *curBArg;
    drm_ttm_arg_t arg;
    drmMMBuf *buf;
    unsigned pageSize = getpagesize();
    int ret;

    while (cur != head) {
	if (drmCheckValidation(cur->flags, cur->buf) == 1)
	    needsValid++;
	cur = cur->next;
    }

    if (!needsValid)
	return 0;

    vl = (drm_ttm_buf_arg_t *) calloc(needsValid, sizeof(*vl));
    if (!vl) {
	drmMsg("drmMMValidateBuffers: Could not allocate memory\n");
	return -1;
    }

    curBArg = vl;
    cur = head->next;

    while (cur != head) {
	buf = cur->buf;

	if (buf->pool && buf->pool->pinned)
	    continue;

	curBArg->op = (cur->flags & DRM_MM_TT) ? ttm_validate : ttm_unbind;
	curBArg->ttm_handle = buf->kernelPool;
	curBArg->region_handle = buf->kernelBuf;
	curBArg->ttm_page_offset = (buf->pool) ? buf->poolOffs / pageSize : 0;
	curBArg->num_pages = buf->size / pageSize;
	curBArg->flags = cur->flags;
	curBArg->next = curBArg + 1;
	curBArg->fence_type = cur->fenceType;
	cur = cur->next;
	curBArg++;
    }

    arg.op = ttm_bufs;
    arg.first = vl;
    arg.num_bufs = needsValid;

    if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
	drmMsg("drmMMValidateBuffers: failed: %s\n", strerror(errno));
	free(vl);
	return -errno;
    }

    curBArg = vl;
    cur = head->next;
    ret = 0;

    while (cur != head) {
	buf = cur->buf;

	if (buf->pool && buf->pool->pinned)
	    continue;

	buf->lastValSeq = arg.val_seq;
	buf->flags = curBArg->flags;
	buf->err = curBArg->ret;
	if (buf->err) {
	    ret = buf->err;
	}
	buf->offset = curBArg->aper_offset * pageSize;
	cur = cur->next;
	curBArg++;
    }
    free(vl);
    if (ret) {
	drmMsg("drmMMValidateBuffers: At least one buffer"
	    " validation failed: %s\n", strerror(ret));
    }
    return ret;
}

int
drmMMFenceBuffers(int drmFD, drmMMBufList * head)
{
    unsigned fenceType;
    drmFence fence;
    int ret;
    drmMMBufList *list = head->next;

    if (!kernelOnEmitFence) {
	while (list != head) {
	    fenceType = list->fenceType & DRM_FENCE_MASK;
	    list->buf->fence.fenceType = fenceType;
	    list->buf->fence.fenceSeq = drmMMSarea->emitted[fenceType];
	    list = list->next;
	}
	return 0;
    } else {
	unsigned char isEmitted[DRM_FENCE_TYPES];
	unsigned fenceSeqs[DRM_FENCE_TYPES];

	memset(isEmitted, 0, numFenceTypes);

	while (list != head) {

	    fenceType = list->fenceType & DRM_FENCE_MASK;

	    if (!isEmitted[fenceType]) {
		ret = drmEmitFence(drmFD, fenceType, &fence);

		if (ret) {
		    drmMsg("drmFenceBuffers: Emit fence failed: %s\n",
			strerror(errno));
		    return -errno;
		}

		isEmitted[fenceType] = 1;
		fenceSeqs[fenceType] = fence.fenceSeq;
		list->buf->fence = fence;
	    } else {
		list->buf->fence.fenceType = fenceType;
		list->buf->fence.fenceSeq = fenceSeqs[fenceType];
	    }
	    list = list->next;
	}
    }
    return 0;
}

void
drmMMClearBufList(drmMMBufList * head)
{
    drmMMBufList *cur = head->next;
    drmMMBufList *freeList;

    freeList = head->next;
    if (freeList == head)
	return;

    head->prev->next = head->free;
    head->prev = head;
    head->next = head;
    head->free = freeList;
}

void
drmMMInitListHead(drmMMBufList * head)
{
    head->prev = head;
    head->next = head;
    head->free = NULL;
}

void
drmMMFreeBufList(drmMMBufList * head)
{
    drmMMBufList *cur;
    drmMMBufList *tmp;

    drmMMClearBufList(head);
    cur = head->free;
    while (cur) {
	tmp = cur->next;
	free(cur);
	cur = tmp;
    }
}

int
drmMMBufListAdd(drmMMBufList * head, drmMMBuf * buf, unsigned fenceType,
    unsigned flags)
{
    drmMMBufList *cur;
    drmMMBufList *tmp;

    if (head && head->free) {
	cur = head->free;
	head->free = cur->next;
	cur->free = NULL;
	cur->next = NULL;
	cur->prev = NULL;
    } else {
	cur = (drmMMBufList *) malloc(sizeof(*cur));
	if (!cur) {
	    drmMsg("drmMMBufListAdd: Alloc buffer list entry failed: %s\n");
	    return -1;
	}
	cur->free = NULL;
    }

    if (!head) {
	head = cur;
	head->prev = head;
	head->next = head;
    } else {
	tmp = head->prev;
	head->prev = cur;
	cur->next = head;
	cur->prev = tmp;
	tmp->next = cur;
    }

    cur->buf = buf;
    cur->fenceType = fenceType;
    cur->flags = flags;
    return 0;
}
