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
#include "mga_drv.h"

#include <linux/interrupt.h>	/* For task queue support */

#define MGA_REG(reg)		0 /* for now */
#define MGA_BASE(reg)		((unsigned long) \
				dev->maplist[MGA_REG(reg)]->handle)
#define MGA_ADDR(reg)		(MGA_BASE(reg) + MGA_OFF(reg))
#define MGA_DEREF(reg)		*(__volatile__ int *)MGA_ADDR(reg)
#define MGA_READ(reg)		MGA_DEREF(reg)
#define MGA_WRITE(reg,val) 	do { MGA_DEREF(reg) = val; } while (0)

typedef _mga_primary_buffer {
	u32 *head;
	u32 *dma_ptr;
	u8 tempIndex[4];
	int outcount;
	int num_dwords;
	int max_dwords;
	unsigned long phys_head;
} mgaPrimBuf;

static mgaPrimBuf buffer;
typedef enum {
	TT_GENERAL,
	TT_BLIT,
	TT_VECTOR,
	TT_VERTEX
} transferType_t;

#define DWGREG0 	0x1c00
#define DWGREG0_END 	0x1dff

#define ISREG0(r)	(r >= DWGREG0 && r <= DWGREG0_END)
#define ADRINDEX0(r)	(u8)((r - DWGREG0) >> 2)
#define ADRINDEX1(r)	(u8)(((r - DWGREG1) >> 2) | 0x80)
#define ADRINDEX(r)	(ISREG0(r) ? ADRINDEX0(r) : ADRINDEX1(r)) 

#define DMAOUTREG(reg, val) do {			\
buffer.tempIndex[buffer.outcount]=ADRINDEX(reg);	\
buffer.dma_ptr[1+buffer.outcount] = val;		\
if( ++buffer.outcount == 4) {				\
buffer.outcount = 0;					\
buffer.dma_ptr[0] = *(u32 *)tempIndex;			\
buffer.dma_ptr+=5;					\
buffer.num_dwords += 5;					\
}							\
}while (0)

#define CHECK_OVERFLOW(length) do {			\
if((buffer.max_dwords - buffer.num_dwords) < length) {	\
	mga_prim_overflow();				\
}							\
}while(0)

#define PDEA_pagpxfer_enable 0x2

static inline void i810_dma_dispatch(drm_device_t *dev, unsigned long address,
				      unsigned long length)
{
	transferType_t transferType = TT_GENERAL;
	int use_agp = PDEA_pagpxfer_enable;

	CHECK_OVERFLOW(10);
	DMAOUTREG(MGAREG_DMAPAD, 0);
	DMAOUTREG(MGAREG_DMAPAD, 0);
	DMAOUTREG(MGAREG_SECADDRESS, address | transferType);
	DMAOUTREG(MGAREG_SECEND, (address + length) | use_agp);
	DMAOUTREG(MGAREG_DMAPAD, 0);
	DMAOUTREG(MGAREG_DMAPAD, 0);
	DMAOUTREG(MGAREG_DWGSYNC, 0);
	DMAOUTREG(MGAREG_SOFTRAP, 0);
	/* Needs to write out to PRIMEND */
}

static inline void i810_dma_quiescent(drm_device_t *dev)
{
#if 0
	while (GAMMA_READ(GAMMA_DMACOUNT))
		;
	while (GAMMA_READ(GAMMA_INFIFOSPACE) < 3)
		;
	GAMMA_WRITE(GAMMA_BROADCASTMASK, 3);
	GAMMA_WRITE(GAMMA_FILTERMODE, 1 << 10);
	GAMMA_WRITE(GAMMA_SYNC, 0);
	
				/* Read from first MX */
	do {
		while (!GAMMA_READ(GAMMA_OUTFIFOWORDS))
			;
	} while (GAMMA_READ(GAMMA_OUTPUTFIFO) != GAMMA_SYNC_TAG);
	

				/* Read from second MX */
	do {
		while (!GAMMA_READ(GAMMA_OUTFIFOWORDS + 0x10000))
			;
	} while (GAMMA_READ(GAMMA_OUTPUTFIFO + 0x10000) != GAMMA_SYNC_TAG);
#endif
}

static inline void i810_dma_ready(drm_device_t *dev)
{
#if 0
	while (GAMMA_READ(GAMMA_DMACOUNT))
		;
#endif
}

static inline int i810_dma_is_ready(drm_device_t *dev)
{
#if 0
	return !GAMMA_READ(GAMMA_DMACOUNT);
#endif
}


