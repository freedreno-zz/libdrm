/* i810_dma.c -- DMA support for the i810 -*- linux-c -*-
 * Created: Mon Dec 13 01:50:01 1999 by jhartmann@precisioninsight.com
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
 * Authors: Rickard E. (Rik) Faith <faith@precisioninsight.com>
 *	    Jeff Hartmann <jhartmann@precisioninsight.com>
 *
 * $XFree86$
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "i810_drm_public.h"
#include "i810_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/time.h>		/* For do_gettimeofday    */

#define I810_REG(reg)		2
#define I810_BASE(reg)		((unsigned long) \
				dev->maplist[I810_REG(reg)]->handle)
#define I810_ADDR(reg)		(I810_BASE(reg) + reg)
#define I810_DEREF(reg)		*(__volatile__ int *)I810_ADDR(reg)
#define I810_READ(reg)		I810_DEREF(reg)
#define I810_WRITE(reg,val) 	do { I810_DEREF(reg) = val; } while (0)
#define I810_DEREF16(reg)	*(__volatile__ u16 *)I810_ADDR(reg)
#define I810_READ16(reg)	I810_DEREF16(reg)
#define I810_WRITE16(reg,val)	do { I810_DEREF16(reg) = val; } while (0)

#define RING_LOCALS()	unsigned int outring, ringmask; volatile char *virt;
#define BEGIN_LP_RING(n)                                         \
   if (dev_priv->ring.space < n*4) i810_wait_ring(dev, n*4, 0);  \
   dev_priv->ring.space -= n*4;                                  \
   outring = dev_priv->ring.tail;                                \
   ringmask = dev_priv->ring.tail_mask;                          \
   virt = dev_priv->ring.virtual_start;

#define ADVANCE_LP_RING() {                         \
    dev_priv->ring.tail = outring;                  \
    I810_WRITE(LP_RING + RING_TAIL, outring);       \
}

#define OUT_RING(n) {                                   \
   *(volatile unsigned int *)(virt + outring) = n;      \
   outring += 4;                                        \
   outring &= ringmask;                                 \
}

static unsigned long i810_alloc_page(drm_device_t *dev)
{
   unsigned long address;
   
   address = __get_free_page(GFP_KERNEL);
   if(address == 0UL) {
      return 0;
   }
   atomic_inc(&mem_map[MAP_NR((void *) address)].count);
   set_bit(PG_locked, &mem_map[MAP_NR((void *) address)].flags);
   
   return address;
}

static void i810_free_page(drm_device_t *dev, unsigned long page)
{
   if(page == 0UL) {
      return;
   }
   atomic_dec(&mem_map[MAP_NR((void *) page)].count);
   clear_bit(PG_locked, &mem_map[MAP_NR((void *) page)].flags);
   wake_up(&mem_map[MAP_NR((void *) page)].wait);
   free_page(page);
   return;
}

static int i810_alloc_kernel_queue(drm_device_t *dev)
{
	drm_queue_t *queue = NULL;
				/* Allocate a new queue */
	down(&dev->struct_sem);
	
   	if(dev->queue_count != 0) {
	   /* Reseting the kernel context here is not
	    * a race, since it can only happen when that
	    * queue is empty.
	    */
	   queue = dev->queuelist[DRM_KERNEL_CONTEXT];
	   printk("Kernel queue already allocated\n");
	} else {
	   queue = drm_alloc(sizeof(*queue), DRM_MEM_QUEUES);
	   if(!queue) {
	      up(&dev->struct_sem);
	      printk("out of memory\n");
	      return -ENOMEM;
	   }
	   ++dev->queue_count;
	   dev->queuelist = drm_alloc(sizeof(*dev->queuelist), 
				      DRM_MEM_QUEUES);
	   if(!dev->queuelist) {
	      up(&dev->struct_sem);
	      drm_free(queue, sizeof(*queue), DRM_MEM_QUEUES);
	      printk("out of memory\n");
	      return -ENOMEM;
	   }  
	}
	   
	memset(queue, 0, sizeof(*queue));
	atomic_set(&queue->use_count, 1);
   	atomic_set(&queue->finalization,  0);
	atomic_set(&queue->block_count,   0);
	atomic_set(&queue->block_read,    0);
	atomic_set(&queue->block_write,   0);
	atomic_set(&queue->total_queued,  0);
	atomic_set(&queue->total_flushed, 0);
	atomic_set(&queue->total_locks,   0);

	init_waitqueue_head(&queue->write_queue);
	init_waitqueue_head(&queue->read_queue);
	init_waitqueue_head(&queue->flush_queue);

	queue->flags = 0;

	drm_waitlist_create(&queue->waitlist, dev->dma->buf_count);
   
   	dev->queue_slots = 1;
   	dev->queuelist[DRM_KERNEL_CONTEXT] = queue;
   	dev->queue_count--;
	
	up(&dev->struct_sem);
	printk("%d (new)\n", dev->queue_count - 1);
	return DRM_KERNEL_CONTEXT;
}

