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
 * TODO Urgent:
 * 
 * Clean up user pool handling in this file. The code is messy.
 * Fix the kernel hashing mechanism.
 * Fence pinned regions.
 * Kernel garbage eviction, icluding keeping track of validations.
 */

/*
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
# define inline
# define sched_yield()
# ifndef XFree86LOADER
#  include <sys/mman.h>
# endif
#else
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stddef.h>
#endif

typedef struct _drmMMBlock
{
    unsigned long offset;
    drm_handle_t kernelPool;
    drm_handle_t kernelBuf;
    unsigned lastValSeq;
    int hasKernelBuf;
    int inUse;
    int fenced;
    drmFence fence;
    DrmMMListHead listHead;
} drmMMBlock;

typedef struct _drmMMBufList
{
    struct _drmMMBufList *next, *prev, *free;
    drmMMBuf *buf;
    unsigned fenceType;
    unsigned long *offsetReturn;
    unsigned *memtypeReturn;
    unsigned flags;
} drmMMBufList;

/*
 * static kernel info. Note that the mm sarea is mapped read-only from userspace
 * which ensures both security and also data integrity as long as read and write
 * operations are atomic. Thus we need not protect it with a lock in userspace.
 * The kernel protects write-operations with dev->struct_sem.
 */

static struct
{
    drm_handle_t sareaHandle;
    volatile drm_mm_sarea_t *sarea;
    int emitFence;
    unsigned fenceTypes;
    unsigned pageSize;
} drmMMKI = {
.sarea = NULL};

#define drmMMCheckInit(__drmFD)			\
    do{						\
	if (!drmMMKI.sarea)			\
	    drmDoMMCheckInit(__drmFD);		\
    } while(0)

static int
drmDoMMCheckInit(int drmFD)
{
    drm_mm_init_arg_t ma;
    drmAddress sa;
    int ret;

    ma.req.op = mm_query;
    ret = ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma);

    if (ret) {
	drmMsg("drmDoMMCheckInit: failed: %s\n", strerror(errno));
	return -errno;
    }

    drmMMKI.sareaHandle = ma.rep.mm_sarea;
    if (drmMap(drmFD, drmMMKI.sareaHandle, DRM_MM_SAREA_SIZE, &sa)) {
	ret = -errno;
	drmMsg("drmMMInit: could not map mm sarea: %s\n", strerror(errno));
	return ret;
    }
    drmMMKI.sarea = (drm_mm_sarea_t *) sa;
    drmMMKI.emitFence = ma.rep.kernel_emit;
    drmMMKI.fenceTypes = ma.rep.fence_types;
    drmMMKI.pageSize = getpagesize();
    return 0;
}

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

    ma.req.vr_offset_lo = vRamOffs & 0xFFFFFFFFU;
    ma.req.vr_size_lo = vRamSize & 0xFFFFFFFFU;
    ma.req.tt_p_offset_lo = ttPageOffs & 0xFFFFFFFFU;
    ma.req.tt_p_size_lo = ttNumPages & 0xFFFFFFFFU;
    if (sizeof(vRamOffs) == 8) {
	int shift = 32;

	ma.req.vr_offset_hi = vRamOffs >> shift;
	ma.req.vr_size_hi = vRamSize >> shift;
	ma.req.tt_p_offset_hi = ttPageOffs >> shift;
	ma.req.tt_p_size_hi = ttNumPages >> shift;
    }
    ma.req.op = mm_init;

    ret = ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma);

    if (ret) {
	drmMsg("drmMMInit: failed: %s\n", strerror(errno));
	return -errno;
    }

    drmMMKI.sareaHandle = ma.rep.mm_sarea;
    if (drmMap(drmFD, drmMMKI.sareaHandle, DRM_MM_SAREA_SIZE, &sa)) {
	ret = -errno;
	drmMsg("drmMMInit: could not map mm sarea: %s\n", strerror(errno));
	ma.req.op = mm_takedown;
	ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma);
	return ret;
    }
    drmMMKI.sarea = (drm_mm_sarea_t *) sa;
    drmMMKI.sareaHandle = ma.rep.mm_sarea;
    drmMMKI.emitFence = ma.rep.kernel_emit;
    drmMMKI.fenceTypes = ma.rep.fence_types;
    drmMMKI.pageSize = getpagesize();

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

    if (!drmMMKI.sarea)
	return 0;

    drmUnmap((void *)drmMMKI.sarea, DRM_MM_SAREA_SIZE);
    drmMMKI.sarea = NULL;

    ma.req.op = mm_takedown;
    if (ioctl(drmFD, DRM_IOCTL_MM_INIT, &ma)) {
	drmMsg("drmMMTakedown: failed: %s\n", strerror(errno));
	ret = -errno;
    }

    return ret;
}

