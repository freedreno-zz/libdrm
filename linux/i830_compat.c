/* i830_dma.c -- DMA support for the I830 -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Rickard E. (Rik) Faith <faith@valinux.com>
 *	    Jeff Hartmann <jhartmann@valinux.com>
 *	    Keith Whitwell <keith@tungstengraphics.com>
 *	    Abraham vd Merwe <abraham@2d3d.co.za>
 *
 */

#define __NO_VERSION__
#include "i830.h"
#include "drmP.h"
#include "drm.h"
#include "i830_drm.h"
#include "i830_drv.h"
#include <linux/interrupt.h>	/* For task queue support */
#include <linux/pagemap.h>     /* For FASTCALL on unlock_page() */
#include <linux/delay.h>

#ifdef DO_MUNMAP_4_ARGS
#define DO_MUNMAP(m, a, l)	do_munmap(m, a, l, 1)
#else
#define DO_MUNMAP(m, a, l)	do_munmap(m, a, l)
#endif

#define I830_BUF_FREE		2
#define I830_BUF_CLIENT		1
#define I830_BUF_HARDWARE      	0

#define I830_BUF_UNMAPPED 0
#define I830_BUF_MAPPED   1

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,4,2)
#define down_write down
#define up_write up
#endif

/* Code to support the original i810-style security model which
 * involved unmapping any buffers not owned by individual clients, in
 * particular buffers submitted to the ring awaiting processing by
 * hardware.
 *
 * New instructions in i830 and above provide an alternate security
 * model, much simpler to make portable across operating systems.
 * This code is fairly hacky (in particularly the way filep->fops is
 * swapped out) and has never been ported beyond linux.
 *
 * New clients use the I830_GETBUF2 ioctl which doesn't do any of this
 * stuff, and rely on a secure implementation of I830_VERTEX.
 */

static struct file_operations i830_buffer_fops = {
	.open	 = DRM(open),
	.flush	 = DRM(flush),
	.release = DRM(release),
	.ioctl	 = DRM(ioctl),
	.mmap	 = i830_mmap_buffers,
	.fasync  = DRM(fasync),
};

int i830_mmap_buffers(struct file *filp, struct vm_area_struct *vma)
{
	drm_file_t	    *priv	  = filp->private_data;
	drm_device_t	    *dev;
	drm_i830_private_t  *dev_priv;
	drm_buf_t           *buf;
	drm_i830_buf_priv_t *buf_priv;

	lock_kernel();
	dev	 = priv->dev;
	dev_priv = dev->dev_private;
	buf      = dev_priv->mmap_buffer;
	buf_priv = buf->dev_private;
   
	vma->vm_flags |= (VM_IO | VM_DONTCOPY);
	vma->vm_file = filp;
   
   	buf_priv->currently_mapped = I830_BUF_MAPPED;
	unlock_kernel();

	if (remap_page_range(DRM_RPR_ARG(vma) vma->vm_start,
			     VM_OFFSET(vma),
			     vma->vm_end - vma->vm_start,
			     vma->vm_page_prot)) return -EAGAIN;
	return 0;
}

static int i830_map_buffer(drm_buf_t *buf, struct file *filp)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
      	drm_i830_private_t *dev_priv = dev->dev_private;
   	struct file_operations *old_fops;
	int retcode = 0;

	if(buf_priv->currently_mapped == I830_BUF_MAPPED) return -EINVAL;

	down_write( &current->mm->mmap_sem );
	old_fops = filp->f_op;
	filp->f_op = &i830_buffer_fops;
	dev_priv->mmap_buffer = buf;
	buf_priv->virtual = (void *)do_mmap(filp, 0, buf->total, 
					    PROT_READ|PROT_WRITE,
					    MAP_SHARED, 
					    buf->bus_address);
	dev_priv->mmap_buffer = NULL;
	filp->f_op = old_fops;
	if (IS_ERR(buf_priv->virtual)) {
		/* Real error */
		DRM_ERROR("mmap error\n");
		retcode = PTR_ERR(buf_priv->virtual);
		buf_priv->virtual = 0;
	}
	up_write( &current->mm->mmap_sem );

	return retcode;
}