static int i810_dma_cleanup(drm_device_t *dev)
{
      	DRM_DEBUG("i810_dma_cleanup\n");

	if(dev->dev_private) {
	   	drm_i810_private_t *dev_priv = 
	     		(drm_i810_private_t *) dev->dev_private;
	   
	   	if(dev_priv->ring.virtual_start) {
		   	drm_ioremapfree((void *) dev_priv->ring.virtual_start,
					dev_priv->ring.Size);
		}
	   	if(dev_priv->hw_status_page != 0UL) {
		   	i810_free_page(dev, dev_priv->hw_status_page);
		   	/* Need to rewrite hardware status page */
		   	I810_WRITE(0x02080, 0x1ffff000);
		}
	   	drm_free(dev->dev_private, sizeof(drm_i810_private_t), 
			 DRM_MEM_DRIVER);
	   	dev->dev_private = NULL;
	}
   	return 0;
}

static int __gettimeinmillis(void)
{
   struct timeval timep;
   
   do_gettimeofday(&timep);
   return(timep.tv_sec * 1000) + (timep.tv_usec / 1000);
}

static int i810_wait_ring(drm_device_t *dev, int n, int timeout_millis)
{
   	drm_i810_private_t *dev_priv = dev->dev_private;
   	drm_i810_ring_buffer *ring = &(dev_priv->ring);
   	int iters = 0;
   	int startTime = 0;
   	int curTime = 0;
   
      	if (timeout_millis == 0) timeout_millis = 3000;
   
   	DRM_DEBUG(  "i810_wait_ring %d\n", n);
   
   	while (ring->space < n) {
	   	int i;
	
	   	ring->head = I810_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
	   	ring->space = ring->head - (ring->tail+8);
	   
	   	if (ring->space < 0) ring->space += ring->Size;
	   
	   	iters++;
	   	curTime = __gettimeinmillis();
	   	if (startTime == 0 || curTime < startTime /*wrap case*/) {
		   	startTime = curTime;
	   	} else if (curTime - startTime > timeout_millis) {
		   	DRM_ERROR("space: %d wanted %d\n", ring->space, n);
		   	DRM_ERROR("lockup\n");
		}
	   
	   	for (i = 0 ; i < 2000 ; i++) ;
	}
   
   	return iters;
}

static void i810_kernel_lost_context(drm_device_t *dev)
{
      	drm_i810_private_t *dev_priv = dev->dev_private;
   	drm_i810_ring_buffer *ring = &(dev_priv->ring);
   
   	DRM_DEBUG("i810_kernel_lost_context, old ring (%x,%x)\n", 
	       ring->head, ring->tail);
   
   	ring->head = I810_READ(LP_RING + RING_HEAD) & HEAD_ADDR;
     	ring->tail = I810_READ(LP_RING + RING_TAIL);
     	ring->space = ring->head - (ring->tail+8);
     	if (ring->space < 0) ring->space += ring->Size;
   
   	DRM_DEBUG("new ring (%x,%x)\n", ring->head, ring->tail);
}

