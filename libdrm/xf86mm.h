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

#ifndef _XF86MM_H_
#define _XF86MM_H_

#define DRM_INVALID_FENCE_TYPE 0xFFFFFFFFU

#define FLAGS_COMPATIBLE(exist, req) \
((((exist) & (req) & DRM_MM_MEMTYPE_MASK)) && \
 ( ((exist) & (req) & ~DRM_MM_MEMTYPE_MASK) == \
((req) & ~DRM_MM_MEMTYPE_MASK)))

typedef enum
{
    mmPoolRing,
    mmPoolManaged
} MMPoolTypes;

typedef struct _drmMMPool
{
    MMPoolTypes type;
    unsigned char *virtual;
    unsigned flags;
    drm_handle_t kernelPool;
    drm_handle_t kernelBuf;
    int pinned;
    drmSize bufSize;
    unsigned long size;
    unsigned long head;
    unsigned long tail;
    unsigned long free;
    unsigned long numBufs;
    struct _drmMMBuf *buffers;
} drmMMPool;

typedef struct _drmFence
{
    unsigned fenceType;
    unsigned fenceSeq;
} drmFence;

typedef struct _drmMMBuf
{
    drmFence fence;
    int fenced;
    unsigned lastValSeq;
    unsigned flags;
    unsigned err;
    unsigned long size;
    unsigned long offset;
    unsigned char *virtual;
    int mapped;

/* private data */

    drmMMPool *pool;
    unsigned long poolOffs;
    drm_handle_t kernelPool;
    drm_handle_t kernelBuf;
    int inUse;
} drmMMBuf;

typedef struct _drmMMBufList
{
    struct _drmMMBufList *next, *prev, *free;
    drmMMBuf *buf;
    unsigned flags;
    unsigned fenceType;
} drmMMBufList;

extern int drmMMInit(int drmFD, unsigned long vRamOffs,
    unsigned long vRamSize, unsigned long ttPageOffs,
    unsigned long ttNumPages);

extern int drmMMTakedown(int drmFD);

/*
 * Fence handling. Not sure if the driver needs to see these.
 * The fence typing is optional and for different parts of the 
 * GPU that can run independently. Blit engine, video blitter, mpeg etc.
 */

extern int drmEmitFence(int drmFD, unsigned fence_type, drmFence * fence);

extern int drmWaitFence(int drmFD, drmFence fence);
extern int drmTestFence(int drmFD, drmFence fence, int really, int *retired);

/*
 * A pool is a ring of buffers or a user space memory managed area of
 * buffers. In practice one big TTM. Can be pinned but need not be.
 * Good for batchbuffers. Pinned memory managed areas are good
 * for small pixmaps copied to AGP.
 */

extern int drmMMAllocBufferPool(int drmFD, MMPoolTypes type,
    unsigned flags, unsigned long size, drmSize bufferSize, drmMMPool * pool);
extern int drmMMDestroyBufferPool(int drmFD, drmMMPool * pool);

/*
 * These should translate more or less directly. Pool can be NULL, 
 * and numBufs can be 1.
 */

extern int drmMMAllocBuffers(int drmFD, unsigned flags, drmMMPool * pool,
    int numBufs, drmMMBufList * bufs);
extern int drmMMFreeBuffers(int drmFD, int numBufs, drmMMBuf * bufs);

/*
 * ValidateBuffers only need to call the kernel if there are new buffers or
 * the sarea validate sequence numbers indicate that the buffer might have 
 * been evicted.
 */

extern int drmMMValidateBuffers(int drmFD, drmMMBufList * list);

/*
 * Fencebuffers emits a fence and fences the listed user-space buffers.
 * _All_ unfenced kernel buffers will be fenced by this call.
 * If the kernel is never called on fence emission, (intel for example), the
 * unfenced kernel buffers will be fenced with the last emitted fence on the
 * next validate call. That fence will be >= the current fence.
 */

extern int drmMMFenceBuffers(int drmFD, drmMMBufList * list);

/*
 * Do not need to call kernel if buffers are from a pool.
 */

extern int drmMMMapBuffer(int drmFD, drmMMBuf * buf);
extern int drmMMUnmapBuffer(int drmFD, drmMMBuf * buf);

/*
 * Optional for VRAM/EXA.
 */

extern int drmMMBufPixmap(int drmFD, drmMMBuf * buf, unsigned offs,
    drmSize line_len, drmSize pitch, drmSize height);
extern int drmMMBufDirty(int drmFD, drmMMBuf * buf);

#endif
