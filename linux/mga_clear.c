/* mga_state.c -- State support for mga g200/g400 -*- linux-c -*-
 *
 * Created: February 2000 by keithw@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 * Authors: 
 *           Keith Whitwell <keithw@precisioninsight.com>
 *
 */
 
#define __NO_VERSION__
#include "drmP.h"
#include "mga_drv.h"
#include "mgareg_flags.h"
#include "mga_dma.h"
#include "mga_state.h"



/* Build and queue a TT_GENERAL secondary buffer to do the clears.
 * With Jeff's ringbuffer idea, it might make sense if there are only
 * one or two cliprects to emit straight to the primary buffer.
 */
static int mgaClearBuffers(drm_device_t *dev,
			   int clear_color,
			   int clear_depth,
			   int flags)
{
	drm_device_dma_t *dma = dev->dma;
	drm_freelist_t *freelist = &dma->bufs[MGA_DMA_BUF_ORDER].freelist;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	drm_dma_t d;

	buf = drm_freelist_get(freelist, _DRM_DMA_WAIT);
	if (!buf) return -1;

	buf->pid     = current->pid;

	buf_priv = buf->dev_private;
	buf_priv->dma_type = MGA_DMA_CLEAR;
	buf_priv->clear_color = clear_color;
	buf_priv->clear_zval = clear_depth;
	buf_priv->clear_flags = flags;

	if (!mgaCopyAndVerifyState(dev_priv, buf_priv, MGA_UPLOAD_CLIPRECTS)) {
		drm_freelist_put( dev, freelist, buf );
		return -EINVAL;
	}
	   

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CTX;
	buf->used = 1;	


	d.context = DRM_KERNEL_CONTEXT;
	d.send_count = 1;
	d.send_indices = &buf->idx;
	d.send_sizes = &buf->used;
   	d.flags = 0;
	d.request_count = 0;
	d.request_size = 0;
	d.request_indices = NULL;
	d.request_sizes = NULL;
	d.granted_count = 0;	 

   	atomic_inc(&dev_priv->pending_bufs);
      	if((drm_dma_enqueue(dev, &d)) != 0) 
     		atomic_dec(&dev_priv->pending_bufs);
	mga_dma_schedule(dev, 1);
   	return 0;
}

int mgaSwapBuffers(drm_device_t *dev, int flags) 
{
	drm_device_dma_t *dma = dev->dma;
	drm_freelist_t *freelist = &dma->bufs[MGA_DMA_BUF_ORDER].freelist;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	drm_dma_t d;

	buf = drm_freelist_get(freelist, _DRM_DMA_WAIT);
	if (!buf) return -1;

	buf->pid     = current->pid;
	buf_priv = buf->dev_private;
	buf_priv->dma_type = MGA_DMA_SWAP;

	if (!mgaCopyAndVerifyState(dev_priv, buf_priv, MGA_UPLOAD_CLIPRECTS)) {
		drm_freelist_put( dev, freelist, buf );
		return -EINVAL;
	}
	   

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CTX;
	buf->used = 1;	


	d.context = DRM_KERNEL_CONTEXT;
	d.send_count = 1;
	d.send_indices = &buf->idx;
	d.send_sizes = &buf->used;
   	d.flags = 0;
	d.request_count = 0;
	d.request_size = 0;
	d.request_indices = NULL;
	d.request_sizes = NULL;
	d.granted_count = 0;	 

   	atomic_inc(&dev_priv->pending_bufs);
      	if((drm_dma_enqueue(dev, &d)) != 0) 
     		atomic_dec(&dev_priv->pending_bufs);
	mga_dma_schedule(dev, 1);
   	return 0;
}