static inline void i810_ring_write_status(drm_device_t *dev)
{
      	drm_i810_private_t *dev_priv = dev->dev_private;
   	RING_LOCALS();
   
	i810_kernel_lost_context(dev);
   	dev_priv->counter++;
#if 1
	BEGIN_LP_RING(8);
#else
   	BEGIN_LP_RING(16);
#endif
   	OUT_RING(CMD_REPORT_HEAD);
   	OUT_RING(0);
#if 1
   	OUT_RING(CMD_STORE_DWORD_IDX);
   	OUT_RING(5 * sizeof(unsigned long));
   	OUT_RING(dev_priv->counter);
   	OUT_RING(0);
   
#endif
		/* Add a breakpoint interrupt at the end */
#if 1
      	OUT_RING(0);
   	OUT_RING(GFX_OP_BREAKPOINT_INTERRUPT);
#endif
   /* Add a blit here just to see if my commands work */
#if 0
   	OUT_RING(0x02000000);
   	OUT_RING(0);
   	OUT_RING(0x50c00004);
   	OUT_RING(0xcc0800);
   	OUT_RING(0x1900320);
   	OUT_RING(0);
   	OUT_RING(0x800);
   	OUT_RING(0xb80000);
   	OUT_RING(0x02000000);
   	OUT_RING(0);
#endif
   	ADVANCE_LP_RING();
   	/* Wait for ring to complete */
      	i810_wait_ring(dev, dev_priv->ring.Size - 8, 0);
}

static inline void i810_print_status_page(drm_device_t *dev)
{
      	drm_i810_private_t *dev_priv = dev->dev_private;
	u32 *temp = (u32 *)dev_priv->hw_status_page;

   	DRM_DEBUG(  "hw_status: Interrupt Status : %lx\n", temp[0]);
   	DRM_DEBUG(  "hw_status: LpRing Head ptr : %lx\n", temp[1]);
   	DRM_DEBUG(  "hw_status: IRing Head ptr : %lx\n", temp[2]);
      	DRM_DEBUG(  "hw_status: Reserved : %lx\n", temp[3]);
   	DRM_DEBUG(  "hw_status: Driver Counter : %d\n", temp[5]);   
}

static int i810_dma_initialize(drm_device_t *dev, 
			       drm_i810_private_t *dev_priv,
			       drm_i810_init_t *init)
{
   	DRM_DEBUG(  "i810_dma_init\n");
   	dev->dev_private = (void *) dev_priv;
   	memset(dev_priv, 0, sizeof(drm_i810_private_t));
   	if((init->ring_map_idx >= dev->map_count) ||
	   (init->buffer_map_idx >= dev->map_count)) {
	   	i810_dma_cleanup(dev);
	   	DRM_ERROR("ring_map or buffer_map are invalid\n");
	   	return -EINVAL;
	}
   
   	if(i810_alloc_kernel_queue(dev) != DRM_KERNEL_CONTEXT) {
	   i810_dma_cleanup(dev);
	   DRM_ERROR("Kernel context queue not present\n");
	   return -ENOMEM;
	}

   	dev_priv->ring_map_idx = init->ring_map_idx;
   	dev_priv->buffer_map_idx = init->buffer_map_idx;

   	atomic_set(&dev_priv->pending_bufs, 0);
   	atomic_set(&dev_priv->dispatch_lock, 0);
   	atomic_set(&dev_priv->in_flush, 0);
   	
   	dev_priv->ring.Start = init->ring_start;
   	dev_priv->ring.End = init->ring_end;
   	dev_priv->ring.Size = init->ring_size;
   	dev_priv->ring.virtual_start = drm_ioremap(dev->agp->base + 
						   init->ring_start, 
						   init->ring_size);
   	dev_priv->ring.tail_mask = dev_priv->ring.Size - 1;
   
   	if(dev_priv->ring.virtual_start == NULL) {
	   	i810_dma_cleanup(dev);
	   	DRM_ERROR("can not ioremap virtual address for"
			  " ring buffer\n");
	   	return -ENOMEM;
	}
   
   	/* Program Hardware Status Page */
   	dev_priv->hw_status_page = i810_alloc_page(dev);
   	memset((void *) dev_priv->hw_status_page, 0, PAGE_SIZE);
   	if(dev_priv->hw_status_page == 0UL) {
	   i810_dma_cleanup(dev);
	   DRM_ERROR("Can not allocate hardware status page\n");
	   return -ENOMEM;
	}
   	DRM_DEBUG("hw status page @ %lx\n", dev_priv->hw_status_page);
   
   	I810_WRITE(0x02080, virt_to_bus((void *)dev_priv->hw_status_page));
   	DRM_DEBUG("Enabled hardware status page\n");
#if 0
   	DRM_DEBUG("Doing first ring buffer write\n");
   	i810_ring_write_status(dev);
   	DRM_DEBUG("First ring write succeeded\n");
	i810_print_status_page(dev);
   	DRM_DEBUG("Status page dump succeeded\n");
#endif
   	return 0;
}

