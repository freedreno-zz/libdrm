/* mga_dma.c -- DMA support for mga g200/g400 -*- linux-c -*-
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
 * Authors:
 *   Rickard E. (Rik) Faith <faith@valinux.com>
 *   Jeff Hartmann <jhartmann@valinux.com>
 *   Keith Whitwell <keithw@valinux.com>
 *
 * Rewritten by:
 *   Gareth Hughes <gareth@valinux.com>
 */

#define __NO_VERSION__
#include "mga.h"
#include "drmP.h"
#include "mga_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

#define MGA_DEFAULT_USEC_TIMEOUT	10000


#define DO_IOREMAP( _map )						\
do {									\
	(_map)->handle = DRM(ioremap)( (_map)->offset, (_map)->size );	\
} while (0)

#define DO_IOREMAPFREE( _map )						\
do {									\
	if ( (_map)->handle && (_map)->size )				\
		DRM(ioremapfree)( (_map)->handle, (_map)->size );	\
} while (0)

#define DO_FIND_MAP( _map, _offset )					\
do {									\
	int _i;								\
	for ( _i = 0 ; _i < dev->map_count ; _i++ ) {			\
		if ( dev->maplist[_i]->offset == _offset ) {		\
			_map = dev->maplist[_i];			\
			break;						\
		}							\
	}								\
} while (0)



#if 0


#define MGA_REG(reg)		2
#define MGA_BASE(reg)		((unsigned long) \
				((drm_device_t *)dev)->maplist[MGA_REG(reg)]->handle)
#define MGA_ADDR(reg)		(MGA_BASE(reg) + reg)
#define MGA_DEREF(reg)		*(__volatile__ int *)MGA_ADDR(reg)
#define MGA_READ(reg)		MGA_DEREF(reg)
#define MGA_WRITE(reg,val)	do { MGA_DEREF(reg) = val; } while (0)

#define PDEA_pagpxfer_enable	     0x2

static int mga_flush_queue(drm_device_t *dev);

static unsigned long mga_alloc_page(drm_device_t *dev)
{
	unsigned long address;

	address = __get_free_page(GFP_KERNEL);
	if(address == 0UL) {
		return 0;
	}
	atomic_inc(&virt_to_page(address)->count);
	set_bit(PG_reserved, &virt_to_page(address)->flags);

	return address;
}

static void mga_free_page(drm_device_t *dev, unsigned long page)
{
	if(!page) return;
	atomic_dec(&virt_to_page(page)->count);
	clear_bit(PG_reserved, &virt_to_page(page)->flags);
	free_page(page);
	return;
}

static void mga_delay(void)
{
	return;
}

/* These are two age tags that will never be sent to
 * the hardware */
#define MGA_BUF_USED	0xffffffff
#define MGA_BUF_FREE	0

static int mga_freelist_init(drm_device_t *dev)
{
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_freelist_t *item;
	int i;

	dev_priv->head = DRM(alloc)(sizeof(drm_mga_freelist_t),
				    DRM_MEM_DRIVER);
	if(dev_priv->head == NULL) return -ENOMEM;
	memset(dev_priv->head, 0, sizeof(drm_mga_freelist_t));
	dev_priv->head->age = MGA_BUF_USED;

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[ i ];
	        buf_priv = buf->dev_private;
		item = drm_alloc(sizeof(drm_mga_freelist_t),
				 DRM_MEM_DRIVER);
		if(item == NULL) return -ENOMEM;
		memset(item, 0, sizeof(drm_mga_freelist_t));
		item->age = MGA_BUF_FREE;
		item->prev = dev_priv->head;
		item->next = dev_priv->head->next;
		if(dev_priv->head->next != NULL)
			dev_priv->head->next->prev = item;
		if(item->next == NULL) dev_priv->tail = item;
		item->buf = buf;
		buf_priv->my_freelist = item;
		buf_priv->discard = 0;
		buf_priv->dispatched = 0;
		dev_priv->head->next = item;
	}

	return 0;
}

static void mga_freelist_cleanup(drm_device_t *dev)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_freelist_t *item;
	drm_mga_freelist_t *prev;

	item = dev_priv->head;
	while(item) {
		prev = item;
		item = item->next;
		drm_free(prev, sizeof(drm_mga_freelist_t), DRM_MEM_DRIVER);
	}

	dev_priv->head = dev_priv->tail = NULL;
}

/* Frees dispatch lock */
static inline void mga_dma_quiescent(drm_device_t *dev)
{
	drm_device_dma_t  *dma      = dev->dma;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned long end;
	int i;

	DRM_DEBUG("dispatch_status = 0x%02x\n", dev_priv->dispatch_status);
	end = jiffies + (HZ*3);
	while(1) {
		if(!test_and_set_bit(MGA_IN_DISPATCH,
				     &dev_priv->dispatch_status)) {
			break;
		}
		if((signed)(end - jiffies) <= 0) {
			DRM_ERROR("irqs: %d wanted %d\n",
				  atomic_read(&dev->total_irq),
				  atomic_read(&dma->total_lost));
			DRM_ERROR("lockup: dispatch_status = 0x%02x,"
				  " jiffies = %lu, end = %lu\n",
				  dev_priv->dispatch_status, jiffies, end);
			return;
		}
		for (i = 0 ; i < 2000 ; i++) mga_delay();
	}
	end = jiffies + (HZ*3);
	DRM_DEBUG("quiescent status : %x\n", MGA_READ(MGAREG_STATUS));
	while((MGA_READ(MGAREG_STATUS) & 0x00030001) != 0x00020000) {
		if((signed)(end - jiffies) <= 0) {
			DRM_ERROR("irqs: %d wanted %d\n",
				  atomic_read(&dev->total_irq),
				  atomic_read(&dma->total_lost));
			DRM_ERROR("lockup\n");
			clear_bit(MGA_IN_DISPATCH, &dev_priv->dispatch_status);
			return;
		}
		for (i = 0 ; i < 2000 ; i++) mga_delay();
	}
	sarea_priv->dirty |= MGA_DMA_FLUSH;

	clear_bit(MGA_IN_DISPATCH, &dev_priv->dispatch_status);
	DRM_DEBUG("exit, dispatch_status = 0x%02x\n",
		  dev_priv->dispatch_status);
}

static void mga_reset_freelist(drm_device_t *dev)
{
	drm_device_dma_t  *dma      = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	int i;

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[ i ];
	        buf_priv = buf->dev_private;
		buf_priv->my_freelist->age = MGA_BUF_FREE;
	}
}

/* Least recently used :
 * These operations are not atomic b/c they are protected by the
 * hardware lock */