static void i810_dma_service(int irq, void *device, struct pt_regs *regs)
{
#if 0
	drm_device_t	 *dev = (drm_device_t *)device;
	drm_device_dma_t *dma = dev->dma;
	
	atomic_inc(&dev->total_irq);
	GAMMA_WRITE(GAMMA_GDELAYTIMER, 0xc350/2); /* 0x05S */
	GAMMA_WRITE(GAMMA_GCOMMANDINTFLAGS, 8);
	GAMMA_WRITE(GAMMA_GINTFLAGS, 0x2001);
	if (mga_dma_is_ready(dev)) {
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
#endif
}

/* Only called by i810_dma_schedule. */
static int i810_do_dma(drm_device_t *dev, int locked)
{
	unsigned long	 address;
	unsigned long	 length;
	drm_buf_t	 *buf;
	int		 retcode = 0;
	drm_device_dma_t *dma = dev->dma;
#if DRM_DMA_HISTOGRAM
	cycles_t	 dma_start, dma_stop;
#endif

	if (test_and_set_bit(0, &dev->dma_flag)) {
		atomic_inc(&dma->total_missed_dma);
		return -EBUSY;
	}
	
#if DRM_DMA_HISTOGRAM
	dma_start = get_cycles();
#endif

	if (!dma->next_buffer) {
		DRM_ERROR("No next_buffer\n");
		clear_bit(0, &dev->dma_flag);
		return -EINVAL;
	}

	buf	= dma->next_buffer;
	address = (unsigned long)buf->address;
	length	= buf->used;

	DRM_DEBUG("context %d, buffer %d (%ld bytes)\n",
		  buf->context, buf->idx, length);

	if (buf->list == DRM_LIST_RECLAIM) {
		drm_clear_next_buffer(dev);
		drm_free_buffer(dev, buf);
		clear_bit(0, &dev->dma_flag);
		return -EINVAL;
	}

	if (!length) {
		DRM_ERROR("0 length buffer\n");
		drm_clear_next_buffer(dev);
		drm_free_buffer(dev, buf);
		clear_bit(0, &dev->dma_flag);
		return 0;
	}
	
	if (!mga_dma_is_ready(dev)) {
		clear_bit(0, &dev->dma_flag);
		return -EBUSY;
	}

	if (buf->while_locked) {
		if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
			DRM_ERROR("Dispatching buffer %d from pid %d"
				  " \"while locked\", but no lock held\n",
				  buf->idx, buf->pid);
		}
	} else {
		if (!locked && !drm_lock_take(&dev->lock.hw_lock->lock,
					      DRM_KERNEL_CONTEXT)) {
			atomic_inc(&dma->total_missed_lock);
			clear_bit(0, &dev->dma_flag);
			return -EBUSY;
		}
	}

	if (dev->last_context != buf->context
	    && !(dev->queuelist[buf->context]->flags
		 & _DRM_CONTEXT_PRESERVED)) {
				/* PRE: dev->last_context != buf->context */
		if (drm_context_switch(dev, dev->last_context, buf->context)) {
			drm_clear_next_buffer(dev);
			drm_free_buffer(dev, buf);
		}
		retcode = -EBUSY;
		goto cleanup;
			
				/* POST: we will wait for the context
				   switch and will dispatch on a later call
				   when dev->last_context == buf->context.
				   NOTE WE HOLD THE LOCK THROUGHOUT THIS
				   TIME! */
	}

	drm_clear_next_buffer(dev);
	buf->pending	 = 1;
	buf->waiting	 = 0;
	buf->list	 = DRM_LIST_PEND;
#if DRM_DMA_HISTOGRAM
	buf->time_dispatched = get_cycles();
#endif

	mga_dma_dispatch(dev, address, length);
	drm_free_buffer(dev, dma->this_buffer);
	dma->this_buffer = buf;

	atomic_add(length, &dma->total_bytes);
	atomic_inc(&dma->total_dmas);

	if (!buf->while_locked && !dev->context_flag && !locked) {
		if (drm_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
		}
	}
cleanup:

	clear_bit(0, &dev->dma_flag);

#if DRM_DMA_HISTOGRAM
	dma_stop = get_cycles();
	atomic_inc(&dev->histo.dma[drm_histogram_slot(dma_stop - dma_start)]);
#endif

	return retcode;
}

static void i810_dma_schedule_timer_wrapper(unsigned long dev)
{
	i810_dma_schedule((drm_device_t *)dev, 0);
}