int i810_dma_init(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
   	drm_file_t *priv = filp->private_data;
   	drm_device_t *dev = priv->dev;
   	drm_i810_private_t *dev_priv;
   	drm_i810_init_t init;
   	int retcode = 0;
	
   	copy_from_user_ret(&init, (drm_i810_init_t *)arg, 
			   sizeof(init), -EFAULT);
	
   	switch(init.func) {
	 	case I810_INIT_DMA:
	   		dev_priv = drm_alloc(sizeof(drm_i810_private_t), 
					     DRM_MEM_DRIVER);
	   		if(dev_priv == NULL) return -ENOMEM;
	   		retcode = i810_dma_initialize(dev, dev_priv, &init);
	   	break;
	 	case I810_CLEANUP_DMA:
	   		retcode = i810_dma_cleanup(dev);
	   	break;
	 	default:
	   		retcode = -EINVAL;
	   	break;
	}
   
   	return retcode;
}

static inline void i810_dma_dispatch(drm_device_t *dev, unsigned long address,
				    unsigned long length)
{
      	drm_i810_private_t *dev_priv = dev->dev_private;
	unsigned long start = address - dev->agp->base;
   	RING_LOCALS();

   	dev_priv->counter++;
   	DRM_DEBUG(  "dispatch counter : %d\n", dev_priv->counter);
   	DRM_DEBUG(  "i810_dma_dispatch\n");
   	DRM_DEBUG(  "start : %lx\n", start);
	DRM_DEBUG(  "length : %d\n", length);
   	DRM_DEBUG(  "start + length - 4 : %d\n", start + length - 4);
   	i810_kernel_lost_context(dev);
   	BEGIN_LP_RING(8);
   	OUT_RING(CMD_OP_BATCH_BUFFER);
   	OUT_RING(start | BB1_PROTECTED);
   	OUT_RING((start + length) - 4);
      	OUT_RING(CMD_STORE_DWORD_IDX);
   	OUT_RING(5 * sizeof(unsigned long));
   	OUT_RING(dev_priv->counter);
   		/* Add a breakpoint interrupt at the end */
      	OUT_RING(CMD_REPORT_HEAD);
   	OUT_RING(GFX_OP_BREAKPOINT_INTERRUPT);   
   	ADVANCE_LP_RING();
   
   	/* Wait for ring to flush */
#if 0
   	i810_wait_ring(dev, dev_priv->ring.Size - 8, 0);
      	i810_ring_write_status(dev);
   	DRM_DEBUG("ring write succeeded\n");
	i810_print_status_page(dev);
   	DRM_DEBUG("Status page dump succeeded\n");
#endif
   	i810_print_status_page(dev);
}

static inline void i810_dma_quiescent(drm_device_t *dev)
{
   	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;

   	DRM_DEBUG(  "i810_dma_quiescent\n");
   	while(1) {
	   	atomic_inc(&dev_priv->dispatch_lock);
	   	if(atomic_read(&dev_priv->dispatch_lock) == 1) {
		   	break;
		} else {
		   	atomic_dec(&dev_priv->dispatch_lock);
		}
	}
   	atomic_dec(&dev_priv->dispatch_lock);
}

static inline void i810_dma_ready(drm_device_t *dev)
{
   	i810_dma_quiescent(dev);
   	DRM_DEBUG(  "i810_dma_ready\n");
}

static inline int i810_dma_is_ready(drm_device_t *dev)
{
   	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
   	DRM_DEBUG(  "i810_dma_is_ready\n");
   	atomic_inc(&dev_priv->dispatch_lock);
   	if(atomic_read(&dev_priv->dispatch_lock) == 1) {
	   	/* We got the lock */
	   	return 1;
	} else {
	   	atomic_dec(&dev_priv->dispatch_lock);
	   	return 0;
	}
}

static inline int i810_dma_is_ready_no_hold(drm_device_t *dev)
{
   	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
   	atomic_inc(&dev_priv->dispatch_lock);
   	if(atomic_read(&dev_priv->dispatch_lock) == 1) {
	   	/* We got the lock, but free it */
	   	atomic_dec(&dev_priv->dispatch_lock);
	   	return 1;
	} else {
	   	atomic_dec(&dev_priv->dispatch_lock);
	   	return 0;
	}
}