drm_buf_t *mga_freelist_get(drm_device_t *dev)
{
	DECLARE_WAITQUEUE(entry, current);
	drm_mga_private_t *dev_priv =
		(drm_mga_private_t *) dev->dev_private;
	drm_mga_freelist_t *prev;
	drm_mga_freelist_t *next;
	static int failed = 0;
	int return_null = 0;

	if(failed >= 1000 && dev_priv->tail->age >= dev_priv->last_prim_age) {
		DRM_DEBUG("Waiting on freelist,"
			  " tail->age = %d, last_prim_age= %d\n",
			  dev_priv->tail->age,
			  dev_priv->last_prim_age);
		add_wait_queue(&dev_priv->buf_queue, &entry);
		set_bit(MGA_IN_GETBUF, &dev_priv->dispatch_status);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			mga_dma_schedule(dev, 0);
			if(dev_priv->tail->age < dev_priv->last_prim_age)
				break;
			atomic_inc(&dev->total_sleeps);
			schedule();
			if (signal_pending(current)) {
				++return_null;
				break;
			}
		}
		clear_bit(MGA_IN_GETBUF, &dev_priv->dispatch_status);
		current->state = TASK_RUNNING;
		remove_wait_queue(&dev_priv->buf_queue, &entry);
		if (return_null) return NULL;
	}

	if(dev_priv->tail->age < dev_priv->last_prim_age) {
		prev = dev_priv->tail->prev;
		next = dev_priv->tail;
		prev->next = NULL;
		next->prev = next->next = NULL;
		dev_priv->tail = prev;
		next->age = MGA_BUF_USED;
		failed = 0;
		return next->buf;
	}

	failed++;
	return NULL;
}

int mga_freelist_put(drm_device_t *dev, drm_buf_t *buf)
{
	drm_mga_private_t *dev_priv =
		(drm_mga_private_t *) dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_freelist_t *prev;
	drm_mga_freelist_t *head;
	drm_mga_freelist_t *next;

	if(buf_priv->my_freelist->age == MGA_BUF_USED) {
		/* Discarded buffer, put it on the tail */
		next = buf_priv->my_freelist;
		next->age = MGA_BUF_FREE;
		prev = dev_priv->tail;
		prev->next = next;
		next->prev = prev;
		next->next = NULL;
		dev_priv->tail = next;
	} else {
		/* Normally aged buffer, put it on the head + 1,
		 * as the real head is a sentinal element
		 */
		next = buf_priv->my_freelist;
		head = dev_priv->head;
		prev = head->next;
		head->next = next;
		prev->prev = next;
		next->prev = head;
		next->next = prev;
	}

	return 0;
}

static int mga_init_primary_bufs(drm_device_t *dev, drm_mga_init_t *init)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_prim_buf_t *prim_buffer;
	int i, temp, size_of_buf;
	int offset = init->reserved_map_agpstart;

	dev_priv->primary_size = ((init->primary_size + PAGE_SIZE - 1) /
				  PAGE_SIZE) * PAGE_SIZE;
	size_of_buf = dev_priv->primary_size / MGA_NUM_PRIM_BUFS;
	dev_priv->warp_ucode_size = init->warp_ucode_size;
	dev_priv->prim_bufs = drm_alloc(sizeof(drm_mga_prim_buf_t *) *
					(MGA_NUM_PRIM_BUFS + 1),
					DRM_MEM_DRIVER);
	if(dev_priv->prim_bufs == NULL) {
		DRM_ERROR("Unable to allocate memory for prim_buf\n");
		return -ENOMEM;
	}
	memset(dev_priv->prim_bufs,
	       0, sizeof(drm_mga_prim_buf_t *) * (MGA_NUM_PRIM_BUFS + 1));

	temp = init->warp_ucode_size + dev_priv->primary_size;
	temp = ((temp + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

	dev_priv->ioremap = drm_ioremap(dev->agp->base + offset,
					temp);
	if(dev_priv->ioremap == NULL) {
		DRM_ERROR("Ioremap failed\n");
		return -ENOMEM;
	}
	init_waitqueue_head(&dev_priv->wait_queue);

	for(i = 0; i < MGA_NUM_PRIM_BUFS; i++) {
		prim_buffer = drm_alloc(sizeof(drm_mga_prim_buf_t),
					DRM_MEM_DRIVER);
		if(prim_buffer == NULL) return -ENOMEM;
		memset(prim_buffer, 0, sizeof(drm_mga_prim_buf_t));
		prim_buffer->phys_head = offset + dev->agp->base;
		prim_buffer->current_dma_ptr =
			prim_buffer->head =
			(u32 *) (dev_priv->ioremap +
				 offset -
				 init->reserved_map_agpstart);
		prim_buffer->num_dwords = 0;
		prim_buffer->max_dwords = size_of_buf / sizeof(u32);
		prim_buffer->max_dwords -= 5; /* Leave room for the softrap */
		prim_buffer->sec_used = 0;
		prim_buffer->idx = i;
		prim_buffer->prim_age = i + 1;
		offset = offset + size_of_buf;
		dev_priv->prim_bufs[i] = prim_buffer;
	}
	dev_priv->current_prim_idx = 0;
        dev_priv->next_prim =
		dev_priv->last_prim =
		dev_priv->current_prim =
	dev_priv->prim_bufs[0];
	dev_priv->next_prim_age = 2;
	dev_priv->last_prim_age = 1;
	set_bit(MGA_BUF_IN_USE, &dev_priv->current_prim->buffer_status);
	return 0;
}

void mga_fire_primary(drm_device_t *dev, drm_mga_prim_buf_t *prim)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t  *dma	    = dev->dma;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int use_agp = PDEA_pagpxfer_enable;
	unsigned long end;
	int i;
	int next_idx;
	PRIMLOCALS;

	dev_priv->last_prim = prim;

	/* We never check for overflow, b/c there is always room */
	PRIMPTR(prim);
	if(num_dwords <= 0) {
		DRM_ERROR("num_dwords == 0 when dispatched\n");
		goto out_prim_wait;
	}
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_SOFTRAP, 0);
	PRIMFINISH(prim);

	end = jiffies + (HZ*3);
	if(sarea_priv->dirty & MGA_DMA_FLUSH) {
		while((MGA_READ(MGAREG_STATUS) & 0x00030001) != 0x00020000) {
			if((signed)(end - jiffies) <= 0) {
				DRM_ERROR("irqs: %d wanted %d\n",
					  atomic_read(&dev->total_irq),
					  atomic_read(&dma->total_lost));
				DRM_ERROR("lockup (flush)\n");
				goto out_prim_wait;
			}

			for (i = 0 ; i < 4096 ; i++) mga_delay();
		}
		sarea_priv->dirty &= ~(MGA_DMA_FLUSH);
	} else {
		while((MGA_READ(MGAREG_STATUS) & 0x00020001) != 0x00020000) {
			if((signed)(end - jiffies) <= 0) {
				DRM_ERROR("irqs: %d wanted %d\n",
					  atomic_read(&dev->total_irq),
					  atomic_read(&dma->total_lost));
				DRM_ERROR("lockup (wait)\n");
				goto out_prim_wait;
			}

			for (i = 0 ; i < 4096 ; i++) mga_delay();
		}
	}

	mga_flush_write_combine();
	atomic_inc(&dev_priv->pending_bufs);
	MGA_WRITE(MGAREG_PRIMADDRESS, phys_head | TT_GENERAL);
	MGA_WRITE(MGAREG_PRIMEND, (phys_head + num_dwords * 4) | use_agp);
	prim->num_dwords = 0;
	sarea_priv->last_enqueue = prim->prim_age;

	next_idx = prim->idx + 1;
	if(next_idx >= MGA_NUM_PRIM_BUFS)
		next_idx = 0;

	dev_priv->next_prim = dev_priv->prim_bufs[next_idx];
	return;

 out_prim_wait:
	prim->num_dwords = 0;
	prim->sec_used = 0;
	clear_bit(MGA_BUF_IN_USE, &prim->buffer_status);
	wake_up_interruptible(&dev_priv->wait_queue);
	clear_bit(MGA_BUF_SWAP_PENDING, &prim->buffer_status);
	clear_bit(MGA_IN_DISPATCH, &dev_priv->dispatch_status);
}