static int i830_unmap_buffer(drm_buf_t *buf)
{
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
	int retcode = 0;

	if(buf_priv->currently_mapped != I830_BUF_MAPPED) 
		return -EINVAL;

	down_write(&current->mm->mmap_sem);
	retcode = DO_MUNMAP(current->mm,
			    (unsigned long)buf_priv->virtual,
			    (size_t) buf->total);
	up_write(&current->mm->mmap_sem);

   	buf_priv->currently_mapped = I830_BUF_UNMAPPED;
   	buf_priv->virtual = 0;

	return retcode;
}

static int i830_dma_get_buffer(drm_device_t *dev, drm_i830_dma_t *d, 
			       struct file *filp)
{
	drm_buf_t *buf;
	drm_i830_buf_priv_t *buf_priv;
   	drm_device_dma_t *dma = dev->dma;
	int i, ret;
   
	/* Never use the last dma buffer, hw errata.
	 */
   	for (i = 0; i < dma->buf_count - 1; i++) {

		buf = dma->buflist[i];
	   	buf_priv = buf->dev_private;

	   	if (cmpxchg(buf_priv->in_use, 
			    I830_BUF_FREE, 
			    I830_BUF_CLIENT) == I830_BUF_FREE)
			goto got_buf;
	}
   	return DRM_ERR(ENOMEM);

 got_buf:
	ret = i830_map_buffer(buf, filp);
	if (ret) {
		(void) cmpxchg(buf_priv->in_use, 
			       I830_BUF_CLIENT, 
			       I830_BUF_FREE);

		DRM_ERROR("mapbuf failed, retcode %d\n", ret);
		return ret;
	}


	buf->filp = filp;
	buf_priv = buf->dev_private;	
	d->granted = 1;
   	d->request_idx = buf->idx;
   	d->request_size = buf->total;
   	d->virtual = buf_priv->virtual;

	return 0;
}

static void i830_dma_dispatch_vertex(drm_device_t *dev, 
				     drm_buf_t *buf,
				     int discard,
				     int used)
{
   	drm_i830_private_t *dev_priv = dev->dev_private;
	drm_i830_buf_priv_t *buf_priv = buf->dev_private;
   	drm_i830_sarea_t *sarea_priv = dev_priv->sarea_priv;
   	drm_clip_rect_t *box = sarea_priv->boxes;
   	int nbox = sarea_priv->nbox;
	unsigned long address = (unsigned long)buf->bus_address;
	unsigned long start = address - dev->agp->base;     
	int i = 0, u;
   	RING_LOCALS;

   	i830_kernel_lost_context(dev);

   	if (nbox > I830_NR_SAREA_CLIPRECTS) 
		nbox = I830_NR_SAREA_CLIPRECTS;

	if (discard) {
		u = cmpxchg(buf_priv->in_use, I830_BUF_CLIENT, 
			    I830_BUF_HARDWARE);
		if(u != I830_BUF_CLIENT) {
			DRM_DEBUG("xxxx 2\n");
		}
	}

	if (used > 4*1023) 
		used = 0;

	if (sarea_priv->dirty)
	   i830EmitState( dev );

  	DRM_DEBUG("dispatch vertex addr 0x%lx, used 0x%x nbox %d\n", 
		  address, used, nbox);

   	dev_priv->counter++;
   	DRM_DEBUG(  "dispatch counter : %ld\n", dev_priv->counter);
   	DRM_DEBUG(  "i830_dma_dispatch\n");
   	DRM_DEBUG(  "start : %lx\n", start);
	DRM_DEBUG(  "used : %d\n", used);
   	DRM_DEBUG(  "start + used - 4 : %ld\n", start + used - 4);

	if (buf_priv->currently_mapped == I830_BUF_MAPPED) {
		u32 *vp = buf_priv->virtual;

		vp[0] = (GFX_OP_PRIMITIVE |
			 sarea_priv->vertex_prim |
			 ((used/4)-2));

		if (dev_priv->use_mi_batchbuffer_start) {
			vp[used/4] = MI_BATCH_BUFFER_END; 
			used += 4; 
		}
		
		if (used & 4) {
			vp[used/4] = 0;
			used += 4;
		}

		i830_unmap_buffer(buf);
	}
		   
	if (used) {
		do {
			if (i < nbox) {
				BEGIN_LP_RING(6);
				OUT_RING( GFX_OP_DRAWRECT_INFO );
				OUT_RING( sarea_priv->BufferState[I830_DESTREG_DR1] );
				OUT_RING( box[i].x1 | (box[i].y1<<16) );
				OUT_RING( box[i].x2 | (box[i].y2<<16) );
				OUT_RING( sarea_priv->BufferState[I830_DESTREG_DR4] );
				OUT_RING( 0 );
				ADVANCE_LP_RING();
			}

			if (dev_priv->use_mi_batchbuffer_start) {
				BEGIN_LP_RING(2);
				OUT_RING( MI_BATCH_BUFFER_START | (2<<6) );
				OUT_RING( start | MI_BATCH_NON_SECURE );
				ADVANCE_LP_RING();
			} 
			else {
				BEGIN_LP_RING(4);
				OUT_RING( MI_BATCH_BUFFER );
				OUT_RING( start | MI_BATCH_NON_SECURE );
				OUT_RING( start + used - 4 );
				OUT_RING( 0 );
				ADVANCE_LP_RING();
			}

		} while (++i < nbox);
	}

	if (discard) {
		dev_priv->counter++;

		(void) cmpxchg(buf_priv->in_use, I830_BUF_CLIENT,
			       I830_BUF_HARDWARE);

		BEGIN_LP_RING(8);
		OUT_RING( CMD_STORE_DWORD_IDX );
		OUT_RING( 20 );
		OUT_RING( dev_priv->counter );
		OUT_RING( CMD_STORE_DWORD_IDX );
		OUT_RING( buf_priv->my_use_idx );
		OUT_RING( I830_BUF_FREE );
		OUT_RING( CMD_REPORT_HEAD );
		OUT_RING( 0 );
		ADVANCE_LP_RING();
	}
}