static void i810_dma_service(int irq, void *device, struct pt_regs *regs)
{
	drm_device_t	 *dev = (drm_device_t *)device;
	drm_device_dma_t *dma = dev->dma;
	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
   	u16 temp;
   
	atomic_inc(&dev->total_irq);
   	DRM_DEBUG("Interrupt Handler\n");
	i810_print_status_page(dev);
      	temp = I810_READ16(I810REG_INT_IDENTITY_R);
   	temp = temp & ~(0x6000);
   	if(temp != 0) I810_WRITE16(I810REG_INT_IDENTITY_R, 
				   temp); /* Clear all interrupts */
   	atomic_dec(&dev_priv->dispatch_lock);
   	
				/* Free previous buffer */
   	if (test_and_set_bit(0, &dev->dma_flag)) {
	   	atomic_inc(&dma->total_missed_free);
	   	return;
	}
   	if (dma->this_buffer) {
	   	drm_free_buffer(dev, dma->this_buffer);
	   	dma->this_buffer = NULL;
	}
   	clear_bit(0, &dev->dma_flag);
   
   				/* Dispatch new buffer */
   	queue_task(&dev->tq, &tq_immediate);
   	mark_bh(IMMEDIATE_BH);
}

/* Only called by i810_dma_schedule. */
static int i810_do_dma(drm_device_t *dev, int locked)
{
	drm_buf_t	 *buf;
	int		 retcode = 0;
	drm_device_dma_t *dma = dev->dma;
      	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
   	unsigned long address;
   	unsigned long length;

   	printk("i810_do_dma\n");
	if (test_and_set_bit(0, &dev->dma_flag)) {
		atomic_inc(&dma->total_missed_dma);
		return -EBUSY;
	}
	
	if (!dma->next_buffer) {
		DRM_ERROR("No next_buffer\n");
		clear_bit(0, &dev->dma_flag);
		return -EINVAL;
	}

	buf	= dma->next_buffer;

	printk("context %d, buffer %d\n", buf->context, buf->idx);

	if (buf->list == DRM_LIST_RECLAIM) {
		drm_clear_next_buffer(dev);
		drm_free_buffer(dev, buf);
	      	atomic_dec(&dev_priv->pending_bufs);
	   	if(!(atomic_read(&dev_priv->pending_bufs))) {
		   wake_up_interruptible(&dev->queuelist[DRM_KERNEL_CONTEXT]->flush_queue);
	   	}
		clear_bit(0, &dev->dma_flag);
		return -EINVAL;
	}

	if (!buf->used) {
		DRM_ERROR("0 length buffer\n");
		drm_clear_next_buffer(dev);
		drm_free_buffer(dev, buf);
		clear_bit(0, &dev->dma_flag);
		return 0;
	}
	
	if (i810_dma_is_ready(dev) == 0) {
		clear_bit(0, &dev->dma_flag);
		return -EBUSY;
	}
   
	/* Always hold the hardware lock while dispatching.
	 */

	if (!locked && !drm_lock_take(&dev->lock.hw_lock->lock,
				      DRM_KERNEL_CONTEXT)) {
		atomic_inc(&dma->total_missed_lock);
		clear_bit(0, &dev->dma_flag);
		atomic_dec(&dev_priv->dispatch_lock);
		return -EBUSY;
	}

   	dma->next_queue	 = dev->queuelist[DRM_KERNEL_CONTEXT];
	drm_clear_next_buffer(dev);
	buf->pending	 = 1;
	buf->waiting	 = 0;
	buf->list	 = DRM_LIST_PEND;
   	address = buf->bus_address;
   	length = buf->used;

   	printk("dispatch!\n");
   	i810_dma_dispatch(dev, address, length);
   
   	atomic_dec(&dev_priv->pending_bufs);

   	if(dma->this_buffer) {
	   drm_free_buffer(dev, dma->this_buffer);
	}
   
	dma->this_buffer = buf;

	atomic_add(buf->used, &dma->total_bytes);
	atomic_inc(&dma->total_dmas);

	if (!locked) {
		if (drm_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
		}
	}

	clear_bit(0, &dev->dma_flag);
   
   	if(!(atomic_read(&dev_priv->pending_bufs))) {
	   wake_up_interruptible(&dev->queuelist[DRM_KERNEL_CONTEXT]->flush_queue);
	}   
   
	/* We hold the dispatch lock until the interrupt handler
	 * frees it
	 */
	return retcode;
}

