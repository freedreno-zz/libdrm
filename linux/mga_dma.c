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
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Keith Whitwell <keithw@valinux.com>
 *
 * Rewritten by:
 *    Gareth Hughes <gareth@valinux.com>
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
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* The primary DMA stream should look like new right about now.
	 */
	primary->tail = 0;
	primary->space = primary->size;
	primary->last_flush = 0;

	sarea_priv->last_wrap = 0;

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
	unsigned long flags;
	DMA_LOCALS;
	DRM_DEBUG( "%s:\n", __FUNCTION__ );

	if ( primary->tail == primary->last_flush + DMA_BLOCK_SIZE ) {
		DRM_DEBUG( "   bailing out...\n" );
		return;
	}

	spin_lock_irqsave( &primary->flush_lock, flags );

	tail = primary->tail + dev_priv->primary->offset;
	primary->last_flush = primary->tail;

	spin_unlock_irqrestore( &primary->flush_lock, flags );

	BEGIN_DMA( 1 );

	DMA_BLOCK( MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,  0x00000000,
		   MGA_DMAPAD,	0x00000000 );

	ADVANCE_DMA();

	head = *primary->head;

	if ( head <= tail ) {
		primary->space = primary->size - primary->tail;
	} else {
		primary->space = head - tail;
	}

	if ( test_bit( 0, &primary->wrap_flag ) ) {
		DRM_DEBUG( "%s: pending wrap...\n", __FUNCTION__ );
		spin_unlock_irqrestore( &primary->flush_lock, flags );
		return;
	}

#if 0
	DRM_INFO( "   head = 0x%06lx\n",
		  head - dev_priv->primary->offset );
	DRM_INFO( "   tail = 0x%06lx\n",
		  tail - dev_priv->primary->offset );
	DRM_INFO( "  space = 0x%06x\n", primary->space );
#endif

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMEND, tail | MGA_PRIMNOSTART | MGA_PAGPXFER );

	DRM_DEBUG( "%s: done.\n", __FUNCTION__ );
}

void mga_do_dma_wrap( drm_mga_private_t *dev_priv )
{
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	u32 head, tail;
	unsigned long flags;
	DMA_LOCALS;
	DRM_DEBUG( "%s:\n", __FUNCTION__ );

	spin_lock_irqsave( &primary->flush_lock, flags );

	BEGIN_DMA_WRAP();

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_SOFTRAP,	0x00000000 );

	ADVANCE_DMA();

	tail = primary->tail + dev_priv->primary->offset;
	primary->tail = 0;

	head = *primary->head;

	if ( head == dev_priv->primary->offset ) {
		primary->space = primary->size;
	} else {
		primary->space = head - dev_priv->primary->offset;
	}

	primary->last_flush = 0;
	primary->last_wrap++;

	set_bit( 0, &primary->wrap_flag );

	if ( 0 ) {
		DRM_INFO( "%s:\n", __FUNCTION__ );
		DRM_INFO( "   head = 0x%06lx\n",
			  head - dev_priv->primary->offset );
		DRM_INFO( "   tail = 0x%06x\n", primary->tail );
		DRM_INFO( "   wrap = %d\n", primary->last_wrap );
		DRM_INFO( "  space = 0x%06x\n", primary->space );
	}

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMEND, tail | MGA_PRIMNOSTART | MGA_PAGPXFER );

	spin_unlock_irqrestore( &primary->flush_lock, flags );

	DRM_DEBUG( "%s: done.\n", __FUNCTION__ );

	mga_do_wait_for_idle( dev_priv );
}


/* ================================================================
 * DMA interrupt handling
 */

static void mga_dma_service( int irq, void *device, struct pt_regs *regs )
{
	drm_device_t *dev = (drm_device_t *)device;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 head = dev_priv->primary->offset;
	u32 flush;

	/* Verify the interrupt we're servicing is actually the one we
	 * want to service.
	 */
	if ( (MGA_READ( MGA_STATUS ) & MGA_SOFTRAPEN) != MGA_SOFTRAPEN )
		return;

	atomic_inc( &dev->counts[_DRM_STAT_IRQ] );

	spin_lock( &primary->flush_lock );

	MGA_WRITE( MGA_ICLEAR, MGA_SOFTRAPICLR );

	flush = primary->last_flush + dev_priv->primary->offset;
	sarea_priv->last_wrap++;

	clear_bit( 0, &primary->wrap_flag );

#if 0
	DRM_INFO( "  *** wrap interrupt:\n" );
	DRM_INFO( "      head = 0x%06lx\n",
		   head - dev_priv->primary->offset );
	DRM_INFO( "     flush = 0x%06lx\n", primary->last_flush );
	DRM_INFO( "      tail = 0x%06lx  %s\n",
		  primary->tail,
		  primary->tail != primary->last_flush + DMA_BLOCK_SIZE
		  ? "***" : "" );
	DRM_INFO( "      wrap = %d\n", sarea_priv->last_wrap );
#endif

	mga_flush_write_combine();
	MGA_WRITE( MGA_PRIMADDRESS, head | MGA_DMA_GENERAL );

	if ( primary->last_flush > 0 ) {
		MGA_WRITE( MGA_PRIMEND, flush | MGA_PAGPXFER );
	}

	spin_unlock( &primary->flush_lock );
}