int
drmEmitFence(int drmFD, unsigned fenceType, drmFence * fence)
{
    drm_fence_arg_t arg;

    drmMMCheckInit(drmFD);

    if (!drmMMKI.emitFence) {
	fence->fenceType = fenceType;
	fence->fenceSeq = drmMMKI.sarea->emitted[fenceType & DRM_FENCE_MASK];
	return 0;
    }

    arg.req.op = emit_fence;
    arg.req.fence_type = fenceType;

    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	drmMsg("drmEmitFence: failed: %s\n", strerror(errno));
	return -errno;
    }

    fence->fenceType = fenceType;
    fence->fenceSeq = arg.rep.fence_seq;

    return 0;
}

int
drmWaitFence(int drmFD, drmFence fence)
{
    drm_fence_arg_t arg;
    int ret;

    drmMMCheckInit(drmFD);

    if ((drmMMKI.sarea->retired[fence.fenceType & DRM_FENCE_MASK] -
	    fence.fenceSeq) < DRM_MM_WRAP)
	return 0;

    /*
     * Ugly, but necessary for multiple DRI clients with the userspace
     * arbitration scheme. We need a scheduler!!.
     */

#if 0
    sched_yield();

    if ((drmMMKI.sarea->retired[fence.fenceType & DRM_FENCE_MASK] -
	    fence.fenceSeq) < DRM_MM_WRAP)
	return 0;
#endif

    do {
	arg.req.op = wait_fence;
	arg.req.fence_type = fence.fenceType;
	arg.req.fence_seq = fence.fenceSeq;
	ret = ioctl(drmFD, DRM_IOCTL_FENCE, &arg);
    } while (ret != 0 && errno == EAGAIN);

    if (ret) {
	drmMsg("drmWaitFence: failed: %s\n", strerror(errno));
	return -errno;
    }

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

    drmMMCheckInit(drmFD);

    if ((drmMMKI.sarea->retired[fence.fenceType & DRM_FENCE_MASK] -
	    fence.fenceSeq) < DRM_MM_WRAP) {
	*retired = 1;
	return 0;
    }

    arg.req.op = test_fence;
    arg.req.fence_type = fence.fenceType;
    arg.req.fence_seq = fence.fenceSeq;

    if (ioctl(drmFD, DRM_IOCTL_FENCE, &arg)) {
	return -errno;
    }
    *retired = arg.rep.ret;

    return 0;
}

int
drmMMAllocBufferPool(int drmFD, MMPoolTypes type, unsigned fence_type,
    unsigned flags, unsigned long size, drmSize bufferSize, drmMMPool * pool)
{
    int i;
    drm_ttm_arg_t arg;

    drmMMCheckInit(drmFD);

    if (type != mmPoolRing) {
	drmMsg("drmMMAllocBufferPool: Unimplemented pool type\n");
	return -1;
    }

    if (!(flags & (DRM_MM_VRAM | DRM_MM_TT))) {
	drmMsg("drmMMAllocBufferPool: Pool not in VRAM or TT\n");
	return -1;
    }

    if ((flags & DRM_MM_VRAM)) {
	drmMsg("drmMMAllocBufferPool: VRAM not implemented yet\n");
	return -1;
    }

    size =
	drmMMKI.pageSize * ((size + drmMMKI.pageSize - 1) / drmMMKI.pageSize);

    arg.op = ttm_add;
    arg.size = size;
    arg.num_bufs = 0;
    pool->bufSize = 0;

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
    if (flags & DRM_MM_NO_EVICT) {
	drm_ttm_buf_arg_t ba;
	int ret;

	ba.op = ttm_validate;
	ba.ttm_handle = arg.handle;
	ba.flags = flags | DRM_MM_NEW;
	ba.ttm_page_offset = 0;
	ba.num_pages = size / drmMMKI.pageSize;
	ba.next = NULL;
	ba.fence_type = 0;
	arg.first = &ba;
	arg.num_bufs = 1;
	arg.op = ttm_bufs;

	if (((ret = ioctl(drmFD, DRM_IOCTL_TTM, &arg))) || ba.ret) {
	    drmMsg("drmMMAllocBufferPool: Could not validate pool.\n");
	    arg.op = ttm_remove;
	    ioctl(drmFD, DRM_IOCTL_TTM, &arg);
	    return ((ret) ? ret : ba.ret);
	}

	pool->offset = ba.aper_offset * drmMMKI.pageSize;
	pool->kernelBuf = ba.region_handle;
	pool->pinned = 1;
	flags = (flags & ~DRM_MM_KERNEL_MASK) |
	    (ba.flags & DRM_MM_KERNEL_MASK);
    }
    pool->size = size;
    pool->type = mmPoolRing;
    pool->flags = flags;
    pool->bufSize = ((bufferSize + drmMMKI.pageSize - 1) / drmMMKI.pageSize)
	* drmMMKI.pageSize;
    pool->numBufs = pool->size / pool->bufSize;
    pool->blocks =
	(drmMMBlock *) calloc(pool->numBufs, sizeof(*pool->blocks));
    if (!pool->blocks) {
	drmMsg("drmMMAllocBufferPool: Could not allocate buffers\n");
	drmMMDestroyBufferPool(drmFD, pool);
	return -1;
    }

    DRMINITLISTHEAD(&pool->freeStack);
    DRMINITLISTHEAD(&pool->lruList);

    for (i = 0; i < pool->numBufs; ++i) {
	DRMLISTADD(&(pool->blocks[i].listHead), &(pool->freeStack));
    }

    return 0;
}