static void i810_dma_schedule_tq_wrapper(void *dev)
{
	i810_dma_schedule(dev, 0);
}

int i810_dma_schedule(drm_device_t *dev, int locked)
{
	drm_queue_t	 *q;
	drm_buf_t	 *buf;
	int		 retcode   = 0;
	int		 processed = 0;
	int		 missed;
	int		 expire	   = 20;
	drm_device_dma_t *dma	   = dev->dma;
      	drm_i810_private_t *dev_priv = dev->dev_private;


      	printk("i810_dma_schedule\n");

	if (test_and_set_bit(0, &dev->interrupt_flag)) {
				/* Not reentrant */
		atomic_inc(&dma->total_missed_sched);
		return -EBUSY;
	}
	missed = atomic_read(&dma->total_missed_sched);

again:
	/* There is only one queue:
	 */
	if (!dma->next_buffer && DRM_WAITCOUNT(dev, DRM_KERNEL_CONTEXT)) {
		q   = dev->queuelist[DRM_KERNEL_CONTEXT];
		buf = drm_waitlist_get(&q->waitlist);
		dma->next_buffer = buf;
		dma->next_queue	 = q;
		if (buf && buf->list == DRM_LIST_RECLAIM) {
		   	printk("reclaiming in i810_dma_schedule\n");
			drm_clear_next_buffer(dev);
			drm_free_buffer(dev, buf);
		   	atomic_dec(&dev_priv->pending_bufs);
		   printk("pending bufs : %d\n", atomic_read(&dev_priv->pending_bufs));
	   		if(!(atomic_read(&dev_priv->pending_bufs))) {
			   wake_up_interruptible(&dev->queuelist[DRM_KERNEL_CONTEXT]->flush_queue);
			}
		   dma->next_buffer = NULL;
		   goto again;
		}
	}

	if (dma->next_buffer) {
		if (!(retcode = i810_do_dma(dev, locked))) 
			++processed;
	}
   	if(!(atomic_read(&dev_priv->pending_bufs))) {
	   wake_up_interruptible(&dev->queuelist[DRM_KERNEL_CONTEXT]->flush_queue);
	}


	/* Try again if we succesfully dispatched a buffer, or if someone 
	 * tried to schedule while we were working.
	 */
	if (--expire) {
		if (missed != atomic_read(&dma->total_missed_sched)) {
			atomic_inc(&dma->total_lost);
		   	if (i810_dma_is_ready_no_hold(dev)) 
				goto again;
		}

		if (processed && i810_dma_is_ready_no_hold(dev)) {
			atomic_inc(&dma->total_lost);
			processed = 0;
			goto again;
		}
	}
	
	clear_bit(0, &dev->interrupt_flag);
	
	return retcode;
}

static int i810_dma_send_buffers(drm_device_t *dev, drm_dma_t *d)
{
	DECLARE_WAITQUEUE(entry, current);
	drm_buf_t	  *last_buf = NULL;
	int		  retcode   = 0;
	drm_device_dma_t  *dma	    = dev->dma;
      	drm_i810_private_t *dev_priv = dev->dev_private;

   	d->context = DRM_KERNEL_CONTEXT;
	
	if ((retcode = drm_dma_enqueue(dev, d))) {
		return retcode;
	}
   
   	atomic_inc(&dev_priv->pending_bufs);
	i810_dma_schedule(dev, 1);
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

        DRM_DEBUG("i810_dma start\n");
   	copy_from_user_ret(&d, (drm_dma_t *)arg, sizeof(d), -EFAULT);
	DRM_DEBUG("%d %d: %d send, %d req\n",
		  current->pid, d.context, d.send_count, d.request_count);

	if (d.send_count < 0 || d.send_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to send %d buffers (of %d max)\n",
			  current->pid, d.send_count, dma->buf_count);
		return -EINVAL;
	}
	if (d.request_count < 0 || d.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, d.request_count, dma->buf_count);
		return -EINVAL;
	}

	if (d.send_count) {
	   	retcode = i810_dma_send_buffers(dev, &d);
	}

	d.granted_count = 0;

	if (!retcode && d.request_count) {
		retcode = drm_dma_get_buffers(dev, &d);
	}

	DRM_DEBUG("%d returning, granted = %d\n",
		  current->pid, d.granted_count);
	copy_to_user_ret((drm_dma_t *)arg, &d, sizeof(d), -EFAULT);

   	DRM_DEBUG("i810_dma end (granted)\n");
	return retcode;
}

