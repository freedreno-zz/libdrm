/* i810_drm_public.h -- Public header for the i810 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * All rights reserved.
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
 * Authors: Jeff Hartmann <jhartmann@precisioninsight.com>
 *          Keith Whitwell <keithw@precisioninsight.com>
 *
 * $XFree86$
 */

#ifndef _I810_DRM_H_
#define _I810_DRM_H_

typedef struct drm_i810_init {
   	enum {
	   	I810_INIT_DMA = 0x01,
	   	I810_CLEANUP_DMA = 0x02
	} func;
   	int ring_map_idx;
      	int buffer_map_idx;
	int sarea_priv_offset;
   	unsigned long ring_start;
   	unsigned long ring_end;
   	unsigned long ring_size;
	
} drm_i810_init_t;

typedef struct _xf86drmClipRectRec {
   	unsigned short x1;
   	unsigned short y1;
   	unsigned short x2;
   	unsigned short y2;
} xf86drmClipRectRec;

/* Might one day want to support the client-side ringbuffer code again.
 */

#define I810_USE_BATCH 1

#define I810_DMA_BUF_ORDER     12
#define I810_DMA_BUF_SZ        (1<<I810_DMA_BUF_ORDER)
#define I810_DMA_BUF_NR        256

#define I810_NR_SAREA_CLIPRECTS 2

/* Each region is a minimum of 64k, and there are at most 64 of them.
 */
#define I810_NR_TEX_REGIONS 64
#define I810_LOG_MIN_TEX_REGION_SIZE 16

typedef struct {
	unsigned char next, prev; /* indices to form a circular LRU  */
	unsigned char in_use;	/* owned by a client, or free? */
	int age;		/* tracked by clients to update local LRU's */
} i810TexRegion;

typedef struct {
	unsigned int nbox;
	xf86drmClipRectRec boxes[I810_NR_SAREA_CLIPRECTS];

	/* Maintain an LRU of contiguous regions of texture space.  If
	 * you think you own a region of texture memory, and it has an
	 * age different to the one you set, then you are mistaken and
	 * it has been stolen by another client.  If global texAge
	 * hasn't changed, there is no need to walk the list.
	 *
	 * These regions can be used as a proxy for the fine-grained
	 * texture information of other clients - by maintaining them
	 * in the same lru which is used to age their own textures,
	 * clients have an approximate lru for the whole of global
	 * texture space, and can make informed decisions as to which
	 * areas to kick out.  There is no need to choose whether to
	 * kick out your own texture or someone else's - simply eject
	 * them all in LRU order.  
	 */
	i810TexRegion texList[I810_NR_TEX_REGIONS+1]; /* Last elt is sentinal */

        int texAge;		/* last time texture was uploaded */

        int last_enqueue;	/* last time a buffer was enqueued */
	int last_dispatch;	/* age of the most recently dispatched buffer */
	int last_quiescent;     /*  */

	int ctxOwner;		/* last context to upload state */
} drm_i810_sarea_t;


typedef struct {
   	int idx;
	int used;
   	int age;
} drm_i810_general_t;


/* These may be placeholders if we have more cliprects than
 * I810_NR_SAREA_CLIPRECTS.  In that case, idx != real_idx; idx is the
 * number of a placeholder buffer, real_idx is the real buffer to be
 * rendered multiple times.  
 *
 * This is a hack to work around the fact that the drm considers
 * buffers to be only in a single state (ie on a single queue).
 */
typedef struct {
   	int idx;		/* buffer to queue and free on completion */
   	int real_idx;		/* buffer to execute */
	int real_used;		/* buf->used in for real buffer */
	int discard;		/* don't execute the commands */
	int age;
} drm_i810_vertex_t;



#define DRM_IOCTL_I810_INIT    DRM_IOW( 0x40, drm_i810_init_t)
#define DRM_IOCTL_I810_VERTEX  DRM_IOW( 0x41, drm_i810_vertex_t)
#define DRM_IOCTL_I810_DMA     DRM_IOW( 0x42, drm_i810_general_t)
#define DRM_IOCTL_I810_FLUSH   DRM_IO ( 0x43)
#define DRM_IOCTL_I810_GETAGE  DRM_IO ( 0x44)
#endif /* _I810_DRM_H_ */