int mga_advance_primary(drm_device_t *dev)
{
	DECLARE_WAITQUEUE(entry, current);
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_prim_buf_t *prim_buffer;
	drm_device_dma_t  *dma      = dev->dma;
	int next_prim_idx;
	int ret = 0;

	/* This needs to reset the primary buffer if available,
	 * we should collect stats on how many times it bites
	 * it's tail */

	next_prim_idx = dev_priv->current_prim_idx + 1;
	if(next_prim_idx >= MGA_NUM_PRIM_BUFS)
		next_prim_idx = 0;
	prim_buffer = dev_priv->prim_bufs[next_prim_idx];
	set_bit(MGA_IN_WAIT, &dev_priv->dispatch_status);

	/* In use is cleared in interrupt handler */

	if(test_and_set_bit(MGA_BUF_IN_USE, &prim_buffer->buffer_status)) {
		add_wait_queue(&dev_priv->wait_queue, &entry);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			mga_dma_schedule(dev, 0);
			if(!test_and_set_bit(MGA_BUF_IN_USE,
					     &prim_buffer->buffer_status))
				break;
			atomic_inc(&dev->total_sleeps);
			atomic_inc(&dma->total_missed_sched);
			schedule();
			if (signal_pending(current)) {
				ret = -ERESTARTSYS;
				break;
			}
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&dev_priv->wait_queue, &entry);
		if(ret) return ret;
	}
	clear_bit(MGA_IN_WAIT, &dev_priv->dispatch_status);

	/* This primary buffer is now free to use */
	prim_buffer->current_dma_ptr = prim_buffer->head;
	prim_buffer->num_dwords = 0;
	prim_buffer->sec_used = 0;
	prim_buffer->prim_age = dev_priv->next_prim_age++;
	if(prim_buffer->prim_age == 0 || prim_buffer->prim_age == 0xffffffff) {
		mga_flush_queue(dev);
		mga_dma_quiescent(dev);
		mga_reset_freelist(dev);
		prim_buffer->prim_age = (dev_priv->next_prim_age += 2);
	}

	/* Reset all buffer status stuff */
	clear_bit(MGA_BUF_NEEDS_OVERFLOW, &prim_buffer->buffer_status);
	clear_bit(MGA_BUF_FORCE_FIRE, &prim_buffer->buffer_status);
	clear_bit(MGA_BUF_SWAP_PENDING, &prim_buffer->buffer_status);

	dev_priv->current_prim = prim_buffer;
	dev_priv->current_prim_idx = next_prim_idx;
	return 0;
}

/* More dynamic performance decisions */
static inline int mga_decide_to_fire(drm_device_t *dev)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;

	if(test_bit(MGA_BUF_FORCE_FIRE, &dev_priv->next_prim->buffer_status)) {
		return 1;
	}

	if (test_bit(MGA_IN_GETBUF, &dev_priv->dispatch_status) &&
	    dev_priv->next_prim->num_dwords) {
		return 1;
	}

	if (test_bit(MGA_IN_FLUSH, &dev_priv->dispatch_status) &&
	    dev_priv->next_prim->num_dwords) {
		return 1;
	}

	if(atomic_read(&dev_priv->pending_bufs) <= MGA_NUM_PRIM_BUFS - 1) {
		if(test_bit(MGA_BUF_SWAP_PENDING,
			    &dev_priv->next_prim->buffer_status)) {
			return 1;
		}
	}

	if(atomic_read(&dev_priv->pending_bufs) <= MGA_NUM_PRIM_BUFS / 2) {
		if(dev_priv->next_prim->sec_used >= MGA_DMA_BUF_NR / 8) {
			return 1;
		}
	}

	if(atomic_read(&dev_priv->pending_bufs) >= MGA_NUM_PRIM_BUFS / 2) {
		if(dev_priv->next_prim->sec_used >= MGA_DMA_BUF_NR / 4) {
			return 1;
		}
	}

	return 0;
}

int mga_dma_schedule(drm_device_t *dev, int locked)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	int               retval    = 0;

	if (!dev_priv) return -EBUSY;

	if (test_and_set_bit(0, &dev->dma_flag)) {
		retval = -EBUSY;
		goto sch_out_wakeup;
	}

	if(test_bit(MGA_IN_FLUSH, &dev_priv->dispatch_status) ||
	   test_bit(MGA_IN_WAIT, &dev_priv->dispatch_status) ||
	   test_bit(MGA_IN_GETBUF, &dev_priv->dispatch_status)) {
		locked = 1;
	}

	if (!locked &&
	    !drm_lock_take(&dev->lock.hw_lock->lock, DRM_KERNEL_CONTEXT)) {
		clear_bit(0, &dev->dma_flag);
		retval = -EBUSY;
		goto sch_out_wakeup;
	}

	if(!test_and_set_bit(MGA_IN_DISPATCH, &dev_priv->dispatch_status)) {
		/* Fire dma buffer */
		if(mga_decide_to_fire(dev)) {
			clear_bit(MGA_BUF_FORCE_FIRE,
				  &dev_priv->next_prim->buffer_status);
			if(dev_priv->current_prim == dev_priv->next_prim) {
				/* Schedule overflow for a later time */
				set_bit(MGA_BUF_NEEDS_OVERFLOW,
					&dev_priv->next_prim->buffer_status);
			}
			mga_fire_primary(dev, dev_priv->next_prim);
		} else {
			clear_bit(MGA_IN_DISPATCH, &dev_priv->dispatch_status);
		}
	}

	if (!locked) {
		if (drm_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
		}
	}

	clear_bit(0, &dev->dma_flag);

sch_out_wakeup:
	if(test_bit(MGA_IN_FLUSH, &dev_priv->dispatch_status) &&
	   atomic_read(&dev_priv->pending_bufs) == 0) {
		/* Everything has been processed by the hardware */
		clear_bit(MGA_IN_FLUSH, &dev_priv->dispatch_status);
		wake_up_interruptible(&dev_priv->flush_queue);
	}

	if(test_bit(MGA_IN_GETBUF, &dev_priv->dispatch_status)
	   && dev_priv->tail->age < dev_priv->last_prim_age)
		wake_up_interruptible(&dev_priv->buf_queue);

	return retval;
}