int i830_getbuf( DRM_IOCTL_ARGS )
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	int		  retcode   = 0;
	drm_i830_dma_t	  d;
   	drm_i830_private_t *dev_priv = (drm_i830_private_t *)dev->dev_private;
   	u32 *hw_status = dev_priv->hw_status_page;
   	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *) 
     					dev_priv->sarea_priv; 

	DRM_DEBUG("getbuf\n");
   	DRM_COPY_FROM_USER_IOCTL( d, (drm_i830_dma_t *)data, sizeof(d));
   
	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_dma called without lock held\n");
		return DRM_ERR(EINVAL);
	}
	
	d.granted = 0;

	retcode = i830_dma_get_buffer(dev, &d, filp);

	DRM_DEBUG("i830_dma: %d returning %d, granted = %d\n",
		  current->pid, retcode, d.granted);

	if (DRM_COPY_TO_USER((drm_dma_t *)data, &d, sizeof(d)))
		return DRM_ERR(EFAULT);

   	sarea_priv->last_dispatch = (int) hw_status[5];

	return retcode;
}

int i830_dma_vertex(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
   	drm_i830_private_t *dev_priv = (drm_i830_private_t *)dev->dev_private;
      	u32 *hw_status = dev_priv->hw_status_page;
   	drm_i830_sarea_t *sarea_priv = (drm_i830_sarea_t *) 
     					dev_priv->sarea_priv; 
	drm_i830_vertex_t vertex;

	if (copy_from_user(&vertex, (drm_i830_vertex_t *)arg, sizeof(vertex)))
		return -EFAULT;

   	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("i830_dma_vertex called without lock held\n");
		return -EINVAL;
	}

	DRM_DEBUG("i830 dma vertex, idx %d used %d discard %d\n",
		  vertex.idx, vertex.used, vertex.discard);

	if(vertex.idx < 0 || vertex.idx > dma->buf_count) return -EINVAL;

	i830_dma_dispatch_vertex( dev, 
				  dma->buflist[ vertex.idx ], 
				  vertex.discard, vertex.used );

	sarea_priv->last_enqueue = dev_priv->counter-1;
   	sarea_priv->last_dispatch = (int) hw_status[5];
   
	return 0;
}