int
drmMMDestroyBufferPool(int drmFD, drmMMPool * pool)
{
    drm_ttm_arg_t arg;

    if (pool->blocks)
	free(pool->blocks);

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

static unsigned char *
drmPoolVirt(const drmMMPool * pool, unsigned handle)
{
    if (pool->type == mmPoolRing)
	return pool->virtual + handle * pool->bufSize;
    return NULL;
}

static unsigned long
drmPoolOffs(const drmMMPool * pool, unsigned handle)
{
    if (pool->type == mmPoolRing)
	return pool->offset + handle * pool->bufSize;
    return 0;
}

void *
drmMMMapBuffer(int drmFD, drmMMBuf * buf)
{

    int ret;
    drmAddress addr;

    if (buf->block && buf->block->fenced) {
	ret = -EINTR;
	while (ret == -EINTR) {
	    ret = drmWaitFence(drmFD, buf->block->fence);
	}
	if (ret) {
	    return NULL;
	}
    }

    /*
     * FIXME: Ok for now, since the only shared buffers
     * we use now (front, back, depth) are really mapped elsewhere.
     */

    if (buf->flags & DRM_MM_SHARED)
	return buf->virtual;

    buf->virtual = NULL;
    buf->mapped = 0;
    if (buf->pool) {
	buf->virtual = drmPoolVirt(buf->pool, buf->poolHandle);
	buf->mapped = 1;
	return buf->virtual;
    }

    if ((buf->flags & DRM_MM_MEMTYPE_MASK) == DRM_MM_VRAM) {
	drmMsg("drmMMMapBuffer: VRAM not implemented yet.\n");
	return NULL;
    }

    ret = drmMap(drmFD, buf->block->kernelPool, buf->size, &addr);
    if (!ret)
	buf->virtual = addr;
    else
	buf->virtual = NULL;

    buf->mapped = (!ret);
    return buf->virtual;
}

int
drmBufIsBusy(int drmFD, drmMMBuf * buf)
{
    int retired;
    int ret;

    if (buf->block && buf->block->fenced) {
	ret = drmTestFence(drmFD, buf->block->fence, 1, &retired);
	return (!retired || ret);
    } else {
	return 0;
    }
}

int
drmBufWaitBusy(int drmFD, drmMMBuf * buf)
{
    if (buf->block && buf->block->fenced)
	return drmWaitFence(drmFD, buf->block->fence);
    else
	return 0;
}

int
drmMMUnmapBuffer(int drmFD, drmMMBuf * buf)
{
    int ret;

    if (buf->flags & DRM_MM_SHARED)
	return 0;

    if (buf->pool) {
	buf->virtual = NULL;
	buf->mapped = 0;
	return 0;
    }

    ret = drmUnmap(buf->virtual, buf->size);
    if (!ret) {
	buf->mapped = 0;
	buf->virtual = NULL;
    }

    return ret;
}

int
drmMMInitBuffer(int drmFD, unsigned flags, unsigned alignment, drmMMBuf * buf)
{
    buf->flags = flags;
    buf->block = NULL;
    return 0;
}

static int
drmMMFreeLRU(int drmFD, drmMMPool * pool, int mode)
{
    DrmMMListHead *list, *next;
    drmMMBlock *block;
    int ret;
    int retired;

    list = pool->lruList.next;
    next = list->next;

    if (mode == 1 && list != &pool->lruList) {
	block = DRMLISTENTRY(drmMMBlock, list, listHead);
	ret = drmWaitFence(drmFD, block->fence);
	if (ret)
	    return ret;
    }

    for (; list != &pool->lruList; list = next, next = list->next) {
	block = DRMLISTENTRY(drmMMBlock, list, listHead);
	ret = drmTestFence(drmFD, block->fence, 0, &retired);
	if (ret)
	    return ret;
	if (!retired) {
	    return 0;
	}
	DRMLISTDEL(list);
	block->fenced = 0;
	if (block->inUse) {
	    DRMLISTADDTAIL(list, &pool->freeStack);
	} else {
	    DRMLISTADD(list, &pool->freeStack);
	}
    }
    return 0;
}

static void
drmMMFreePoolBuffer(drmMMBuf * buf)
{
    drmMMBlock *block = buf->block;

    block->inUse = 0;
    if (!block->fenced) {
	DRMLISTDEL(&block->listHead);
	DRMLISTADD(&block->listHead, &buf->pool->freeStack);
    }
    buf->block = NULL;
}

static int
drmMMAllocPoolBuffer(int drmFD, unsigned size,
    drmMMPool * pool, drmMMBuf * buf)
{
    drmMMBlock *block;
    int ret;
    DrmMMListHead *list;

    if (size != pool->bufSize) {
	drmMsg("drmMMAllocBuffer: Pool buffer size doesn't match "
	    "requested size.\n");
	return -1;
    }

    do {
	ret = drmMMFreeLRU(drmFD, pool, 0);
	if (ret)
	    return ret;

	list = pool->freeStack.next;

	while (list == &pool->freeStack) {
	    if (pool->lruList.next == &pool->lruList) {
		drmMsg("Lost all batchbuffers. You've encountered a bug.\n");
		return -1;
	    }
	    ret = drmMMFreeLRU(drmFD, pool, 1);
	    if (ret)
		return ret;
	}

	block = DRMLISTENTRY(drmMMBlock, list, listHead);
    } while (block->inUse);

    DRMLISTDELINIT(list);

    block->kernelPool = pool->kernelPool;
    block->fenced = 0;
    block->inUse = 1;

    buf->poolHandle = block - pool->blocks;
    buf->block = block;
    buf->size = pool->bufSize;
    buf->mapped = 0;
    buf->pool = pool;

    if (pool->pinned) {
	buf->flags = pool->flags & ~DRM_MM_NEW;
	buf->offset = drmPoolOffs(pool, buf->poolHandle);
    } else {
	if (buf->block->hasKernelBuf) {
	    buf->flags = pool->flags & ~DRM_MM_NEW;
	    buf->offset = buf->block->offset;
	} else {
	    buf->flags = pool->flags | DRM_MM_NEW;
	}
    }
    return 0;
}

int
drmMMAllocBuffer(int drmFD, unsigned size,
    drmMMPool * pool, int lookAhead, drmMMBuf * buf)
{
    drmMMBlock *block;
    DrmMMListHead *head;

    buf->flags |= DRM_MM_NEW;

    if (buf->block) {
	fprintf(stderr,
	    "drmMMAllocBuffer: Trying to allocate a non-freed buffer.\n");
	return -1;
    }

    if (pool) {

	return drmMMAllocPoolBuffer(drmFD, size, pool, buf);

    } else if (!(buf->flags & DRM_MM_SHARED)) {
	drm_ttm_arg_t arg;

	block = (drmMMBlock *) calloc(1, sizeof(*block));
	if (!block) {
	    drmMsg("drmMMAllocBuffer: Could not alloc info block.\n");
	    return -1;
	}
	size = ((size + drmMMKI.pageSize - 1) / drmMMKI.pageSize)
	    * drmMMKI.pageSize;

	arg.op = ttm_add;
	arg.size = size;
	arg.num_bufs = 0;
	if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
	    drmMsg("drmMMAllocBuffer: %s\n", strerror(errno));
	    free(block);
	    return -errno;
	}

	buf->pool = NULL;
	buf->flags |= DRM_MM_NEW;
	buf->block = block;
	block->kernelPool = arg.handle;
	buf->size = size;
    } else {

	block = (drmMMBlock *) calloc(1, sizeof(*block));

	if (!block) {
	    drmMsg("drmMMAllocBuffer: Could not alloc info block.\n");
	    return -1;
	}
	buf->block = block;
	buf->pool = NULL;
	buf->flags &= ~DRM_MM_NEW;

    }
    buf->block->fenced = 0;
    buf->block->inUse = 1;
    buf->mapped = 0;

    return 0;
}