static int mga_dma_task_queue( void *dev )
{
	drm_mga_private_t *dev_priv = ((drm_device_t *)dev)->dev_private;
	drm_mga_primary_buffer_t *primary = &dev_priv->prim;

	spin_lock( &primary->list_lock );
	mga_do_freelist_wrap( dev );
	spin_unlock( &primary->list_lock );

	return 0;
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
		DRM_INFO( "   %p   idx=%2d  age=0x%x 0x%06lx\n",
			  entry, entry->buf->idx, entry->age.head,
			  entry->age.head - dev_priv->primary->offset );
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
	SET_AGE( &dev_priv->head->age, MGA_BUFFER_USED, 0 );

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
		SET_AGE( &entry->age, MGA_BUFFER_FREE, 0 );
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
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	int i;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
	        buf_priv = buf->dev_private;
		SET_AGE( &buf_priv->list_entry->age,
			 MGA_BUFFER_FREE, 0 );
	}
}

static drm_buf_t *mga_freelist_get( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *next;
	drm_mga_freelist_t *prev;
	drm_mga_freelist_t *tail = dev_priv->tail;
	u32 head, wrap;

	head = *dev_priv->prim.head;
	wrap = dev_priv->sarea_priv->last_wrap;

	DRM_DEBUG( "%s: tail=0x%06lx %d\n",
		  __FUNCTION__,
		  tail->age.head ?
		  tail->age.head - dev_priv->primary->offset : 0,
		  tail->age.wrap );
	DRM_DEBUG( "%s: head=0x%06lx %d\n",
		  __FUNCTION__,
		  head - dev_priv->primary->offset, wrap );

	if ( TEST_AGE( &tail->age, wrap, head ) ) {
		prev = dev_priv->tail->prev;
		next = dev_priv->tail;
		prev->next = NULL;
		next->prev = next->next = NULL;
		dev_priv->tail = prev;
		SET_AGE( &next->age, MGA_BUFFER_USED, 0 );
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
	DRM_DEBUG( "%s: age=0x%06lx wrap=%d\n",
		  __FUNCTION__,
		  buf_priv->list_entry->age.head -
		  dev_priv->primary->offset,
		  buf_priv->list_entry->age.wrap );

	if ( buf_priv->list_entry->age.head == MGA_BUFFER_USED ) {
		/* Discarded buffer, put it on the tail.
		 */
		next = buf_priv->list_entry;
		next->age.head = MGA_BUFFER_FREE;
		next->age.wrap = 0;
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

/* The list spinlock must have been acquired before this can be called...
 */
void mga_do_freelist_wrap( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_freelist_t *entry;
	u32 head = *dev_priv->prim.head;

	DRM_INFO( "%s:\n", __FUNCTION__ );
#if 0
	DRM_DEBUG( "   before...\n" );
	mga_freelist_print( dev );
#endif

	for ( entry = dev_priv->head->next ; entry ; entry = entry->next ) {
		SET_AGE( &entry->age, MGA_BUFFER_FREE, 0 );
	}

#if 0
	DRM_DEBUG( "   after...\n" );
	mga_freelist_print( dev );
#endif
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
	if ( !dev_priv )
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

	dev_priv->sarea = dev->maplist[0];

	DO_FIND_MAP( dev_priv->fb, init->fb_offset );
	DO_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	DO_FIND_MAP( dev_priv->status, init->status_offset );

	DRM_INFO( "status handle = 0x%08lx\n", dev_priv->status->handle );
	DRM_INFO( "status offset = 0x%08lx\n", dev_priv->status->offset );

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

	dev_priv->prim.status = (u32 *)dev_priv->status->handle;

	mga_do_wait_for_idle( dev_priv );

	/* Init the primary DMA registers.
	 */
	MGA_WRITE( MGA_PRIMADDRESS,
		   dev_priv->primary->offset | MGA_DMA_GENERAL );

	MGA_WRITE( MGA_PRIMPTR,
		   virt_to_bus((void *)dev_priv->prim.status) |
		   MGA_PRIMPTREN0 |	/* Soft trap, SECEND, SETUPEND */
		   MGA_PRIMPTREN1 );	/* DWGSYNC */

	dev_priv->prim.start = (u8 *)dev_priv->primary->handle;
	dev_priv->prim.end = ((u8 *)dev_priv->primary->handle
			      + dev_priv->primary->size);
	dev_priv->prim.size = dev_priv->primary->size;

	dev_priv->prim.head = &dev_priv->prim.status[0];
	dev_priv->prim.tail = 0;
	dev_priv->prim.space = dev_priv->prim.size;

	dev_priv->prim.last_flush = 0;
	dev_priv->prim.last_wrap = 0;
	clear_bit( 0, &dev_priv->prim.wrap_flag );

	dev_priv->prim.high_mark = 0;

	spin_lock_init( &dev_priv->prim.flush_lock );
	spin_lock_init( &dev_priv->prim.list_lock );

	dev_priv->prim.status[0] = dev_priv->primary->offset;
	dev_priv->prim.status[1] = 0;

	dev_priv->sarea_priv->last_wrap = 0;
	dev_priv->sarea_priv->last_frame.head = 0;
	dev_priv->sarea_priv->last_frame.wrap = 0;

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

#if 0
		if ( dev_priv->prim.status_page ) {
			mga_free_page( dev_priv->prim.status_page );
		}
#endif

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

#if 0
	INIT_LIST_HEAD(&dev->tq.list);
	dev->tq.sync = 0;
	dev->tq.routine = (void (*)(void *))mga_dma_task_queue;
	dev->tq.data = dev;
#endif

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

		mga_do_dma_flush( dev_priv );

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
		  dev_priv->prim.space, dev_priv->prim.size );
	DRM_INFO( "\n" );
	DRM_INFO( " irqs = %d\n", atomic_read( &dev->counts[_DRM_STAT_IRQ] ) );

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
