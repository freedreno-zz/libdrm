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

typedef struct drm_fence
{
    unsigned fence_type;
    unsigned fence_seq;
} drm_fence_t;

typedef struct drm_mm_buffer
{
} drm_mm_buf_t;

typedef struct drm_mm_pool
{
} drm_mm_pool_t;

extern int drmMMInit(int drmFD, unsigned long vRamOffs,
    unsigned long vRamSize, unsigned long ttPageOffs,
    unsigned long ttNumPages);

extern int drmMMTakedown(int drmFD);

/*
 * Fence handling. Not sure if the driver needs to see these.
 * The fence typing is optional and for different parts of the 
 * GPU that can run independently. Blit engine, Video blitter, mpeg etc.
 */

extern int drmEmitFence(int drmFD, unsigned fence_type, drm_fence_t * fence);
extern int drmWaitFence(int drmFD, drm_fence_t fence);
extern int drmTestFence(int drmFD, drm_fence_t fence, int really,
    int *retired);

/*
 * A pool is a ring of buffers or a user space memory managed area of
 * buffers. In practice one big TTM. Can be pinned but need not be.
 * Good for batchbuffers. Pinned memory managed areas are good
 * for small pixmaps copied to AGP.
 */

extern int drmMMAllocBufferPool(int drmFD, unsigned flags, drmSize size,
    drmSize bufferSize, drm_mm_pool_t * pool);
extern int drmMMDestroyBufferPool(int drmFD, drm_mm_pool_t * pool);

/*
 * These should translate more or less directly. Pool can be NULL, 
 * and numBufs can be 1.
 */

extern int drmMMAllocBuffers(int drmFD, unsigned flags, drm_mm_pool_t * pool,
    int numBufs, drm_mm_buf_t * bufs);
extern int drmMMFreeBuffers(int drmFD, int numBufs, drm_mm_buf_t * bufs);

/*
 * ValidateBuffers only need to call the kernel if there are new buffers or
 * the sarea validate sequence numbers indicate that the buffer might have 
 * been evicted.
 */

extern int drmMMValidateBuffers(int drmFD, unsigned flags, int numBufs,
    drm_mm_buf_t * bufs);

/*
 * Fencebuffers always need to call the kernel, unless we skip fencebuffers and
 * use a scheme where a validateBuffers call fence all previosly unfenced 
 * buffers.
 */

extern int drmMMFenceBuffers(int drmFD, drm_fence_t fence, int numBufs,
    drm_mm_buf_t * bufs);

/*
 * Do not need to call kernel if buffers are from a pool.
 */

extern int drmMMMapBuffer(int drmFD, drm_mm_buf_t * buf, unsigned flags,
    drmAddress * addr);
extern int drmMMUnmapBuffer(int drmFD, drmAddress * addr);

/*
 * Optional for VRAM/EXA.
 */

extern int drmMMBufPixmap(int drmFD, drm_mm_buf_t * buf, unsigned offs,
    drmSize line_len, drmSize pitch, drmSize height);
extern int drmMMBufDirty(int drmFD, drm_mm_buf_t * buf);

#endif