static void mga_dma_service(int irq, void *device, struct pt_regs *regs)
{
	drm_device_t	 *dev = (drm_device_t *)device;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_prim_buf_t *last_prim_buffer;

	atomic_inc(&dev->total_irq);
	if((MGA_READ(MGAREG_STATUS) & 0x00000001) != 0x00000001) return;
	MGA_WRITE(MGAREG_ICLEAR, 0x00000001);
	last_prim_buffer = dev_priv->last_prim;
	last_prim_buffer->num_dwords = 0;
	last_prim_buffer->sec_used = 0;
	dev_priv->sarea_priv->last_dispatch =
		dev_priv->last_prim_age = last_prim_buffer->prim_age;
	clear_bit(MGA_BUF_IN_USE, &last_prim_buffer->buffer_status);
	clear_bit(MGA_BUF_SWAP_PENDING, &last_prim_buffer->buffer_status);
	clear_bit(MGA_IN_DISPATCH, &dev_priv->dispatch_status);
	atomic_dec(&dev_priv->pending_bufs);
	queue_task(&dev->tq, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
	wake_up_interruptible(&dev_priv->wait_queue);
}

static void mga_dma_task_queue(void *device)
{
	mga_dma_schedule((drm_device_t *)device, 0);
}

int mga_dma_cleanup(drm_device_t *dev)
{
	if(dev->dev_private) {
		drm_mga_private_t *dev_priv =
			(drm_mga_private_t *) dev->dev_private;

		if (dev->irq) mga_flush_queue(dev);
		mga_dma_quiescent(dev);

		if(dev_priv->ioremap) {
			int temp = (dev_priv->warp_ucode_size +
				    dev_priv->primary_size +
				    PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;

			drm_ioremapfree((void *) dev_priv->ioremap, temp);
		}
		if(dev_priv->status_page != NULL) {
			iounmap(dev_priv->status_page);
		}
		if(dev_priv->real_status_page != 0UL) {
			mga_free_page(dev, dev_priv->real_status_page);
		}
		if(dev_priv->prim_bufs != NULL) {
			int i;
			for(i = 0; i < MGA_NUM_PRIM_BUFS; i++) {
				if(dev_priv->prim_bufs[i] != NULL) {
					drm_free(dev_priv->prim_bufs[i],
						 sizeof(drm_mga_prim_buf_t),
						 DRM_MEM_DRIVER);
				}
			}
			drm_free(dev_priv->prim_bufs, sizeof(void *) *
				 (MGA_NUM_PRIM_BUFS + 1),
				 DRM_MEM_DRIVER);
		}
		if(dev_priv->head != NULL) {
			mga_freelist_cleanup(dev);
		}


		drm_free(dev->dev_private, sizeof(drm_mga_private_t),
			 DRM_MEM_DRIVER);
		dev->dev_private = NULL;
	}

	return 0;
}

static int mga_dma_initialize(drm_device_t *dev, drm_mga_init_t *init) {
	drm_mga_private_t *dev_priv;
	drm_map_t *sarea_map = NULL;

	dev_priv = drm_alloc(sizeof(drm_mga_private_t), DRM_MEM_DRIVER);
	if(dev_priv == NULL) return -ENOMEM;
	dev->dev_private = (void *) dev_priv;

	memset(dev_priv, 0, sizeof(drm_mga_private_t));

	if((init->reserved_map_idx >= dev->map_count) ||
	   (init->buffer_map_idx >= dev->map_count)) {
		mga_dma_cleanup(dev);
		return -EINVAL;
	}

	dev_priv->reserved_map_idx = init->reserved_map_idx;
	dev_priv->buffer_map_idx = init->buffer_map_idx;
	sarea_map = dev->maplist[0];
	dev_priv->sarea_priv = (drm_mga_sarea_t *)
		((u8 *)sarea_map->handle +
		 init->sarea_priv_offset);

	/* Scale primary size to the next page */
	dev_priv->chipset = init->chipset;
	dev_priv->frontOffset = init->frontOffset;
	dev_priv->backOffset = init->backOffset;
	dev_priv->depthOffset = init->depthOffset;
	dev_priv->textureOffset = init->textureOffset;
	dev_priv->textureSize = init->textureSize;
	dev_priv->cpp = init->cpp;
	dev_priv->sgram = init->sgram;
	dev_priv->stride = init->stride;

	dev_priv->mAccess = init->mAccess;
	init_waitqueue_head(&dev_priv->flush_queue);
	init_waitqueue_head(&dev_priv->buf_queue);
	dev_priv->WarpPipe = 0xff000000;
	dev_priv->vertexsize = 0;

	DRM_DEBUG("chipset=%d ucode_size=%d backOffset=%x depthOffset=%x\n",
		  dev_priv->chipset, dev_priv->warp_ucode_size,
		  dev_priv->backOffset, dev_priv->depthOffset);
	DRM_DEBUG("cpp: %d sgram: %d stride: %d maccess: %x\n",
		  dev_priv->cpp, dev_priv->sgram, dev_priv->stride,
		  dev_priv->mAccess);

	memcpy(&dev_priv->WarpIndex, &init->WarpIndex,
	       sizeof(drm_mga_warp_index_t) * MGA_MAX_WARP_PIPES);

	if(mga_init_primary_bufs(dev, init) != 0) {
		DRM_ERROR("Can not initialize primary buffers\n");
		mga_dma_cleanup(dev);
		return -ENOMEM;
	}
	dev_priv->real_status_page = mga_alloc_page(dev);
	if(dev_priv->real_status_page == 0UL) {
		mga_dma_cleanup(dev);
		DRM_ERROR("Can not allocate status page\n");
		return -ENOMEM;
	}

	dev_priv->status_page =
		ioremap_nocache(virt_to_bus((void *)dev_priv->real_status_page),
				PAGE_SIZE);

	if(dev_priv->status_page == NULL) {
		mga_dma_cleanup(dev);
		DRM_ERROR("Can not remap status page\n");
		return -ENOMEM;
	}

	/* Write status page when secend or softrap occurs */
	MGA_WRITE(MGAREG_PRIMPTR,
		  virt_to_bus((void *)dev_priv->real_status_page) | 0x00000003);


	/* Private is now filled in, initialize the hardware */
	{
		PRIMLOCALS;
		PRIMGETPTR( dev_priv );

		PRIMOUTREG(MGAREG_DMAPAD, 0);
		PRIMOUTREG(MGAREG_DMAPAD, 0);
		PRIMOUTREG(MGAREG_DWGSYNC, 0x0100);
		PRIMOUTREG(MGAREG_SOFTRAP, 0);
		/* Poll for the first buffer to insure that
		 * the status register will be correct
		 */

		mga_flush_write_combine();
		MGA_WRITE(MGAREG_PRIMADDRESS, phys_head | TT_GENERAL);

		MGA_WRITE(MGAREG_PRIMEND, ((phys_head + num_dwords * 4) |
					   PDEA_pagpxfer_enable));

		while(MGA_READ(MGAREG_DWGSYNC) != 0x0100) ;
	}

	if(mga_freelist_init(dev) != 0) {
		DRM_ERROR("Could not initialize freelist\n");
		mga_dma_cleanup(dev);
		return -ENOMEM;
	}
	return 0;
}

int mga_dma_init(struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_init_t init;

	if (copy_from_user(&init, (drm_mga_init_t *)arg, sizeof(init)))
		return -EFAULT;

	switch(init.func) {
	case MGA_INIT_DMA:
		return mga_dma_initialize(dev, &init);
	case MGA_CLEANUP_DMA:
		return mga_dma_cleanup(dev);
	}

	return -EINVAL;
}

static int mga_flush_queue(drm_device_t *dev)
{
	DECLARE_WAITQUEUE(entry, current);
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	int ret = 0;

	if(!dev_priv) return 0;

	if(dev_priv->next_prim->num_dwords != 0) {
		add_wait_queue(&dev_priv->flush_queue, &entry);
		if (test_bit(MGA_IN_FLUSH, &dev_priv->dispatch_status))
			DRM_ERROR("Incorrect mga_flush_queue logic\n");
		set_bit(MGA_IN_FLUSH, &dev_priv->dispatch_status);
		mga_dma_schedule(dev, 0);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			if (!test_bit(MGA_IN_FLUSH,
				      &dev_priv->dispatch_status))
				break;
			atomic_inc(&dev->total_sleeps);
			schedule();
			if (signal_pending(current)) {
				ret = -EINTR; /* Can't restart */
				clear_bit(MGA_IN_FLUSH,
					  &dev_priv->dispatch_status);
				break;
			}
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&dev_priv->flush_queue, &entry);
	}
	return ret;
}

/* Must be called with the lock held */
void mga_reclaim_buffers(drm_device_t *dev, pid_t pid)
{
	drm_device_dma_t *dma = dev->dma;
	int		 i;

	if (!dma) return;
	if(dev->dev_private == NULL) return;
	if(dma->buflist == NULL) return;

	DRM_DEBUG("buf_count=%d\n", dma->buf_count);

        mga_flush_queue(dev);

	for (i = 0; i < dma->buf_count; i++) {
		drm_buf_t *buf = dma->buflist[ i ];
		drm_mga_buf_priv_t *buf_priv = buf->dev_private;

		/* Only buffers that need to get reclaimed ever
		 * get set to free
		 */
		if (buf->pid == pid  && buf_priv) {
			if(buf_priv->my_freelist->age == MGA_BUF_USED)
				buf_priv->my_freelist->age = MGA_BUF_FREE;
		}
	}
}

int mga_lock(struct inode *inode, struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	DECLARE_WAITQUEUE(entry, current);
	int		  ret	= 0;
	drm_lock_t	  lock;

	if (copy_from_user(&lock, (drm_lock_t *)arg, sizeof(lock)))
		return -EFAULT;

	if (lock.context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  current->pid, lock.context);
		return -EINVAL;
	}

	if (lock.context < 0) return -EINVAL;

	/* Only one queue:
	 */

	if (!ret) {
		add_wait_queue(&dev->lock.lock_queue, &entry);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
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
		sigemptyset(&dev->sigmask);
		sigaddset(&dev->sigmask, SIGSTOP);
		sigaddset(&dev->sigmask, SIGTSTP);
		sigaddset(&dev->sigmask, SIGTTIN);
		sigaddset(&dev->sigmask, SIGTTOU);
		dev->sigdata.context = lock.context;
		dev->sigdata.lock    = dev->lock.hw_lock;
		block_all_signals(drm_notifier, &dev->sigdata, &dev->sigmask);

		if (lock.flags & _DRM_LOCK_QUIESCENT) {
		   DRM_DEBUG("_DRM_LOCK_QUIESCENT\n");
		   mga_flush_queue(dev);
		   mga_dma_quiescent(dev);
		}
	}

	if (ret) DRM_DEBUG("%d %s\n", lock.context,
			   ret ? "interrupted" : "has lock");
	return ret;
}

