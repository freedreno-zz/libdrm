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

/*
 * Flags. The flags the kernel know about are masked with
 * DRM_MM_KERNEL_MASK, and are currently:
 *
 * The below flags are masked with DRM_MM_MEMTYPE_MASK.
 * They are used in the buffer flags field to indicate
 * what memory space the buffer is currently in. They
 * may be or-d together if not known. After a validate call,
 * exactly one of them is set. 
 *
 * DRM_MM_VRAM       0x00000001
 * DRM_MM_TT         0x00000002
 * DRM_MM_SYSTEM     0x00000004
 *
 * Indicating that the buffer is not allocated yet, and a map
 * will result in failure. The kernel looks at this flag to
 * determine if it should allocate resources for the buffer
 * during a validate call.
 *
 * DRM_MM_NEW        0x00000100
 *
 * Used by the kernel during validate to make the buffer cached 
 * if possible, even if it resides in  non-system memory. The
 * application can query this flag after a map operation to know
 * if this is really the case.
 *
 * DRM_MM_CACHED     0x00000200
 *
 * The user should indicate these during validate and map operations.
 * Directions are as the buffers are referenced by the CPU. For example,
 * a WRITE buffer can be written to by the CPU and read by the GPU, but not
 * written to by the GPU and Read by the CPU. Buffers marked EXE contains
 * DMA commands and the flag is used for flushing and in the future to protect
 * EXE-buffers from user-space writing after they have been security checked.
 * 
 * DRM_MM_READ       0x00000400
 * DRM_MM_WRITE      0x00000800
 * DRM_MM_EXE        0x00001000
 *
 * Prevent the kernel from moving buffers around. UPLOAD is FROM memory to
 * either TT or VRAM. EVICT is FROM VRAM / TT. NO_MOVE means keep offset constant.
 *
 * DRM_MM_NO_UPLOAD  0x00002000
 * DRM_MM_NO_EVICT   0x00004000
 * DRM_MM_NO_MOVE    0x00008000
 *
 *
 */

/*
 * Flags used by libdrm but not by kernel:
 *
 */

#define DRM_MM_LIBDRM_MASK 0x0F000000U

/*
 * When allocating: This buffer is already allocated by another client, but
 * here are the references, and I want to access it. For now, libdrm never
 * calls the kernel for these buffers, so the other client needs to have them
 * pinned. 
 */

#define DRM_MM_SHARED 0x01000000

/*
 * The following bit-range is available to the user.
 */

#define DRM_MM_USER_MASK 0xF0000000U

#define FLAGS_COMPATIBLE(exist, req) \
((((exist) & (req) & DRM_MM_MEMTYPE_MASK)) && \
 ( ((exist) & (req) & DRM_MM_KERNEL_MASK) == \
((req) & DRM_MM_KERNEL_MASK)))

typedef enum
{
    mmPoolRing,
    mmPoolManaged
} MMPoolTypes;

/*
 * List macros heavily inspired by the Linux kernel
 * list handling. No list looping yet.
 */

typedef struct _drmMMListHead
{
    struct _drmMMListHead *prev;
    struct _drmMMListHead *next;
} DrmMMListHead;

#define DRMINITLISTHEAD(__item)		       \
  do{					       \
    (__item)->prev = (__item);		       \
    (__item)->next = (__item);		       \
  } while (0)

#define DRMLISTADD(__item, __list)			\
  do {						\
    (__item)->prev = (__list);			\
    (__item)->next = (__list)->next;		\
    (__list)->next->prev = (__item);		\
    (__list)->next = (__item);			\
  } while (0)

#define DRMLISTADDTAIL(__item, __list)		\
  do {						\
    (__item)->next = (__list);			\
    (__item)->prev = (__list)->prev;		\
    (__list)->prev->next = (__item);		\
    (__list)->prev = (__item);			\
  } while(0)

#define DRMLISTDEL(__item)			\
  do {						\
    (__item)->prev->next = (__item)->next;	\
    (__item)->next->prev = (__item)->prev;	\
  } while(0)