int
drmMMFreeBuffer(int drmFD, drmMMBuf * buf)
{

    if (buf && !buf->block)
	return 0;

    if (buf && !buf->pool) {
	if (!(buf->flags & (DRM_MM_NEW | DRM_MM_SHARED))) {
	    drm_ttm_arg_t arg;

	    arg.op = ttm_remove;
	    arg.num_bufs = 0;
	    arg.handle = buf->block->kernelPool;

	    if (ioctl(drmFD, DRM_IOCTL_TTM, &arg)) {
		drmMsg("drmFreeBuffer: Could not destroy buffer: %s\n",
		    strerror(errno));
		return -errno;
	    }
	}
    } else if (buf->pool) {
	drmMMFreePoolBuffer(buf);
    }
    return 0;
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
    if (!buf->block)
	return -1;
    if (buf->pool && buf->pool->pinned)
	return -1;
    if (buf->flags & DRM_MM_SHARED)
	return -1;
    if (buf->flags & DRM_MM_NEW)
	return 1;
    if (!FLAGS_COMPATIBLE(buf->flags, flags))
	return 1;
    if ((drmMMKI.sarea->validation_seq - buf->block->lastValSeq) >=
	(DRM_MM_CLEAN - 1))
	return 1;
    if ((drmMMKI.sarea->evict_tt_seq - buf->block->lastValSeq - 1) <=
	DRM_MM_WRAP)
	return 1;
    return 1;
}

