/* i810_state.c -- State support for i810 g200/g400 -*- linux-c -*-
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
#include "i810_drv.h"
#include "i810_dma.h"
#include "i810_drm_public.h"



static int i810DmaGeneral(drm_device_t *dev, drm_i810_general_t *args)
{
	drm_device_dma_t *dma = dev->dma;
   	drm_buf_t *buf = dma->buflist[ args->idx ];
	drm_i810_buf_priv_t *buf_priv = (drm_i810_buf_priv_t *)buf->dev_private;
   	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
	drm_dma_t d;
   	
   	buf_priv->dma_type = I810_DMA_GENERAL;
	buf_priv->age = args->age;
	buf->used = args->used;

	if (I810_VERBOSE)
		printk("i810DmaGeneral idx %d used %d\n", args->idx, buf->used);

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
	i810_dma_schedule(dev, 1);

	return 0; 
}


static int i810DmaVertex(drm_device_t *dev, drm_i810_vertex_t *args)
{
	drm_device_dma_t *dma = dev->dma;
	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
	drm_i810_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_i810_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	drm_dma_t d;


	buf = dma->buflist[ args->idx ];
	buf->used = args->real_used;

	if (I810_VERBOSE)
		printk("i810DmaVertex idx %d used %d\n", args->idx, buf->used);

	buf_priv = buf->dev_private;
	buf_priv->dma_type = I810_DMA_VERTEX;
	buf_priv->vertex_real_idx = args->real_idx;
	buf_priv->vertex_discard = args->discard;
	buf_priv->nbox = sarea_priv->nbox;
	buf_priv->age = args->age;

	if (buf_priv->nbox >= I810_NR_SAREA_CLIPRECTS)
		buf_priv->nbox = I810_NR_SAREA_CLIPRECTS;

	if (buf_priv->nbox)
		memcpy( buf_priv->boxes, 
			sarea_priv->boxes,
			buf_priv->nbox * sizeof(xf86drmClipRectRec));
	   
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
	i810_dma_schedule(dev, 1);
   	return 0;
}




int i810_dma_general(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_i810_general_t general;
	int retcode = 0;
	
	copy_from_user_ret(&general, (drm_i810_general_t *)arg, sizeof(general),
			   -EFAULT);

	if (I810_VERBOSE) 
		printk("i810 dma general idx %d used %d\n",
		       general.idx, general.used);
   
	retcode = i810DmaGeneral(dev, &general);
   
	return retcode;
}

int i810_dma_vertex(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_i810_vertex_t vertex;
	int retcode = 0;

	copy_from_user_ret(&vertex, (drm_i810_vertex_t *)arg, sizeof(vertex),
			   -EFAULT);
   
	if (I810_VERBOSE) 
		printk("i810 dma vertex, idx %d used %d"
		       " real_idx %d discard %d\n",
		       vertex.idx, vertex.real_used, vertex.real_idx,
		       vertex.discard);

	retcode = i810DmaVertex(dev, &vertex);
   
	return retcode;

}


int i810_dma(struct inode *inode, struct file *filp, unsigned int cmd,
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

	/* Please don't send us buffers.
	 */
	if (d.send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  current->pid, d.send_count);
		return -EINVAL;
	}
	
	/* We'll send you buffers.
	 */
	if (d.request_count < 0 || d.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, d.request_count, dma->buf_count);
		return -EINVAL;
	}
	
	d.granted_count = 0;

	if (!retcode && d.request_count) {
		retcode = drm_dma_get_buffers(dev, &d);
	}

	copy_to_user_ret((drm_dma_t *)arg, &d, sizeof(d), -EFAULT);

	return retcode;
}