int mga_flush_ioctl(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	drm_lock_t	  lock;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;

	if (copy_from_user(&lock, (drm_lock_t *)arg, sizeof(lock)))
		return -EFAULT;

	if(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("lock not held\n");
		return -EINVAL;
	}

	if(lock.flags & _DRM_LOCK_FLUSH || lock.flags & _DRM_LOCK_FLUSH_ALL) {
		drm_mga_prim_buf_t *temp_buf;

		temp_buf = dev_priv->current_prim;

		if(temp_buf && temp_buf->num_dwords) {
			set_bit(MGA_BUF_FORCE_FIRE, &temp_buf->buffer_status);
			mga_advance_primary(dev);
		}
		mga_dma_schedule(dev, 1);
	}
	if(lock.flags & _DRM_LOCK_QUIESCENT) {
		mga_flush_queue(dev);
		mga_dma_quiescent(dev);
	}

	return 0;
}


#endif




/*
 * ****************************************************************
 *
 *			START NEW CODE HERE
 *
 * ****************************************************************
 */

static unsigned long mga_alloc_page( void )
{
	unsigned long address;

	address = __get_free_page( GFP_KERNEL );
	if ( !address )
		return 0;

	atomic_inc( &virt_to_page(address)->count );
	set_bit( PG_reserved, &virt_to_page(address)->flags );

	return address;
}

static void mga_free_page( unsigned long address )
{
	if ( !address )
		return;

	atomic_dec( &virt_to_page(address)->count );
	clear_bit( PG_reserved, &virt_to_page(address)->flags );
	free_page( address );
}




/* ================================================================
 * Engine control
 */

int mga_do_wait_for_idle( drm_mga_private_t *dev_priv )
{
	u32 status = 0;
	int i;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		status = MGA_READ( MGA_STATUS ) & MGA_ENGINE_IDLE_MASK;
		if ( status == MGA_ENDPRDMASTS ) return 0;
		udelay( 1 );
	}

	DRM_DEBUG( "failed! status=0x%08x\n", status );
	return -EBUSY;
}

int mga_do_dma_idle( drm_mga_private_t *dev_priv )
{
	u32 status = 0;
	int i;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		status = MGA_READ( MGA_STATUS ) & MGA_DMA_IDLE_MASK;
		if ( status == MGA_ENDPRDMASTS ) return 0;
		udelay( 1 );
	}

	DRM_DEBUG( "failed! status=0x%08x\n", status );
	return -EBUSY;
}

int mga_do_dma_reset( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* The primary DMA stream should look like new right about now.
	 */
	primary->tail = 0;
	primary->wrap = 0;
	primary->space = primary->size - MGA_DMA_SOFTRAP_SIZE;
	primary->last_flush = 0;

	/* FIXME: Reset counters, buffer ages etc...
	 */

	/* FIXME: What else do we need to reinitialize?  WARP stuff?
	 */

	return 0;
}

int mga_do_engine_reset( drm_mga_private_t *dev_priv )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* Okay, so we've completely screwed up and locked the engine.
	 * How about we clean up after ourselves?
	 */
	MGA_WRITE( MGA_RST, MGA_SOFTRESET );
	udelay( 15 );				/* Wait at least 10 usecs */
	MGA_WRITE( MGA_RST, 0 );

	/* Initialize the registers that get clobbered by the soft
	 * reset.  Many of the core register values survive a reset,
	 * but the drawing registers are basically all gone.
	 *
	 * 3D clients should probably die after calling this.  The X
	 * server should reset the engine state to known values.
	 */
#if 0
	MGA_WRITE( MGA_PRIMPTR,
		   virt_to_bus((void *)dev_priv->prim.status_page) |
		   MGA_PRIMPTREN0 |
		   MGA_PRIMPTREN1 );