static int
drmMMReturnOffsets(drmMMBufList * head)
{
    drmMMBufList *cur;

    cur = head->next;
    while (cur != head) {
	if (cur->offsetReturn) {
	    *(cur->offsetReturn) = cur->buf->offset;
	}
	if (cur->memtypeReturn) {
	    *(cur->memtypeReturn) = cur->flags & DRM_MM_MEMTYPE_MASK;
	}
	cur = cur->next;
    }
    return 0;
}

/**
 * 
 * The user may request a buffer state change by the flags in the
 * validation list. The validate function checks if it needs to
 * call the kernel as a result of this. Pooled buffers list flag arguments are
 * ignored. They are set to the pool flags. 
 *
 */

int
drmMMValidateBuffers(int drmFD, drmMMBufList * head)
{
    int needsValid = 0;
    int doValid = 0;
    int tmp;
    int ret;
    drmMMBufList *cur = head->next;
    drm_ttm_buf_arg_t *vl;
    drm_ttm_buf_arg_t *curBArg;
    drm_ttm_arg_t arg;
    drmMMBuf *buf;
    drmMMBlock *block;
    unsigned flags;

    drmMMCheckInit(drmFD);

    while (cur != head) {
	buf = cur->buf;
	flags = cur->flags;
	if (buf->pool)
	    flags = buf->pool->flags;

	tmp = drmCheckValidation(flags, cur->buf);
	if (tmp >= 0)
	    needsValid++;
	if (tmp == 1)
	    doValid = 1;
	cur = cur->next;
    }

    if (!doValid) {
	drmMMReturnOffsets(head);
	return 0;
    }

    vl = NULL;
    if (needsValid) {
	vl = (drm_ttm_buf_arg_t *) calloc(needsValid, sizeof(*vl));

	if (!vl) {
	    drmMsg("drmMMValidateBuffers: Could not allocate memory\n");
	    return -1;
	}
    }

    curBArg = vl;
    cur = head->next;
    while (cur != head) {
	buf = cur->buf;
	block = buf->block;
	flags = cur->flags;
	if (buf->pool)
	    flags = buf->pool->flags;
	if (drmCheckValidation(flags, buf) != -1) {
	    flags = (flags & ~DRM_MM_NEW) | (buf->flags & DRM_MM_NEW);
	    curBArg->op = ttm_validate;
	    curBArg->ttm_handle = block->kernelPool;
	    curBArg->region_handle = block->kernelBuf;
	    curBArg->ttm_page_offset =
		(buf->pool) ? (buf->poolHandle * buf->pool->bufSize) /
		drmMMKI.pageSize : 0;
	    curBArg->num_pages = buf->size / drmMMKI.pageSize;

	    curBArg->flags = flags & DRM_MM_KERNEL_MASK;
	    curBArg->next = curBArg + 1;
	    curBArg->fence_type = cur->fenceType;
	    curBArg++;
	}
	cur = cur->next;
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
	block = buf->block;

	flags = cur->flags;
	if (buf->pool)
	    flags = buf->pool->flags;

	if (drmCheckValidation(flags, cur->buf) != -1) {
	    block->lastValSeq = arg.val_seq;
	    buf->flags = (buf->flags & ~DRM_MM_KERNEL_MASK) |
		(curBArg->flags & DRM_MM_KERNEL_MASK);
	    buf->err = curBArg->ret;
	    if (buf->err) {
		ret = buf->err;
	    }
	    block->kernelBuf = curBArg->region_handle;
	    block->hasKernelBuf = 1;
	    block->offset = curBArg->aper_offset * drmMMKI.pageSize;
	    buf->offset = block->offset;
	    curBArg++;
	}
	cur = cur->next;
    }

    if (vl)
	free(vl);
    if (ret) {
	drmMsg("drmMMValidateBuffers: At least one buffer"
	    " validation failed: %s\n", strerror(-ret));
    }

    drmMMReturnOffsets(head);
    return ret;
}