static void i810_dma_schedule_tq_wrapper(void *dev)
{
	i810_dma_schedule(dev, 0);
}

int i810_dma_schedule(drm_device_t *dev, int locked)
{
#if 0
	int		 next;
	drm_queue_t	 *q;
	drm_buf_t	 *buf;
	int		 retcode   = 0;
	int		 processed = 0;
	int		 missed;
	int		 expire	   = 20;
	drm_device_dma_t *dma	   = dev->dma;
#if DRM_DMA_HISTOGRAM
	cycles_t	 schedule_start;
#endif

	if (test_and_set_bit(0, &dev->interrupt_flag)) {
				/* Not reentrant */
		atomic_inc(&dma->total_missed_sched);
		return -EBUSY;
	}
	missed = atomic_read(&dma->total_missed_sched);

#if DRM_DMA_HISTOGRAM
	schedule_start = get_cycles();
#endif

again:
	if (dev->context_flag) {
		clear_bit(0, &dev->interrupt_flag);
		return -EBUSY;
	}
	if (dma->next_buffer) {
				/* Unsent buffer that was previously
				   selected, but that couldn't be sent
				   because the lock could not be obtained
				   or the DMA engine wasn't ready.  Try
				   again. */
		atomic_inc(&dma->total_tried);
		if (!(retcode = mga_do_dma(dev, locked))) {
			atomic_inc(&dma->total_hit);
			++processed;
		}
	} else {
		do {
			next = drm_select_queue(dev,
					     mga_dma_schedule_timer_wrapper);
			if (next >= 0) {
				q   = dev->queuelist[next];
				buf = drm_waitlist_get(&q->waitlist);
				dma->next_buffer = buf;
				dma->next_queue	 = q;
				if (buf && buf->list == DRM_LIST_RECLAIM) {
					drm_clear_next_buffer(dev);
					drm_free_buffer(dev, buf);
				}
			}
		} while (next >= 0 && !dma->next_buffer);
		if (dma->next_buffer) {
			if (!(retcode = mga_do_dma(dev, locked))) {
				++processed;
			}
		}
	}

	if (--expire) {
		if (missed != atomic_read(&dma->total_missed_sched)) {
			atomic_inc(&dma->total_lost);
			if (mga_dma_is_ready(dev)) goto again;
		}
		if (processed && mga_dma_is_ready(dev)) {
			atomic_inc(&dma->total_lost);
			processed = 0;
			goto again;
		}
	}
	
	clear_bit(0, &dev->interrupt_flag);
	
#if DRM_DMA_HISTOGRAM
	atomic_inc(&dev->histo.schedule[drm_histogram_slot(get_cycles()
							   - schedule_start)]);
#endif
	return retcode;
#endif
}