#endif

	MGA_WRITE( MGA_ICLEAR, MGA_SOFTRAPICLR );
	MGA_WRITE( MGA_IEN,    MGA_SOFTRAPIEN );

	/* The primary DMA stream should look like new right about now.
	 */
	mga_do_dma_reset( dev_priv );

	/* This bad boy will never fail.
	 */
	return 0;
}


/* ================================================================
 * Primary DMA stream
 */

void mga_do_dma_flush( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	DMA_LOCALS;
	DRM_DEBUG( "%s:\n", __FUNCTION__ );

	if ( primary->tail == primary->last_flush ) {
		DRM_DEBUG( "   bailing out...\n" );
		return;
	}

	tail = primary->tail + dev_priv->primary->offset;

	BEGIN_DMA( 1 );

	DMA_BLOCK( MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,	0x00000000 );

	ADVANCE_DMA();

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMEND, tail | MGA_PRIMNOSTART | MGA_PAGPXFER );

	head = *primary->head;

	DRM_DEBUG( "   head = 0x%06lx\n", head - dev_priv->primary->offset );
	DRM_DEBUG( "   tail = 0x%06lx\n", tail - dev_priv->primary->offset );

	if ( head <= tail ) {
		primary->space = primary->size - primary->tail;
	} else {
		primary->space = head - tail;
	}
	primary->space -= MGA_DMA_SOFTRAP_SIZE;

	DRM_DEBUG( "  space = 0x%06lx\n", (unsigned long)primary->space );

	primary->last_flush = primary->tail;

	DRM_DEBUG( "%s: done.\n", __FUNCTION__ );
}

void mga_do_dma_wrap( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	DMA_LOCALS;
	DRM_DEBUG( "%s:\n", __FUNCTION__ );

	BEGIN_DMA_WRAP();

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_SOFTRAP,	0x00000000 );

	ADVANCE_DMA();

	tail = primary->tail + dev_priv->primary->offset;

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMEND, tail | MGA_PRIMNOSTART | MGA_PAGPXFER );

	spin_lock_irqsave( &primary->lock, flags );
	primary->tail = 0;
	spin_unlock_irqrestore( &primary->lock, flags );

	head = *primary->head;

	DRM_DEBUG( "   head = 0x%06lx\n", head - dev_priv->primary->offset );
	DRM_DEBUG( "   tail = 0x%06x\n", 0 );

	if ( head <= tail ) {
		primary->space = primary->size - primary->tail;
	} else {
		primary->space = head - tail;
	}
	primary->space -= MGA_DMA_SOFTRAP_SIZE;

	DRM_DEBUG( "  space = 0x%06lx\n", (unsigned long)primary->space );

	primary->last_flush = 0;

	DRM_DEBUG( "%s: done.\n", __FUNCTION__ );
}


/* ================================================================
 * DMA interrupt handling
 */

static void mga_dma_service( int irq, void *device, struct pt_regs *regs )
{
	drm_device_t *dev = (drm_device_t *)device;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head = dev_priv->primary->offset;
	u32 tail;

	atomic_inc( &dev->total_irq );

	/* Verify the interrupt we're servicing is actually the one we
	 * want to service.
	 */
	if ( (MGA_READ( MGA_STATUS ) & MGA_SOFTRAPEN) != MGA_SOFTRAPEN )
		return;

	MGA_WRITE( MGA_ICLEAR, MGA_SOFTRAPICLR );

	spin_lock( &primary->lock );
	tail = primary->tail + dev_priv->primary->offset;
	spin_unlock( &primary->lock );

	DRM_DEBUG( "  *** wrap interrupt:\n" );
	DRM_DEBUG( "      head = 0x%06lx\n", head - dev_priv->primary->offset);
	DRM_DEBUG( "      tail = 0x%06lx\n", tail - dev_priv->primary->offset);

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMADDRESS, head | MGA_DMA_GENERAL );
	MGA_WRITE( MGA_PRIMEND,     tail | MGA_PRIMNOSTART | MGA_PAGPXFER );
}


/* ================================================================
 * Freelist management
 */

#define MGA_BUFFER_USED		~0
#define MGA_BUFFER_FREE		0

static void mga_freelist_print( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;

	DRM_INFO( "\n" );
	DRM_INFO( "current dispatch: last=0x%x done=0x%x\n",
		  dev_priv->sarea_priv->last_dispatch,
		  dev_priv->prim.status[1] );
	DRM_INFO( "current freelist:\n" );

	for ( entry = dev_priv->head->next ; entry ; entry = entry->next ) {
		DRM_INFO( "   %p   idx=%2d  age=0x%x\n",
			  entry, entry->buf->idx, entry->age );
	}
	DRM_INFO( "\n" );
}

static int mga_freelist_init( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_freelist_t *entry;
	int i;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv->head = DRM(alloc)( sizeof(drm_mga_freelist_t),
				     DRM_MEM_DRIVER );
	if ( dev_priv->head == NULL )
		return -ENOMEM;

	memset( dev_priv->head, 0, sizeof(drm_mga_freelist_t) );
	dev_priv->head->age = MGA_BUFFER_USED;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
	        buf_priv = buf->dev_private;

		entry = DRM(alloc)( sizeof(drm_mga_freelist_t),
				    DRM_MEM_DRIVER );
		if ( entry == NULL )
			return -ENOMEM;

		memset( entry, 0, sizeof(drm_mga_freelist_t) );

		entry->next = dev_priv->head->next;
		entry->prev = dev_priv->head;
		entry->age = MGA_BUFFER_FREE;
		entry->buf = buf;

		if ( dev_priv->head->next != NULL )
			dev_priv->head->next->prev = entry;
		if ( entry->next == NULL )
			dev_priv->tail = entry;

		buf_priv->list_entry = entry;
		buf_priv->discard = 0;
		buf_priv->dispatched = 0;

		dev_priv->head->next = entry;
	}

	return 0;
}

static void mga_freelist_cleanup( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;
	drm_mga_freelist_t *next;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	entry = dev_priv->head;
	while ( entry ) {
		next = entry->next;
		DRM(free)( entry, sizeof(drm_mga_freelist_t), DRM_MEM_DRIVER );
		entry = next;
	}

	dev_priv->head = dev_priv->tail = NULL;
}

static void mga_freelist_reset( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	int i;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
	        buf_priv = buf->dev_private;
		buf_priv->list_entry->age = MGA_BUFFER_FREE;
	}
}

static drm_buf_t *mga_freelist_get( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *next;
	drm_mga_freelist_t *prev;

	DRM_DEBUG( "%s: tail=0x%x status=0x%x\n",
		  __FUNCTION__, dev_priv->tail->age,
		  dev_priv->prim.status[1] );
#if 0
	mga_freelist_print( dev );
#endif
	if ( dev_priv->tail->age <= dev_priv->prim.status[1] ) {
		prev = dev_priv->tail->prev;
		next = dev_priv->tail;
		prev->next = NULL;
		next->prev = next->next = NULL;
		dev_priv->tail = prev;
		next->age = MGA_BUFFER_USED;
		return next->buf;
	}

	DRM_ERROR( "returning NULL!\n" );
	return NULL;
}