int
drmMMFenceBuffers(int drmFD, drmMMBufList * head)
{
    unsigned fenceType;
    drmFence fence;
    int ret;
    drmMMBufList *list = head->next;
    drmMMBuf *buf;

    drmMMCheckInit(drmFD);

    if (!drmMMKI.emitFence) {
	while (list != head) {
	    buf = list->buf;
	    if (buf->block) {
		fenceType = list->fenceType & DRM_FENCE_MASK;
		buf->block->fence.fenceType = fenceType;
		buf->block->fence.fenceSeq =
		    drmMMKI.sarea->emitted[fenceType];
		buf->block->fenced = 1;
		if (buf->pool) {
		    DRMLISTADDTAIL(&buf->block->listHead,
			&buf->pool->lruList);
		}
	    }
	    list = list->next;
	}
    } else {
	unsigned char isEmitted[DRM_FENCE_TYPES];
	unsigned fenceSeqs[DRM_FENCE_TYPES];

	memset(isEmitted, 0, drmMMKI.fenceTypes);

	while (list != head) {

	    buf = list->buf;
	    if (buf->block) {
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
		    buf->block->fence = fence;
		} else {
		    buf->block->fence.fenceType = fenceType;
		    buf->block->fence.fenceSeq = fenceSeqs[fenceType];
		}
		buf->block->fenced = 1;
		if (buf->pool) {
		    DRMLISTADDTAIL(&buf->block->listHead,
			&buf->pool->lruList);
		}
	    }
	    list = list->next;
	}
    }
    return 0;
}

void
drmMMClearBufList(drmMMBufList * head)
{
    drmMMBufList *freeList;

    freeList = head->next;
    if (freeList == head)
	return;

    head->prev->next = head->free;
    head->prev = head;
    head->next = head;
    head->free = freeList;
}

drmMMBufList *
drmMMInitListHead(void)
{
    drmMMBufList *head = calloc(1, sizeof(*head));

    if (!head)
	return NULL;

    head->prev = head;
    head->next = head;
    head->free = NULL;
    return head;
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
drmMMScanBufList(drmMMBufList * head, drmMMBuf * buf)
{
    int count = 0;

    drmMMBufList *cur = head->next;

    while (cur != head) {
	if (cur->buf == buf)
	    return count;
	count++;
	cur = cur->next;
    }
    return -1;
}

int
drmMMBufListAdd(drmMMBufList * head, drmMMBuf * buf, unsigned fenceType,
    unsigned flags, unsigned *memtypeReturn, unsigned long *offsetReturn)
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

    tmp = head->prev;
    head->prev = cur;
    cur->next = head;
    cur->prev = tmp;
    tmp->next = cur;

    cur->buf = buf;
    cur->fenceType = fenceType;
    cur->flags = flags;
    cur->memtypeReturn = memtypeReturn;
    cur->offsetReturn = offsetReturn;
    return 0;
}