static int mgaIload(drm_device_t *dev, drm_mga_iload_t *args)
{
	drm_device_dma_t *dma = dev->dma;
   	drm_buf_t *buf = dma->buflist[ args->idx ];
	drm_mga_buf_priv_t *buf_priv = (drm_mga_buf_priv_t *)buf->dev_private;
   	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_dma_t d;
   	int pixperdword;
   	
   	buf_priv->dma_type = MGA_DMA_ILOAD;
   	buf_priv->boxes[0].y1 = args->texture.y1;
   	buf_priv->boxes[0].y2 = args->texture.y2;
   	buf_priv->boxes[0].x1 = args->texture.x1;
   	buf_priv->boxes[0].x2 = args->texture.x2;
   	buf_priv->ContextState[MGA_CTXREG_DSTORG] = args->destOrg;
   	buf_priv->ContextState[MGA_CTXREG_MACCESS] = args->mAccess;
   	buf_priv->ServerState[MGA_2DREG_PITCH] = args->pitch;
	buf_priv->nbox = 1;   
   	sarea_priv->dirty |= (MGA_UPLOAD_CTX | MGA_UPLOAD_2D);
   	switch((args->mAccess & 0x00000003)) {
	 	case 0:
	   		pixperdword = 4;
	   	break;
	 	case 1:
	   		pixperdword = 2;
	   	break;
	 	case 2:
	   		pixperdword = 1;
	  	break;
		default:
	   		DRM_ERROR("Invalid maccess value passed"
				  " to mgaIload\n");
	   	return -EINVAL;
	}
   	buf->used = ((args->texture.y2 - args->texture.y1) *
		     (args->texture.x2 - args->texture.x1) /
		     pixperdword);
   	DRM_DEBUG("buf->used : %d\n", buf->used);
	d.context = DRM_KERNEL_CONTEXT;
	d.send_count = 1;
	d.send_indices = &buf->idx;
	d.send_sizes = &buf->used;
	d.flags = 0;
	d.request_count = 0;
	d.request_size = 0;
	d.request_indices = NULL;
	d.request_sizes = NULL;
	d.granted_count = 0;	 
   
      	atomic_inc(&dev_priv->pending_bufs);
      	if((drm_dma_enqueue(dev, &d)) != 0) 
     		atomic_dec(&dev_priv->pending_bufs);
	mga_dma_schedule(dev, 1);

	return 0; 
}


int mga_clear_bufs(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_clear_t clear;
	int retcode;
   
	return 0;

	copy_from_user_ret(&clear, (drm_mga_clear_t *)arg,
			   sizeof(clear), -EFAULT);
   
	retcode = mgaClearBuffers(dev, clear.clear_color,
				  clear.clear_depth,
				  clear.flags);
   
	return retcode;
}

int mga_swap_bufs(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_swap_t swap;
	int retcode = 0;

	return 0;

	copy_from_user_ret(&swap, (drm_mga_swap_t *)arg,
			   sizeof(swap), -EFAULT);
   
	retcode = mgaSwapBuffers(dev, swap.flags);
   
	return retcode;
}

int mga_iload(struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_iload_t iload;
	int retcode = 0;

	copy_from_user_ret(&iload, (drm_mga_iload_t *)arg, 
			   sizeof(iload), -EFAULT);
   
	retcode = mgaIload(dev, &iload);
   
	return retcode;
}


int mga_dma(struct inode *inode, struct file *filp, unsigned int cmd,
	    unsigned long arg)
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	drm_device_dma_t  *dma	    = dev->dma;
	int		  retcode   = 0;
	drm_dma_t	  d;

   	copy_from_user_ret(&d, (drm_dma_t *)arg, sizeof(d), -EFAULT);
	DRM_DEBUG("%d %d: %d send, %d req\n",
		  current->pid, d.context, d.send_count, d.request_count);

	/* Per-context queues are unworkable if you are trying to do
	 * state management from the client.
	 */
	d.context = DRM_KERNEL_CONTEXT;
	d.flags &= ~_DRM_DMA_WHILE_LOCKED;

	/* Maybe multiple buffers is useful for iload...
	 * But this ioctl is only for *despatching* vertex data...
	 */
	if (d.send_count < 0 || d.send_count > 1) {
		DRM_ERROR("Process %d trying to send %d buffers (max 1)\n",
			  current->pid, d.send_count);
		return -EINVAL;
	}

	
	/* But it *is* used to request buffers for all types of dma:
	 */
	if (d.request_count < 0 || d.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, d.request_count, dma->buf_count);
		return -EINVAL;
	}

	if (d.send_count) {
		int idx = d.send_indices[0];
		drm_mga_buf_priv_t *buf_priv = dma->buflist[ idx ]->dev_private;
		drm_mga_private_t *dev_priv = dev->dev_private;

		buf_priv->dma_type = MGA_DMA_VERTEX;

		/* Snapshot the relevent bits of the sarea... 
		 */
		if (!mgaCopyAndVerifyState( dev_priv, buf_priv, ~0 ))
			dma->buflist[ idx ]->used = 0;

	      	atomic_inc(&dev_priv->pending_bufs);
		retcode = drm_dma_enqueue(dev, &d);
	      	if(retcode != 0) 
     			atomic_dec(&dev_priv->pending_bufs);
		mga_dma_schedule(dev, 1);
	}
	
	d.granted_count = 0;

	if (!retcode && d.request_count) {
		retcode = drm_dma_get_buffers(dev, &d);
	}

	DRM_DEBUG("%d returning, granted = %d\n",
		  current->pid, d.granted_count);
	copy_to_user_ret((drm_dma_t *)arg, &d, sizeof(d), -EFAULT);

	return retcode;
}