static int i810_dma_priority(drm_device_t *dev, drm_dma_t *d)
{
	unsigned long	  address;
	unsigned long	  length;
	int		  must_free = 0;
	int		  retcode   = 0;
	int		  i;
	int		  idx;
	drm_buf_t	  *buf;
	drm_buf_t	  *last_buf = NULL;
	drm_device_dma_t  *dma	    = dev->dma;
	DECLARE_WAITQUEUE(entry, current);

				/* Turn off interrupt handling */
	while (test_and_set_bit(0, &dev->interrupt_flag)) {
		schedule();
		if (signal_pending(current)) return -EINTR;
	}
	if (!(d->flags & _DRM_DMA_WHILE_LOCKED)) {
		while (!drm_lock_take(&dev->lock.hw_lock->lock,
				      DRM_KERNEL_CONTEXT)) {
			schedule();
			if (signal_pending(current)) {
				clear_bit(0, &dev->interrupt_flag);
				return -EINTR;
			}
		}
		++must_free;
	}
	atomic_inc(&dma->total_prio);

	for (i = 0; i < d->send_count; i++) {
		idx = d->send_indices[i];
		if (idx < 0 || idx >= dma->buf_count) {
			DRM_ERROR("Index %d (of %d max)\n",
				  d->send_indices[i], dma->buf_count - 1);
			continue;
		}
		buf = dma->buflist[ idx ];
		if (buf->pid != current->pid) {
			DRM_ERROR("Process %d using buffer owned by %d\n",
				  current->pid, buf->pid);
			retcode = -EINVAL;
			goto cleanup;
		}
		if (buf->list != DRM_LIST_NONE) {
			DRM_ERROR("Process %d using %d's buffer on list %d\n",
				  current->pid, buf->pid, buf->list);
			retcode = -EINVAL;
			goto cleanup;
		}
				/* This isn't a race condition on
				   buf->list, since our concern is the
				   buffer reclaim during the time the
				   process closes the /dev/drm? handle, so
				   it can't also be doing DMA. */
		buf->list	  = DRM_LIST_PRIO;
		buf->used	  = d->send_sizes[i];
		buf->context	  = d->context;
		buf->while_locked = d->flags & _DRM_DMA_WHILE_LOCKED;
		address		  = (unsigned long)buf->address;
		length		  = buf->used;
		if (!length) {
			DRM_ERROR("0 length buffer\n");
		}
		if (buf->pending) {
			DRM_ERROR("Sending pending buffer:"
				  " buffer %d, offset %d\n",
				  d->send_indices[i], i);
			retcode = -EINVAL;
			goto cleanup;
		}
		if (buf->waiting) {
			DRM_ERROR("Sending waiting buffer:"
				  " buffer %d, offset %d\n",
				  d->send_indices[i], i);
			retcode = -EINVAL;
			goto cleanup;
		}
		buf->pending = 1;
		
		if (dev->last_context != buf->context
		    && !(dev->queuelist[buf->context]->flags
			 & _DRM_CONTEXT_PRESERVED)) {
			add_wait_queue(&dev->context_wait, &entry);
			current->state = TASK_INTERRUPTIBLE;
				/* PRE: dev->last_context != buf->context */
			drm_context_switch(dev, dev->last_context,
					   buf->context);
				/* POST: we will wait for the context
				   switch and will dispatch on a later call
				   when dev->last_context == buf->context.
				   NOTE WE HOLD THE LOCK THROUGHOUT THIS
				   TIME! */
			schedule();
			current->state = TASK_RUNNING;
			remove_wait_queue(&dev->context_wait, &entry);
			if (signal_pending(current)) {
				retcode = -EINTR;
				goto cleanup;
			}
			if (dev->last_context != buf->context) {
				DRM_ERROR("Context mismatch: %d %d\n",
					  dev->last_context,
					  buf->context);
			}
		}

#if DRM_DMA_HISTOGRAM
		buf->time_queued     = get_cycles();
		buf->time_dispatched = buf->time_queued;
#endif
		mga_dma_dispatch(dev, address, length);
		atomic_add(length, &dma->total_bytes);
		atomic_inc(&dma->total_dmas);
		
		if (last_buf) {
			drm_free_buffer(dev, last_buf);
		}
		last_buf = buf;
	}


cleanup:
	if (last_buf) {
		mga_dma_ready(dev);
		drm_free_buffer(dev, last_buf);
	}
	
	if (must_free && !dev->context_flag) {
		if (drm_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
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

	if (d->flags & _DRM_DMA_BLOCK) {
		last_buf = dma->buflist[d->send_indices[d->send_count-1]];
		add_wait_queue(&last_buf->dma_wait, &entry);
	}
	
	if ((retcode = drm_dma_enqueue(dev, d))) {
		if (d->flags & _DRM_DMA_BLOCK)
			remove_wait_queue(&last_buf->dma_wait, &entry);
		return retcode;
	}
	
	mga_dma_schedule(dev, 0);
	
	if (d->flags & _DRM_DMA_BLOCK) {
		DRM_DEBUG("%d waiting\n", current->pid);
		current->state = TASK_INTERRUPTIBLE;
		for (;;) {
			if (!last_buf->waiting
			    && !last_buf->pending)
				break; /* finished */
			schedule();
			if (signal_pending(current)) {
				retcode = -EINTR; /* Can't restart */
				break;
			}
		}
		current->state = TASK_RUNNING;
		DRM_DEBUG("%d running\n", current->pid);
		remove_wait_queue(&last_buf->dma_wait, &entry);
		if (!retcode
		    || (last_buf->list==DRM_LIST_PEND && !last_buf->pending)) {
			if (!waitqueue_active(&last_buf->dma_wait)) {
				drm_free_buffer(dev, last_buf);
			}
		}
		if (retcode) {
			DRM_ERROR("ctx%d w%d p%d c%d i%d l%d %d/%d\n",
				  d->context,
				  last_buf->waiting,
				  last_buf->pending,
				  DRM_WAITCOUNT(dev, d->context),
				  last_buf->idx,
				  last_buf->list,
				  last_buf->pid,
				  current->pid);
		}
	}
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

	if (d.context == DRM_KERNEL_CONTEXT || d.context >= dev->queue_slots) {
		DRM_ERROR("Process %d using context %d\n",
			  current->pid, d.context);
		return -EINVAL;
	}
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
		if (d.flags & _DRM_DMA_PRIORITY)
			retcode = mga_dma_priority(dev, &d);
		else 
			retcode = mga_dma_send_buffers(dev, &d);
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

int i810_irq_install(drm_device_t *dev, int irq)
{
	int retcode;

	if (!irq)     return -EINVAL;
	
	down(&dev->struct_sem);
	if (dev->irq) {
		up(&dev->struct_sem);
		return -EBUSY;
	}
	dev->irq = irq;
	up(&dev->struct_sem);
	
	DRM_DEBUG("%d\n", irq);

	dev->context_flag     = 0;
	dev->interrupt_flag   = 0;
	dev->dma_flag	      = 0;
	
	dev->dma->next_buffer = NULL;
	dev->dma->next_queue  = NULL;
	dev->dma->this_buffer = NULL;

	dev->tq.next	      = NULL;
	dev->tq.sync	      = 0;
	dev->tq.routine	      = mga_dma_schedule_tq_wrapper;
	dev->tq.data	      = dev;


				/* Before installing handler */
	MGA_WRITE(MGAREG_IEN, 0);
	
				/* Install handler */
	if ((retcode = request_irq(dev->irq,
				   mga_dma_service,
				   0,
				   dev->devname,
				   dev))) {
		down(&dev->struct_sem);
		dev->irq = 0;
		up(&dev->struct_sem);
		return retcode;
	}

				/* After installing handler */
	MGA_WRITE(MGAREG_IEN, 0x00000001);
	return 0;
}

int i810_irq_uninstall(drm_device_t *dev)
{
	int irq;

	down(&dev->struct_sem);
	irq	 = dev->irq;
	dev->irq = 0;
	up(&dev->struct_sem);
	
	if (!irq) return -EINVAL;
	
	DRM_DEBUG("%d\n", irq);
	
	MGA_WRITE(MGAREG_IEN, 0);
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
	
	copy_from_user_ret(&ctl, (drm_control_t *)arg, sizeof(ctl), -EFAULT);
	
	switch (ctl.func) {
	case DRM_INST_HANDLER:
		if ((retcode = mga_irq_install(dev, ctl.irq)))
			return retcode;
		break;
	case DRM_UNINST_HANDLER:
		if ((retcode = mga_irq_uninstall(dev)))
			return retcode;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int i810_lock(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	DECLARE_WAITQUEUE(entry, current);
	int		  ret	= 0;
	drm_lock_t	  lock;
	drm_queue_t	  *q;
#if DRM_DMA_HISTOGRAM
	cycles_t	  start;

	dev->lck_start = start = get_cycles();
#endif

	copy_from_user_ret(&lock, (drm_lock_t *)arg, sizeof(lock), -EFAULT);

	if (lock.context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  current->pid, lock.context);
		return -EINVAL;
	}

	DRM_DEBUG("%d (pid %d) requests lock (0x%08x), flags = 0x%08x\n",
		  lock.context, current->pid, dev->lock.hw_lock->lock,
		  lock.flags);

	if (lock.context < 0 || lock.context >= dev->queue_count)
		return -EINVAL;
	q = dev->queuelist[lock.context];
	
	ret = drm_flush_block_and_flush(dev, lock.context, lock.flags);

	if (!ret) {
		if (_DRM_LOCKING_CONTEXT(dev->lock.hw_lock->lock)
		    != lock.context) {
			long j = jiffies - dev->lock.lock_time;

			if (j > 0 && j <= DRM_LOCK_SLICE) {
				/* Can't take lock if we just had it and
				   there is contention. */
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(j);
			}
		}
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
				atomic_inc(&q->total_locks);
				break;	/* Got lock */
			}
			
				/* Contention */
			atomic_inc(&dev->total_sleeps);
			current->state = TASK_INTERRUPTIBLE;
			schedule();
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&dev->lock.lock_queue, &entry);
	}

	drm_flush_unblock(dev, lock.context, lock.flags); /* cleanup phase */
	
	if (!ret) {
		if (lock.flags & _DRM_LOCK_READY)
			mga_dma_ready(dev);
		if (lock.flags & _DRM_LOCK_QUIESCENT)
			mga_dma_quiescent(dev);
	}
	DRM_DEBUG("%d %s\n", lock.context, ret ? "interrupted" : "has lock");

#if DRM_DMA_HISTOGRAM
	atomic_inc(&dev->histo.lacq[drm_histogram_slot(get_cycles() - start)]);
#endif
	
	return ret;
}