#define DRMLISTDELINIT(__item)			\
  do {						\
    (__item)->prev->next = (__item)->next;	\
    (__item)->next->prev = (__item)->prev;	\
    (__item)->next = (__item);			\
    (__item)->prev = (__item);			\
  } while(0)

#define DRMLISTENTRY(__type, __item, __field)   \
    ((__type *)(((char *) (__item)) - offsetof(__type, __field)))

struct _drmMMBufInfo;

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
    unsigned long numBufs;
    unsigned long offset;
    struct _drmMMBlock *blocks;
    DrmMMListHead freeStack;
    DrmMMListHead lruList;
} drmMMPool;

typedef struct _drmFence
{
    unsigned fenceType;
    unsigned fenceSeq;
} drmFence;

typedef struct _drmMMBuf
{
    unsigned long client_priv;
    unsigned alignment;
    unsigned flags;
    unsigned err;
    unsigned long size;
    unsigned long offset;
    unsigned long *offset_notify;
    unsigned char *virtual;
    int mapped;

/* private data */
    drmMMPool *pool;
    unsigned poolHandle;

    struct _drmMMBlock *block;
} drmMMBuf;

struct _drmMMBufList;

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
extern int drmBufIsBusy(int drmFD, drmMMBuf * buf);
extern int drmBufWaitBusy(int drmFD, drmMMBuf * buf);

/*
 * A pool is a ring of buffers or a user space memory managed area of
 * buffers. In practice one big TTM. Can be pinned but need not be.
 * Good for batchbuffers. Pinned memory managed areas are good
 * for small pixmaps copied to AGP.
 */

extern int drmMMAllocBufferPool(int drmFD, MMPoolTypes type,
    unsigned fence_type,
    unsigned flags, unsigned long size, drmSize bufferSize, drmMMPool * pool);
extern int drmMMDestroyBufferPool(int drmFD, drmMMPool * pool);

/*
 * InitBuffer is used to initialize a buffer or when flags or alignment changes.
 */

extern int drmMMInitBuffer(int drmFD, unsigned flags, unsigned alignment,
    drmMMBuf * buf);

extern int drmMMAllocBuffer(int drmFD, unsigned size, drmMMPool * pool,
    int lookAhead, drmMMBuf * buf);
extern int drmMMFreeBuffer(int drmFD, drmMMBuf * buf);

/*
 * ValidateBuffers only need to call the kernel if there are new buffers or
 * the sarea validate sequence numbers indicate that the buffer might have 
 * been evicted.
 */

extern int drmMMValidateBuffers(int drmFD, struct _drmMMBufList *list);

/*
 * Fencebuffers emits a fence and fences the listed user-space buffers.
 * _All_ unfenced kernel buffers will be fenced by this call.
 * If the kernel is never called on fence emission, (intel for example), the
 * unfenced kernel buffers will be fenced with the last emitted fence on the
 * next validate call. That fence will be >= the current fence.
 */

extern int drmMMFenceBuffers(int drmFD, struct _drmMMBufList *list);

/*
 * Do not need to call kernel if buffers are from a pool.
 */

extern void *drmMMMapBuffer(int drmFD, drmMMBuf * buf);
extern int drmMMUnmapBuffer(int drmFD, drmMMBuf * buf);

extern int drmMMBufListAdd(struct _drmMMBufList *head, drmMMBuf * buf,
    unsigned fenceType, unsigned flags,
    unsigned *memtypeReturn, unsigned long *offsetReturn);

extern void drmMMFreeBufList(struct _drmMMBufList *head);

extern struct _drmMMBufList *drmMMInitListHead(void);

extern void drmMMClearBufList(struct _drmMMBufList *head);
extern int drmMMScanBufList(struct _drmMMBufList *head, drmMMBuf * buf);

/*
 * Optional for VRAM/EXA.
 */

extern int drmMMBufPixmap(int drmFD, drmMMBuf * buf, unsigned offs,
    drmSize line_len, drmSize pitch, drmSize height);
extern int drmMMBufDirty(int drmFD, drmMMBuf * buf);

#endif