int i810_irq_install(drm_device_t *dev, int irq)
{
	int retcode;
	u16 temp;
   
	if (!irq)     return -EINVAL;
	
	down(&dev->struct_sem);
	if (dev->irq) {
		up(&dev->struct_sem);
		return -EBUSY;
	}
	dev->irq = irq;
	up(&dev->struct_sem);
	
   	DRM_DEBUG(  "Interrupt Install : %d\n", irq);
	DRM_DEBUG("%d\n", irq);

	dev->context_flag     = 0;
	dev->interrupt_flag   = 0;
	dev->dma_flag	      = 0;
	
	dev->dma->next_buffer = NULL;
	dev->dma->next_queue  = NULL;
	dev->dma->this_buffer = NULL;

	dev->tq.next	      = NULL;
	dev->tq.sync	      = 0;
	dev->tq.routine	      = i810_dma_schedule_tq_wrapper;
	dev->tq.data	      = dev;

				/* Before installing handler */
   	temp = I810_READ16(I810REG_HWSTAM);
   	temp = temp & 0x6000;
   	I810_WRITE16(I810REG_HWSTAM, temp);
   	
      	temp = I810_READ16(I810REG_INT_MASK_R);
   	temp = temp & 0x6000;
   	I810_WRITE16(I810REG_INT_MASK_R, temp); /* Unmask interrupts */
   	temp = I810_READ16(I810REG_INT_ENABLE_R);
   	temp = temp & 0x6000;
      	I810_WRITE16(I810REG_INT_ENABLE_R, temp); /* Disable all interrupts */

				/* Install handler */
	if ((retcode = request_irq(dev->irq,
				   i810_dma_service,
				   0,
				   dev->devname,
				   dev))) {
		down(&dev->struct_sem);
		dev->irq = 0;
		up(&dev->struct_sem);
		return retcode;
	}
   	temp = I810_READ16(I810REG_INT_ENABLE_R);
   	temp = temp & 0x6000;
   	temp = temp | 0x0001;
   	I810_WRITE16(I810REG_INT_ENABLE_R, 
		     0x0001); /* Enable bp interrupts */
	return 0;
}

int i810_irq_uninstall(drm_device_t *dev)
{
	int irq;
   	u16 temp;

	down(&dev->struct_sem);
	irq	 = dev->irq;
	dev->irq = 0;
	up(&dev->struct_sem);
	
	if (!irq) return -EINVAL;

   	DRM_DEBUG(  "Interrupt UnInstall: %d\n", irq);

	
	DRM_DEBUG("%d\n", irq);
   
   	temp = I810_READ16(I810REG_INT_IDENTITY_R);
   	temp = temp & ~(0x6000);
   	if(temp != 0) I810_WRITE16(I810REG_INT_IDENTITY_R, 
				   temp); /* Clear all interrupts */
   
   	temp = I810_READ16(I810REG_INT_ENABLE_R);
   	temp = temp & 0x6000;
   	I810_WRITE16(I810REG_INT_ENABLE_R, 
		     temp);                     /* Disable all interrupts */

   	free_irq(irq, dev);

	return 0;
}