static int mga_freelist_put( drm_device_t *dev, drm_buf_t *buf )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_freelist_t *head;
	drm_mga_freelist_t *next;
	drm_mga_freelist_t *prev;
	DRM_DEBUG( "%s: age=0x%x\n",
		  __FUNCTION__, buf_priv->list_entry->age );

	if ( buf_priv->list_entry->age == MGA_BUFFER_USED ) {
		/* Discarded buffer, put it on the tail.
		 */
		next = buf_priv->list_entry;
		next->age = MGA_BUFFER_FREE;
		prev = dev_priv->tail;
		prev->next = next;
		next->prev = prev;
		next->next = NULL;
		dev_priv->tail = next;
	} else {
		/* Normally aged buffer, put it on the head + 1,
		 * as the real head is a sentinal element
		 */
		next = buf_priv->list_entry;
		head = dev_priv->head;
		prev = head->next;
		head->next = next;
		prev->prev = next;
		next->prev = head;
		next->next = prev;
	}

	return 0;
}


/* ================================================================
 * DMA initialization, cleanup
 */

static int mga_do_init_dma( drm_device_t *dev, drm_mga_init_t *init )
{
	drm_mga_private_t *dev_priv;
	int ret;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv = DRM(alloc)( sizeof(drm_mga_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL )
		return -ENOMEM;
	dev->dev_private = (void *)dev_priv;

	memset( dev_priv, 0, sizeof(drm_mga_private_t) );


	dev_priv->chipset = init->chipset;

	dev_priv->usec_timeout = MGA_DEFAULT_USEC_TIMEOUT;

	if ( init->sgram ) {
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_BLK;
	} else {
		dev_priv->clear_cmd = MGA_DWGCTL_CLEAR | MGA_ATYPE_RSTR;
	}
	dev_priv->maccess	= init->maccess;

	dev_priv->fb_cpp	= init->fb_cpp;
	dev_priv->front_offset	= init->front_offset;
	dev_priv->front_pitch	= init->front_pitch;
	dev_priv->back_offset	= init->back_offset;
	dev_priv->back_pitch	= init->back_pitch;

	dev_priv->depth_cpp	= init->depth_cpp;
	dev_priv->depth_offset	= init->depth_offset;
	dev_priv->depth_pitch	= init->depth_pitch;

	DRM_INFO( "      macces = 0x%08x\n", init->maccess );
	DRM_INFO( "front offset = 0x%08x\n", init->front_offset );
	DRM_INFO( "front pitch  = 0x%08x\n", init->front_pitch );
	DRM_INFO( " back offset = 0x%08x\n", init->back_offset );
	DRM_INFO( " back pitch  = 0x%08x\n", init->back_pitch );
	DRM_INFO( "depth offset = 0x%08x\n", init->depth_offset );
	DRM_INFO( "depth pitch  = 0x%08x\n", init->depth_pitch );

	dev_priv->sarea = dev->maplist[0];

	DO_FIND_MAP( dev_priv->fb, init->fb_offset );
	DO_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	DO_FIND_MAP( dev_priv->warp, init->warp_offset );
	DO_FIND_MAP( dev_priv->primary, init->primary_offset );
	DO_FIND_MAP( dev_priv->buffers, init->buffers_offset );

	dev_priv->sarea_priv =
		(drm_mga_sarea_t *)((u8 *)dev_priv->sarea->handle +
				    init->sarea_priv_offset);

	DO_IOREMAP( dev_priv->warp );
	DO_IOREMAP( dev_priv->primary );
	DO_IOREMAP( dev_priv->buffers );

	ret = mga_warp_install_microcode( dev );
	if ( ret < 0 ) {
		DRM_ERROR( "failed to install WARP ucode!\n" );
		mga_do_cleanup_dma( dev );
		return ret;
	}

	ret = mga_warp_init( dev );
	if ( ret < 0 ) {
		DRM_ERROR( "failed to init WARP engine!\n" );
		mga_do_cleanup_dma( dev );
		return ret;
	}

	dev_priv->prim.status_page = mga_alloc_page();
	if ( !dev_priv->prim.status_page ) {
		DRM_ERROR( "failed to allocate status page!\n" );
		mga_do_cleanup_dma( dev );
		return -ENOMEM;
	}

	dev_priv->prim.status = ioremap_nocache( virt_to_bus((void *)dev_priv->prim.status_page), PAGE_SIZE );
	if ( dev_priv->prim.status == NULL ) {
		DRM_ERROR( "failed to remap status page!\n" );
		mga_do_cleanup_dma( dev );
		return -ENOMEM;
	}

	mga_do_wait_for_idle( dev_priv );

	/* Init the primary DMA registers.
	 */
	MGA_WRITE( MGA_PRIMADDRESS,
		   dev_priv->primary->offset | MGA_DMA_GENERAL );

	MGA_WRITE( MGA_PRIMPTR,
		   virt_to_bus((void *)dev_priv->prim.status_page) |
		   MGA_PRIMPTREN0 |	/* Soft trap, SECEND, SETUPEND */
		   MGA_PRIMPTREN1 );	/* DWGSYNC */

	dev_priv->prim.start = (u8 *)dev_priv->primary->handle;
	dev_priv->prim.end = ((u8 *)dev_priv->primary->handle
			      + dev_priv->primary->size);
	dev_priv->prim.size = dev_priv->primary->size;

	dev_priv->prim.head = &dev_priv->prim.status[0];
	dev_priv->prim.tail = 0;
	dev_priv->prim.wrap = 0;
	dev_priv->prim.space = dev_priv->prim.size - MGA_DMA_SOFTRAP_SIZE;

	dev_priv->prim.last_flush = 0;
	dev_priv->prim.high_mark = 0;

	spin_lock_init( &dev_priv->prim.lock );

	dev_priv->sarea_priv->last_dispatch = 0;
	dev_priv->prim.status[1] = 0;

	if ( mga_freelist_init( dev ) < 0 ) {
		DRM_ERROR( "could not initialize freelist\n" );
		mga_do_cleanup_dma( dev );
		return -ENOMEM;
	}

	return 0;
}

int mga_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( dev->dev_private ) {
		drm_mga_private_t *dev_priv = dev->dev_private;

		if ( dev_priv->prim.status ) {
			iounmap( (void *)dev_priv->prim.status );
		}
		if ( dev_priv->prim.status_page ) {
			mga_free_page( dev_priv->prim.status_page );
		}

		DO_IOREMAPFREE( dev_priv->warp );
		DO_IOREMAPFREE( dev_priv->primary );
		DO_IOREMAPFREE( dev_priv->buffers );

		/* Make sure we catch this here...
		 */
		if ( dev->irq ) mga_irq_uninstall( dev );

		if ( dev_priv->head != NULL ) {
			mga_freelist_cleanup( dev );
		}

		DRM(free)( dev->dev_private, sizeof(drm_mga_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

int mga_dma_init( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_init_t init;

	if ( copy_from_user( &init, (drm_mga_init_t *)arg, sizeof(init) ) )
		return -EFAULT;

	switch ( init.func ) {
	case MGA_INIT_DMA:
		return mga_do_init_dma( dev, &init );
	case MGA_CLEANUP_DMA:
		return mga_do_cleanup_dma( dev );
	}

	return -EINVAL;
}



/* ================================================================
 * Primary DMA stream management
 */

int mga_dma_flush( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_lock_t lock;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &lock, (drm_lock_t *)arg, sizeof(lock) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: %s%s%s\n",
		   __FUNCTION__,
		   (lock.flags & _DRM_LOCK_FLUSH) ?	"flush, " : "",
		   (lock.flags & _DRM_LOCK_FLUSH_ALL) ?	"flush all, " : "",
		   (lock.flags & _DRM_LOCK_QUIESCENT) ?	"idle, " : "" );

	if ( lock.flags & (_DRM_LOCK_FLUSH | _DRM_LOCK_FLUSH_ALL) ) {
		mga_do_dma_flush( dev_priv );
	}

	if ( lock.flags & _DRM_LOCK_QUIESCENT ) {
		return mga_do_wait_for_idle( dev_priv );
	} else {
		return 0;
	}
}

int mga_dma_reset( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;

	LOCK_TEST_WITH_RETURN( dev );

	return mga_do_dma_reset( dev_priv );
}


/* ================================================================
 * IRQ management
 */

int mga_irq_install( drm_device_t *dev, int irq )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	int ret;

	if ( !irq ) return -EINVAL;

	down( &dev->struct_sem );
	if ( dev->irq ) {
		up( &dev->struct_sem );
		return -EBUSY;
	}
	dev->irq = irq;
	up( &dev->struct_sem );

	DRM_DEBUG( "install irq handler %d\n", irq );

	dev->context_flag = 0;
	dev->interrupt_flag = 0;
	dev->dma_flag = 0;

	dev->dma->next_buffer = NULL;
	dev->dma->next_queue = NULL;
	dev->dma->this_buffer = NULL;

	/* Before installing handler
	 */
	MGA_WRITE( MGA_IEN, 0 );

	/* Install handler
	 */
	ret = request_irq( dev->irq,
			   mga_dma_service,
			   SA_SHIRQ,
			   dev->devname,
			   dev );
	if ( ret ) {
		down( &dev->struct_sem );
		dev->irq = 0;
		up( &dev->struct_sem );
		return ret;
	}

	/* After installing handler
	 */
	MGA_WRITE( MGA_ICLEAR, MGA_SOFTRAPICLR );
	MGA_WRITE( MGA_IEN,    MGA_SOFTRAPIEN );





if ( 0 ) {
	int i;
	int ret;

	DRM_INFO( "start = %p\n", dev_priv->prim.start );
	DRM_INFO( "  end = %p\n", dev_priv->prim.end );
	DRM_INFO( "space = %d \n", dev_priv->prim.space );

#if 0
	/* Let's lock the board and then recover from it.  Isn't it
	 * nice to be able to do so?
	 */
	dev_priv->prim.head += 4096;
	dev_priv->prim.tail = 128;

	mga_do_dma_flush( dev_priv );

	ret = mga_do_wait_for_idle( dev_priv );
	if ( ret < 0 ) {
		mga_do_engine_reset( dev_priv );
	}
#endif

	/* You'll notice that this does not fail.  Cool, eh?
	 */
	mga_do_wait_for_idle( dev_priv );

	for ( i = 0 ; i < 256 ; i++ ) {
		DMA_LOCALS;

		BEGIN_DMA( 8 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000 );

		DMA_BLOCK( MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DMAPAD,	0x00000000,
			   MGA_DWGSYNC,	0x12345678 );

		ADVANCE_DMA();

		FLUSH_DMA();

		udelay( 5 );

		DRM_INFO( "\n" );
		DRM_INFO( "head = 0x%08lx 0x%06lx\n",
			  (unsigned long)dev_priv->prim.status[0],
			  dev_priv->prim.status[0] -
			  dev_priv->primary->offset );
		DRM_INFO( "sync = 0x%08x\n", dev_priv->prim.status[1] );
		DRM_INFO( "\n" );
	}

	udelay( 10 );

	ret = 0;
	if ( mga_do_wait_for_idle( dev_priv ) < 0 )
		ret = -EINVAL;

	DRM_INFO( "head = 0x%08lx 0x%06lx\n",
		  (unsigned long)dev_priv->prim.status[0],
		  dev_priv->prim.status[0] - dev_priv->primary->offset );
	DRM_INFO( "sync = 0x%08x\n", dev_priv->prim.status[1] );
	DRM_INFO( "\n" );
	DRM_INFO( "space = 0x%x / 0x%x\n",
		  dev_priv->prim.space,
		  dev_priv->prim.size - MGA_DMA_SOFTRAP_SIZE );
	DRM_INFO( "\n" );
	DRM_INFO( " irqs = %d\n", atomic_read( &dev->total_irq ) );

	return ret;
}

	return 0;
}

int mga_irq_uninstall( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	int irq;

	down( &dev->struct_sem );
	irq = dev->irq;
	dev->irq = 0;
	up( &dev->struct_sem );

	if ( !irq ) return -EINVAL;

	DRM_DEBUG( "remove irq handler %d\n", irq );

	MGA_WRITE( MGA_ICLEAR, MGA_SOFTRAPICLR );
	MGA_WRITE( MGA_IEN, 0 );

	free_irq( irq, dev );

	return 0;
}

int mga_control( struct inode *inode, struct file *filp,
		 unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_control_t ctl;

	if ( copy_from_user( &ctl, (drm_control_t *)arg, sizeof(ctl) ) )
		return -EFAULT;

	switch ( ctl.func ) {
	case DRM_INST_HANDLER:
		return mga_irq_install( dev, ctl.irq );
	case DRM_UNINST_HANDLER:
		return mga_irq_uninstall( dev );
	default:
		return -EINVAL;
	}
}




/* ================================================================
 * DMA buffer management
 */

static int mga_dma_get_buffers( drm_device_t *dev, drm_dma_t *d )
{
	drm_buf_t *buf;
	int i;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = mga_freelist_get( dev );
		if ( !buf )
			return -EAGAIN;

		buf->pid = current->pid;

		if ( copy_to_user( &d->request_indices[i],
				   &buf->idx, sizeof(buf->idx) ) )
			return -EFAULT;
		if ( copy_to_user( &d->request_sizes[i],
				   &buf->total, sizeof(buf->total) ) )
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int mga_dma_buffers( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_dma_t d;
	int ret = 0;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &d, (drm_dma_t *)arg, sizeof(d) ) )
		return -EFAULT;

	/* Please don't send us buffers.
	 */
	if ( d.send_count != 0 ) {
		DRM_ERROR( "Process %d trying to send %d buffers via drmDMA\n",
			   current->pid, d.send_count );
		return -EINVAL;
	}

	/* We'll send you buffers.
	 */
	if ( d.request_count < 0 || d.request_count > dma->buf_count ) {
		DRM_ERROR( "Process %d trying to get %d buffers (of %d max)\n",
			   current->pid, d.request_count, dma->buf_count );
		return -EINVAL;
	}

	d.granted_count = 0;

	if ( d.request_count ) {
		ret = mga_dma_get_buffers( dev, &d );
	}

	if ( copy_to_user( (drm_dma_t *)arg, &d, sizeof(d) ) )
		return -EFAULT;

	return ret;
}