int i810_control(struct inode *inode, struct file *filp, unsigned int cmd,
		  unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_control_t	ctl;
	int		retcode;
   
   	DRM_DEBUG(  "i810_control\n");

	copy_from_user_ret(&ctl, (drm_control_t *)arg, sizeof(ctl), -EFAULT);
	
	switch (ctl.func) {
	case DRM_INST_HANDLER:
		if ((retcode = i810_irq_install(dev, ctl.irq)))
			return retcode;
		break;
	case DRM_UNINST_HANDLER:
		if ((retcode = i810_irq_uninstall(dev)))
			return retcode;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int i810_flush_queue(drm_device_t *dev)
{
   	DECLARE_WAITQUEUE(entry, current);
	drm_queue_t	  *q = dev->queuelist[DRM_KERNEL_CONTEXT];
   	drm_i810_private_t *dev_priv = (drm_i810_private_t *)dev->dev_private;
   	int ret = 0;
   	
   	printk("i810_flush_queue\n");
   	printk("pending_bufs : %d\n", atomic_read(&dev_priv->pending_bufs));
   	if(atomic_read(&dev_priv->pending_bufs) != 0) {
	   printk("got to flush\n");
	   current->state = TASK_INTERRUPTIBLE;
	   add_wait_queue(&q->flush_queue, &entry);
	   for (;;) {
	      	if (!atomic_read(&dev_priv->pending_bufs)) break;
	      	printk("Calling schedule from flush_queue : %d\n",
		       atomic_read(&dev_priv->pending_bufs));
	      	i810_dma_schedule(dev, 0);
	      	schedule();
	      	if (signal_pending(current)) {
		   	ret = -EINTR; /* Can't restart */
		   	break;
		}
	   }
	   printk("Exited out of schedule from flush_queue\n");
	   current->state = TASK_RUNNING;
	   remove_wait_queue(&q->flush_queue, &entry);
	}
   
   	return ret;
}

int i810_lock(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
   	drm_i810_private_t *dev_priv = (drm_i810_private_t *) dev->dev_private;

	DECLARE_WAITQUEUE(entry, current);
	int		  ret	= 0;
	drm_lock_t	  lock;

	copy_from_user_ret(&lock, (drm_lock_t *)arg, sizeof(lock), -EFAULT);

	if (lock.context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  current->pid, lock.context);
		return -EINVAL;
	}
   
   	printk("%d (pid %d) requests lock (0x%08x), flags = 0x%08x\n",
		  lock.context, current->pid, dev->lock.hw_lock->lock,
		  lock.flags);

	if (lock.context < 0) {
		return -EINVAL;
	}
   
   	atomic_inc(&dev_priv->in_flush);
   	printk("in_flush : %d\n", atomic_read(&dev_priv->in_flush));
   	if(atomic_read(&dev_priv->in_flush) != 1) {
	   atomic_dec(&dev_priv->in_flush);
	   add_wait_queue(&dev->lock.lock_queue, &entry);
	   for (;;) {
	      /* Contention */
	      atomic_inc(&dev->total_sleeps);
	      current->state = TASK_INTERRUPTIBLE;
	      current->policy |= SCHED_YIELD;
	      atomic_inc(&dev_priv->in_flush);

	      printk("in_flush_loop : %d\n", atomic_read(&dev_priv->in_flush));
	      if(atomic_read(&dev_priv->in_flush) == 1) {
		 break;
	      }
	      atomic_dec(&dev_priv->in_flush);
	      printk("Calling lock schedule\n");
	      schedule();
	      if (signal_pending(current)) {
		 ret = -ERESTARTSYS;
		 break;
	      }
	   }
	   current->state = TASK_RUNNING;
	   remove_wait_queue(&dev->lock.lock_queue, &entry);
	}
   
   	if (lock.flags & _DRM_LOCK_QUIESCENT) {
	   ret = i810_flush_queue(dev);
	   if(ret != 0) {
	      atomic_dec(&dev_priv->in_flush);
	      wake_up_interruptible(&dev->lock.lock_queue);
	   }
	} else if (ret == 0) {
	   atomic_dec(&dev_priv->in_flush);
	   wake_up_interruptible(&dev->lock.lock_queue);
	}

	/* Only one queue:
	 */

	if (!ret) {
		add_wait_queue(&dev->lock.lock_queue, &entry);
		for (;;) {
			if (!dev->lock.hw_lock) {
				/* Device has been unregistered */
				ret = -EINTR;
				break;
			}
			if (drm_lock_take(&dev->lock.hw_lock->lock,
					  lock.context)) {
				dev->lock.pid	    = current->pid;
				dev->lock.lock_time = jiffies;
				atomic_inc(&dev->total_locks);
				break;	/* Got lock */
			}
			
				/* Contention */
			atomic_inc(&dev->total_sleeps);
			current->state = TASK_INTERRUPTIBLE;
		   	current->policy |= SCHED_YIELD;
		   	printk("Calling lock schedule\n");
			schedule();
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&dev->lock.lock_queue, &entry);
	}
	
	if (!ret) {
		if (lock.flags & _DRM_LOCK_QUIESCENT) {
		   printk("_DRM_LOCK_QUIESCENT\n");
		   i810_dma_quiescent(dev);
		   atomic_dec(&dev_priv->in_flush);
		   wake_up_interruptible(&dev->lock.lock_queue);
		}
	}
	printk("%d %s\n", lock.context, ret ? "interrupted" : "has lock");
	return ret;
}
