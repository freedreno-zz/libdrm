/* radeon_cp.c -- CP support for Radeon -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
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
 *   Kevin E. Martin <martin@valinux.com>
 *   Gareth Hughes <gareth@valinux.com>
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "radeon_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>


/* CP microcode (from ATI) */
static u32 radeon_cp_microcode[][2] = {
	{ 0x21007000, 0000000000 },
	{ 0x20007000, 0000000000 },
	{ 0x000000b4, 0x00000004 },
	{ 0x000000b8, 0x00000004 },
	{ 0x6f5b4d4c, 0000000000 },
	{ 0x4c4c427f, 0000000000 },
	{ 0x5b568a92, 0000000000 },
	{ 0x4ca09c6d, 0000000000 },
	{ 0xad4c4c4c, 0000000000 },
	{ 0x4ce1af3d, 0000000000 },
	{ 0xd8afafaf, 0000000000 },
	{ 0xd64c4cdc, 0000000000 },
	{ 0x4cd10d10, 0000000000 },
	{ 0x000f0000, 0x00000016 },
	{ 0x362f242d, 0000000000 },
	{ 0x00000012, 0x00000004 },
	{ 0x000f0000, 0x00000016 },
	{ 0x362f282d, 0000000000 },
	{ 0x000380e7, 0x00000002 },
	{ 0x04002c97, 0x00000002 },
	{ 0x000f0001, 0x00000016 },
	{ 0x333a3730, 0000000000 },
	{ 0x000077ef, 0x00000002 },
	{ 0x00061000, 0x00000002 },
	{ 0x00000021, 0x0000001a },
	{ 0x00004000, 0x0000001e },
	{ 0x00061000, 0x00000002 },
	{ 0x00000021, 0x0000001a },
	{ 0x00004000, 0x0000001e },
	{ 0x00061000, 0x00000002 },
	{ 0x00000021, 0x0000001a },
	{ 0x00004000, 0x0000001e },
	{ 0x00000017, 0x00000004 },
	{ 0x0003802b, 0x00000002 },
	{ 0x040067e0, 0x00000002 },
	{ 0x00000017, 0x00000004 },
	{ 0x000077e0, 0x00000002 },
	{ 0x00065000, 0x00000002 },
	{ 0x000037e1, 0x00000002 },
	{ 0x040067e1, 0x00000006 },
	{ 0x000077e0, 0x00000002 },
	{ 0x000077e1, 0x00000002 },
	{ 0x000077e1, 0x00000006 },
	{ 0xffffffff, 0000000000 },
	{ 0x10000000, 0000000000 },
	{ 0x0003802b, 0x00000002 },
	{ 0x040067e0, 0x00000006 },
	{ 0x00007675, 0x00000002 },
	{ 0x00007676, 0x00000002 },
	{ 0x00007677, 0x00000002 },
	{ 0x00007678, 0x00000006 },
	{ 0x0003802c, 0x00000002 },
	{ 0x04002676, 0x00000002 },
	{ 0x00007677, 0x00000002 },
	{ 0x00007678, 0x00000006 },
	{ 0x0000002f, 0x00000018 },
	{ 0x0000002f, 0x00000018 },
	{ 0000000000, 0x00000006 },
	{ 0x00000030, 0x00000018 },
	{ 0x00000030, 0x00000018 },
	{ 0000000000, 0x00000006 },
	{ 0x01605000, 0x00000002 },
	{ 0x00065000, 0x00000002 },
	{ 0x00098000, 0x00000002 },
	{ 0x00061000, 0x00000002 },
	{ 0x64c0603e, 0x00000004 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00080000, 0x00000016 },
	{ 0000000000, 0000000000 },
	{ 0x0400251d, 0x00000002 },
	{ 0x00007580, 0x00000002 },
	{ 0x00067581, 0x00000002 },
	{ 0x04002580, 0x00000002 },
	{ 0x00067581, 0x00000002 },
	{ 0x00000049, 0x00000004 },
	{ 0x00005000, 0000000000 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00061000, 0x00000002 },
	{ 0x0000750e, 0x00000002 },
	{ 0x00019000, 0x00000002 },
	{ 0x00011055, 0x00000014 },
	{ 0x00000055, 0x00000012 },
	{ 0x0400250f, 0x00000002 },
	{ 0x0000504f, 0x00000004 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00007565, 0x00000002 },
	{ 0x00007566, 0x00000002 },
	{ 0x00000058, 0x00000004 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x01e655b4, 0x00000002 },
	{ 0x4401b0e4, 0x00000002 },
	{ 0x01c110e4, 0x00000002 },
	{ 0x26667066, 0x00000018 },
	{ 0x040c2565, 0x00000002 },
	{ 0x00000066, 0x00000018 },
	{ 0x04002564, 0x00000002 },
	{ 0x00007566, 0x00000002 },
	{ 0x0000005d, 0x00000004 },
	{ 0x00401069, 0x00000008 },
	{ 0x00101000, 0x00000002 },
	{ 0x000d80ff, 0x00000002 },
	{ 0x0080006c, 0x00000008 },
	{ 0x000f9000, 0x00000002 },
	{ 0x000e00ff, 0x00000002 },
	{ 0000000000, 0x00000006 },
	{ 0x0000008f, 0x00000018 },
	{ 0x0000005b, 0x00000004 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00007576, 0x00000002 },
	{ 0x00065000, 0x00000002 },
	{ 0x00009000, 0x00000002 },
	{ 0x00041000, 0x00000002 },
	{ 0x0c00350e, 0x00000002 },
	{ 0x00049000, 0x00000002 },
	{ 0x00051000, 0x00000002 },
	{ 0x01e785f8, 0x00000002 },
	{ 0x00200000, 0x00000002 },
	{ 0x0060007e, 0x0000000c },
	{ 0x00007563, 0x00000002 },
	{ 0x006075f0, 0x00000021 },
	{ 0x20007073, 0x00000004 },
	{ 0x00005073, 0x00000004 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00007576, 0x00000002 },
	{ 0x00007577, 0x00000002 },
	{ 0x0000750e, 0x00000002 },
	{ 0x0000750f, 0x00000002 },
	{ 0x00a05000, 0x00000002 },
	{ 0x00600083, 0x0000000c },
	{ 0x006075f0, 0x00000021 },
	{ 0x000075f8, 0x00000002 },
	{ 0x00000083, 0x00000004 },
	{ 0x000a750e, 0x00000002 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x0020750f, 0x00000002 },
	{ 0x00600086, 0x00000004 },
	{ 0x00007570, 0x00000002 },
	{ 0x00007571, 0x00000002 },
	{ 0x00007572, 0x00000006 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00005000, 0x00000002 },
	{ 0x00a05000, 0x00000002 },
	{ 0x00007568, 0x00000002 },
	{ 0x00061000, 0x00000002 },
	{ 0x00000095, 0x0000000c },
	{ 0x00058000, 0x00000002 },
	{ 0x0c607562, 0x00000002 },
	{ 0x00000097, 0x00000004 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x00600096, 0x00000004 },
	{ 0x400070e5, 0000000000 },
	{ 0x000380e6, 0x00000002 },
	{ 0x040025c5, 0x00000002 },
	{ 0x000380e5, 0x00000002 },
	{ 0x000000a8, 0x0000001c },
	{ 0x000650aa, 0x00000018 },
	{ 0x040025bb, 0x00000002 },
	{ 0x000610ab, 0x00000018 },
	{ 0x040075bc, 0000000000 },
	{ 0x000075bb, 0x00000002 },
	{ 0x000075bc, 0000000000 },
	{ 0x00090000, 0x00000006 },
	{ 0x00090000, 0x00000002 },
	{ 0x000d8002, 0x00000006 },
	{ 0x00007832, 0x00000002 },
	{ 0x00005000, 0x00000002 },
	{ 0x000380e7, 0x00000002 },
	{ 0x04002c97, 0x00000002 },
	{ 0x00007820, 0x00000002 },
	{ 0x00007821, 0x00000002 },
	{ 0x00007800, 0000000000 },
	{ 0x01200000, 0x00000002 },
	{ 0x20077000, 0x00000002 },
	{ 0x01200000, 0x00000002 },
	{ 0x20007000, 0x00000002 },
	{ 0x00061000, 0x00000002 },
	{ 0x0120751b, 0x00000002 },
	{ 0x8040750a, 0x00000002 },
	{ 0x8040750b, 0x00000002 },
	{ 0x00110000, 0x00000002 },
	{ 0x000380e5, 0x00000002 },
	{ 0x000000c6, 0x0000001c },
	{ 0x000610ab, 0x00000018 },
	{ 0x844075bd, 0x00000002 },
	{ 0x000610aa, 0x00000018 },
	{ 0x840075bb, 0x00000002 },
	{ 0x000610ab, 0x00000018 },
	{ 0x844075bc, 0x00000002 },
	{ 0x000000c9, 0x00000004 },
	{ 0x804075bd, 0x00000002 },
	{ 0x800075bb, 0x00000002 },
	{ 0x804075bc, 0x00000002 },
	{ 0x00108000, 0x00000002 },
	{ 0x01400000, 0x00000002 },
	{ 0x006000cd, 0x0000000c },
	{ 0x20c07000, 0x00000020 },
	{ 0x000000cf, 0x00000012 },
	{ 0x00800000, 0x00000006 },
	{ 0x0080751d, 0x00000006 },
	{ 0000000000, 0000000000 },
	{ 0x0000775c, 0x00000002 },
	{ 0x00a05000, 0x00000002 },
	{ 0x00661000, 0x00000002 },
	{ 0x0460275d, 0x00000020 },
	{ 0x00004000, 0000000000 },
	{ 0x01e00830, 0x00000002 },
	{ 0x21007000, 0000000000 },
	{ 0x6464614d, 0000000000 },
	{ 0x69687420, 0000000000 },
	{ 0x00000073, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0x00005000, 0x00000002 },
	{ 0x000380d0, 0x00000002 },
	{ 0x040025e0, 0x00000002 },
	{ 0x000075e1, 0000000000 },
	{ 0x00000001, 0000000000 },
	{ 0x000380e0, 0x00000002 },
	{ 0x04002394, 0x00000002 },
	{ 0x00005000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0x00000008, 0000000000 },
	{ 0x00000004, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
	{ 0000000000, 0000000000 },
};


#define DO_REMAP(_m) (_m)->handle = drm_ioremap((_m)->offset, (_m)->size)

#define DO_REMAPFREE(_m)                                                    \
	do {                                                                \
		if ((_m)->handle && (_m)->size)                             \
			drm_ioremapfree((_m)->handle, (_m)->size);          \
	} while (0)

#define DO_FIND_MAP(_m, _o)                                                 \
	do {                                                                \
		int _i;                                                     \
		for (_i = 0; _i < dev->map_count; _i++) {                   \
			if (dev->maplist[_i]->offset == _o) {               \
				_m = dev->maplist[_i];                      \
				break;                                      \
			}                                                   \
		}                                                           \
	} while (0)


int RADEON_READ_PLL(drm_device_t *dev, int addr)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX, addr & 0x1f);
	return RADEON_READ(RADEON_CLOCK_CNTL_DATA);
}


#if 0
static void radeon_status( drm_radeon_private_t *dev_priv )
{
	printk( "GUI_STAT           = 0x%08x\n",
		(unsigned int)RADEON_READ( RADEON_GUI_STAT ) );
	printk( "PM4_STAT           = 0x%08x\n",
		(unsigned int)RADEON_READ( RADEON_PM4_STAT ) );
	printk( "PM4_BUFFER_DL_WPTR = 0x%08x\n",
		(unsigned int)RADEON_READ( RADEON_PM4_BUFFER_DL_WPTR ) );
	printk( "PM4_BUFFER_DL_RPTR = 0x%08x\n",
		(unsigned int)RADEON_READ( RADEON_PM4_BUFFER_DL_RPTR ) );
	printk( "PM4_MICRO_CNTL     = 0x%08x\n",
		(unsigned int)RADEON_READ( RADEON_PM4_MICRO_CNTL ) );
	printk( "PM4_BUFFER_CNTL    = 0x%08x\n",
		(unsigned int)RADEON_READ( RADEON_PM4_BUFFER_CNTL ) );
}
#endif


/* ================================================================
 * Engine, FIFO control
 */

static int radeon_do_pixcache_flush( drm_radeon_private_t *dev_priv )
{
	u32 tmp;
	int i;

	tmp  = RADEON_READ( RADEON_RB2D_DSTCACHE_CTLSTAT );
	tmp |= RADEON_RB2D_DC_FLUSH_ALL;
	RADEON_WRITE( RADEON_RB2D_DSTCACHE_CTLSTAT, tmp );

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(RADEON_READ( RADEON_RB2D_DSTCACHE_CTLSTAT )
		       & RADEON_RB2D_DC_BUSY) ) {
			return 0;
		}
		udelay( 1 );
	}

	DRM_ERROR( "%s failed!\n", __FUNCTION__ );
	return -EBUSY;
}

static int radeon_do_wait_for_fifo( drm_radeon_private_t *dev_priv, int entries )
{
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		int slots = ( RADEON_READ( RADEON_RBBM_STATUS )
			      & RADEON_RBBM_FIFOCNT_MASK );
		if ( slots >= entries ) return 0;
		udelay( 1 );
	}

	DRM_ERROR( "%s failed!\n", __FUNCTION__ );
	return -EBUSY;
}

static int radeon_do_wait_for_idle( drm_radeon_private_t *dev_priv )
{
	int i, ret;

	ret = radeon_do_wait_for_fifo( dev_priv, 64 );
	if ( !ret ) return ret;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(RADEON_READ( RADEON_RBBM_STATUS )
		       & RADEON_RBBM_ACTIVE) ) {
			radeon_do_pixcache_flush( dev_priv );
			return 0;
		}
		udelay( 1 );
	}

	DRM_ERROR( "%s failed!\n", __FUNCTION__ );
	return -EBUSY;
}


/* ================================================================
 * CP control, initialization
 */

/* Load the microcode for the CP */
static void radeon_cp_load_microcode( drm_radeon_private_t *dev_priv )
{
	int i;

	radeon_do_wait_for_idle( dev_priv );

	RADEON_WRITE( RADEON_CP_ME_RAM_ADDR, 0 );
	for ( i = 0 ; i < 256 ; i++ ) {
		RADEON_WRITE( RADEON_CP_ME_RAM_DATAH,
			      radeon_cp_microcode[i][1] );
		RADEON_WRITE( RADEON_CP_ME_RAM_DATAL,
			      radeon_cp_microcode[i][0] );
	}
}

/* Flush any pending commands to the CP.  This should only be used just
 * prior to a wait for idle, as it informs the engine that the command
 * stream is ending.
 */
static void radeon_do_cp_flush( drm_radeon_private_t *dev_priv )
{
#if 0
	u32 tmp;

	tmp = RADEON_READ( RADEON_PM4_BUFFER_DL_WPTR ) | RADEON_PM4_BUFFER_DL_DONE;
	RADEON_WRITE( RADEON_PM4_BUFFER_DL_WPTR, tmp );
#endif
}

/* Wait for the CP to go idle.
 */
static int radeon_do_cp_idle( drm_radeon_private_t *dev_priv )
{
	return radeon_do_wait_for_idle( dev_priv );
}

/* Start the Concurrent Command Engine.
 */
static void radeon_do_cp_start( drm_radeon_private_t *dev_priv )
{
	radeon_do_wait_for_idle( dev_priv );

	RADEON_WRITE( RADEON_CP_CSQ_CNTL, dev_priv->cp_mode );

	dev_priv->cp_running = 1;
}

/* Reset the Concurrent Command Engine.  This will not flush any pending
 * commangs, so you must wait for the CP command stream to complete
 * before calling this routine.
 */
static void radeon_do_cp_reset( drm_radeon_private_t *dev_priv )
{
	u32 cur_read_ptr;

	cur_read_ptr = RADEON_READ( RADEON_CP_RB_RPTR );
	RADEON_WRITE( RADEON_CP_RB_WPTR, cur_read_ptr );
	*dev_priv->ring.head = cur_read_ptr;
	dev_priv->ring.tail = cur_read_ptr;
}

/* Stop the Concurrent Command Engine.  This will not flush any pending
 * commangs, so you must flush the command stream and wait for the CP
 * to go idle before calling this routine.
 */
static void radeon_do_cp_stop( drm_radeon_private_t *dev_priv )
{
	RADEON_WRITE( RADEON_CP_CSQ_CNTL, RADEON_CSQ_PRIDIS_INDDIS );

	dev_priv->cp_running = 0;
}

/* Reset the engine.  This will stop the CP if it is running.
 */
static int radeon_do_engine_reset( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 clock_cntl_index, mclk_cntl, rbbm_soft_reset;

	radeon_do_pixcache_flush( dev_priv );

	clock_cntl_index = RADEON_READ( RADEON_CLOCK_CNTL_INDEX );
	mclk_cntl = RADEON_READ_PLL( dev, RADEON_MCLK_CNTL );

	/* FIXME: remove magic number here and in radeon ddx driver!!! */
	RADEON_WRITE_PLL( RADEON_MCLK_CNTL, mclk_cntl | 0x003f00000 );

	rbbm_soft_reset = RADEON_READ( RADEON_RBBM_SOFT_RESET );

	RADEON_WRITE( RADEON_RBBM_SOFT_RESET, ( rbbm_soft_reset |
						RADEON_SOFT_RESET_CP |
						RADEON_SOFT_RESET_HI |
						RADEON_SOFT_RESET_SE |
						RADEON_SOFT_RESET_RE |
						RADEON_SOFT_RESET_PP |
						RADEON_SOFT_RESET_E2 |
						RADEON_SOFT_RESET_RB |
						RADEON_SOFT_RESET_HDP ) );
	RADEON_READ( RADEON_RBBM_SOFT_RESET );
	RADEON_WRITE( RADEON_RBBM_SOFT_RESET, ( rbbm_soft_reset &
						~( RADEON_SOFT_RESET_CP |
						   RADEON_SOFT_RESET_HI |
						   RADEON_SOFT_RESET_SE |
						   RADEON_SOFT_RESET_RE |
						   RADEON_SOFT_RESET_PP |
						   RADEON_SOFT_RESET_E2 |
						   RADEON_SOFT_RESET_RB |
						   RADEON_SOFT_RESET_HDP ) ) );
	RADEON_READ( RADEON_RBBM_SOFT_RESET );


	RADEON_WRITE_PLL( RADEON_MCLK_CNTL, mclk_cntl );
	RADEON_WRITE( RADEON_CLOCK_CNTL_INDEX, clock_cntl_index );
	RADEON_WRITE( RADEON_RBBM_SOFT_RESET,  rbbm_soft_reset );

	/* Reset the CP ring */
	radeon_do_cp_reset( dev_priv );

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	/* Reset any pending vertex, indirect buffers */
	radeon_freelist_reset( dev );

	return 0;
}

static void radeon_cp_init_ring_buffer( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32 ring_start, cur_read_ptr;
	u32 tmp;

	/* Initialize the memory controller */
	RADEON_WRITE( RADEON_MC_FB_LOCATION,
		      ( dev_priv->agp_vm_start-1 ) & 0xffff0000);
	RADEON_WRITE( RADEON_MC_AGP_LOCATION,
		      ( ( ( dev_priv->agp_vm_start-1 +
			    dev_priv->agp_size ) & 0xffff0000)
			| ( dev_priv->agp_vm_start >> 16 ) ) );

	ring_start = dev_priv->cp_ring->offset - dev->agp->base;

	RADEON_WRITE( RADEON_CP_RB_BASE, ring_start + dev_priv->agp_vm_start );

	/* Set the write pointer delay */
	RADEON_WRITE( RADEON_CP_RB_WPTR_DELAY, 0 );

	/* Initialize the ring buffer's read and write pointers */
	cur_read_ptr = RADEON_READ( RADEON_CP_RB_RPTR );
	RADEON_WRITE( RADEON_CP_RB_WPTR, cur_read_ptr );
	*dev_priv->ring.head = cur_read_ptr;
	dev_priv->ring.tail = cur_read_ptr;

	RADEON_WRITE( RADEON_CP_RB_RPTR_ADDR, dev_priv->ring_rptr->offset );

	/* Set ring buffer size */
	RADEON_WRITE( RADEON_CP_RB_CNTL, dev_priv->ring.size_l2qw );

	radeon_do_wait_for_idle( dev_priv );

	/* Turn off PCI GART */
	tmp = RADEON_READ( RADEON_AIC_CNTL ) & ~RADEON_PCIGART_TRANSLATE_EN;
	RADEON_WRITE( RADEON_AIC_CNTL, tmp );

	/* Turn on bus mastering */
	tmp = RADEON_READ( RADEON_BUS_CNTL ) & ~RADEON_BUS_MASTER_DIS;
	RADEON_WRITE( RADEON_BUS_CNTL, tmp );
}

static int radeon_do_init_cp( drm_device_t *dev, drm_radeon_init_t *init )
{
	drm_radeon_private_t *dev_priv;
        int i;

	dev_priv = drm_alloc( sizeof(drm_radeon_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL )
		return -ENOMEM;
	dev->dev_private = (void *)dev_priv;

	memset( dev_priv, 0, sizeof(drm_radeon_private_t) );

	dev_priv->is_pci = init->is_pci;

	/* We don't support PCI cards until PCI GART is implemented.
	 * Fail here so we can remove all checks for PCI cards around
	 * the CP ring code.
	 */
	if ( dev_priv->is_pci ) {
		drm_free( dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER );
		dev->dev_private = NULL;
		return -EINVAL;
	}

	dev_priv->usec_timeout = init->usec_timeout;
	if ( dev_priv->usec_timeout < 1 ||
	     dev_priv->usec_timeout > RADEON_MAX_USEC_TIMEOUT ) {
		drm_free( dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER );
		dev->dev_private = NULL;
		return -EINVAL;
	}

	dev_priv->cp_mode = init->cp_mode;
	dev_priv->cp_secure = init->cp_secure;

	/* Simple idle check.
	 */
	atomic_set( &dev_priv->idle_count, 0 );

	/* We don't support anything other than bus-mastering ring mode,
	 * but the ring can be in either AGP or PCI space for the ring
	 * read pointer.
	 */
	if ( ( init->cp_mode != RADEON_CSQ_PRIBM_INDDIS ) &&
	     ( init->cp_mode != RADEON_CSQ_PRIBM_INDBM ) ) {
		drm_free( dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER );
		dev->dev_private = NULL;
		return -EINVAL;
	}

	dev_priv->fb_bpp	= init->fb_bpp;
	dev_priv->front_offset	= init->front_offset;
	dev_priv->front_pitch	= init->front_pitch;
	dev_priv->front_x	= init->front_x;
	dev_priv->front_y	= init->front_y;
	dev_priv->back_offset	= init->back_offset;
	dev_priv->back_pitch	= init->back_pitch;
	dev_priv->back_x	= init->back_x;
	dev_priv->back_y	= init->back_y;

	dev_priv->depth_bpp	= init->depth_bpp;
	dev_priv->depth_offset	= init->depth_offset;
	dev_priv->depth_pitch	= init->depth_pitch;
	dev_priv->depth_x	= init->depth_x;
	dev_priv->depth_y	= init->depth_y;

	/* FIXME: We want multiple shared areas, including one shared
	 * only by the X Server and kernel module.
	 */
	for ( i = 0 ; i < dev->map_count ; i++ ) {
		if ( dev->maplist[i]->type == _DRM_SHM ) {
			dev_priv->sarea = dev->maplist[i];
			break;
		}
	}

	DO_FIND_MAP( dev_priv->fb, init->fb_offset );
	DO_FIND_MAP( dev_priv->mmio, init->mmio_offset );
	DO_FIND_MAP( dev_priv->cp_ring, init->ring_offset );
	DO_FIND_MAP( dev_priv->ring_rptr, init->ring_rptr_offset );
	DO_FIND_MAP( dev_priv->buffers, init->buffers_offset );

	if ( !dev_priv->is_pci ) {
		DO_FIND_MAP( dev_priv->agp_textures,
			     init->agp_textures_offset );
	}

	dev_priv->sarea_priv =
		(drm_radeon_sarea_t *)((u8 *)dev_priv->sarea->handle +
				       init->sarea_priv_offset);

	DO_REMAP( dev_priv->cp_ring );
	DO_REMAP( dev_priv->ring_rptr );
	DO_REMAP( dev_priv->buffers );
#if 0
	if ( !dev_priv->is_pci ) {
		DO_REMAP( dev_priv->agp_textures );
	}
#endif

	dev_priv->agp_vm_start = init->agp_vm_start;
	dev_priv->agp_size = init->agp_size;

	dev_priv->ring.head = ((__volatile__ u32 *)
			       dev_priv->ring_rptr->handle);

	dev_priv->ring.start = (u32 *)dev_priv->cp_ring->handle;
	dev_priv->ring.end = ((u32 *)dev_priv->cp_ring->handle
			      + init->ring_size / sizeof(u32));
	dev_priv->ring.size = init->ring_size;
	dev_priv->ring.size_l2qw = drm_order( init->ring_size / 8 );

	dev_priv->ring.tail_mask =
		(dev_priv->ring.size / sizeof(u32)) - 1;

	dev_priv->sarea_priv->last_frame = 0;
	RADEON_WRITE( RADEON_LAST_FRAME_REG,
		      dev_priv->sarea_priv->last_frame );

	dev_priv->sarea_priv->last_dispatch = 0;
	RADEON_WRITE( RADEON_LAST_DISPATCH_REG,
		      dev_priv->sarea_priv->last_dispatch );

	radeon_cp_load_microcode( dev_priv );
	radeon_cp_init_ring_buffer( dev );
	radeon_do_engine_reset( dev );

	return 0;
}

static int radeon_do_cleanup_cp( drm_device_t *dev )
{
	if ( dev->dev_private ) {
		drm_radeon_private_t *dev_priv = dev->dev_private;

		DO_REMAPFREE( dev_priv->cp_ring );
		DO_REMAPFREE( dev_priv->ring_rptr );
		DO_REMAPFREE( dev_priv->buffers );
#if 0
		if ( !dev_priv->is_pci ) {
			DO_REMAPFREE( dev_priv->agp_textures );
		}
#endif

		drm_free( dev->dev_private, sizeof(drm_radeon_private_t),
			  DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

int radeon_cp_init( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_radeon_init_t init;

	if ( copy_from_user( &init, (drm_radeon_init_t *)arg, sizeof(init) ) )
		return -EFAULT;

	switch ( init.func ) {
	case RADEON_INIT_CP:
		return radeon_do_init_cp( dev, &init );
	case RADEON_CLEANUP_CP:
		return radeon_do_cleanup_cp( dev );
	}

	return -EINVAL;
}

int radeon_cp_start( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}
	if ( dev_priv->cp_running ) {
		DRM_DEBUG( "%s while CP running\n", __FUNCTION__ );
		return 0;
	}
	if ( dev_priv->cp_mode == RADEON_CSQ_PRIDIS_INDDIS ) {
		DRM_DEBUG( "%s called with bogus CP mode (%d)\n",
			   __FUNCTION__, dev_priv->cp_mode );
		return 0;
	}

	radeon_do_cp_start( dev_priv );

	return 0;
}

/* Stop the CP.  The engine must have been idled before calling this
 * routine.
 */
int radeon_cp_stop( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_cp_stop_t stop;
	int ret;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &stop, (drm_radeon_init_t *)arg, sizeof(stop) ) )
		return -EFAULT;

	/* Flush any pending CP commands.  This ensures any outstanding
	 * commands are exectuted by the engine before we turn it off.
	 */
	if ( stop.flush ) {
		radeon_do_cp_flush( dev_priv );
	}

	/* If we fail to make the engine go idle, we return an error
	 * code so that the DRM ioctl wrapper can try again.
	 */
	if ( stop.idle ) {
		ret = radeon_do_cp_idle( dev_priv );
		if ( ret < 0 ) return ret;
	}

	/* Finally, we can turn off the CP.  If the engine isn't idle,
	 * we will get some dropped triangles as they won't be fully
	 * rendered before the CP is shut down.
	 */
	radeon_do_cp_stop( dev_priv );

	/* Reset the engine */
	radeon_do_engine_reset( dev );

	return 0;
}

/* Just reset the CP ring.  Called as part of an X Server engine reset.
 */
int radeon_cp_reset( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}
	if ( !dev_priv ) {
		DRM_DEBUG( "%s called before init done\n", __FUNCTION__ );
		return -EINVAL;
	}

	radeon_do_cp_reset( dev_priv );

	/* The CP is no longer running after an engine reset */
	dev_priv->cp_running = 0;

	return 0;
}

int radeon_cp_idle( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}

	return radeon_do_cp_idle( dev_priv );
}

int radeon_engine_reset( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}

	return radeon_do_engine_reset( dev );
}


/* ================================================================
 * Freelist management
 */
#define RADEON_BUFFER_USED	0xffffffff
#define RADEON_BUFFER_FREE	0

static int radeon_freelist_init( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_buf_t *buf;
	drm_radeon_buf_priv_t *buf_priv;
	drm_radeon_freelist_t *entry;
	int i;

	dev_priv->head = drm_alloc( sizeof(drm_radeon_freelist_t),
				    DRM_MEM_DRIVER );
	if ( dev_priv->head == NULL )
		return -ENOMEM;

	memset( dev_priv->head, 0, sizeof(drm_radeon_freelist_t) );
	dev_priv->head->age = RADEON_BUFFER_USED;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;

		entry = drm_alloc( sizeof(drm_radeon_freelist_t),
				   DRM_MEM_DRIVER );
		if ( !entry ) return -ENOMEM;

		entry->age = RADEON_BUFFER_FREE;
		entry->buf = buf;
		entry->prev = dev_priv->head;
		entry->next = dev_priv->head->next;
		if ( !entry->next )
			dev_priv->tail = entry;

		buf_priv->discard = 0;
		buf_priv->dispatched = 0;
		buf_priv->list_entry = entry;

		dev_priv->head->next = entry;

		if ( dev_priv->head->next )
			dev_priv->head->next->prev = entry;
	}

	return 0;

}

drm_buf_t *radeon_freelist_get( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	drm_buf_t *buf;
	int i, t;

	/* FIXME: Optimize -- use freelist code */

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;
		if ( buf->pid == 0 )
			return buf;
	}

	for ( t = 0 ; t < dev_priv->usec_timeout ; t++ ) {
		u32 done_age = RADEON_READ( RADEON_LAST_DISPATCH_REG );

		for ( i = 0 ; i < dma->buf_count ; i++ ) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if ( buf->pending && buf_priv->age <= done_age ) {
				/* The buffer has been processed, so it
				 * can now be used.
				 */
				buf->pending = 0;
				return buf;
			}
		}
		udelay( 1 );
	}

	DRM_ERROR( "returning NULL!\n" );
	return NULL;
}

void radeon_freelist_reset( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	int i;

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		drm_buf_t *buf = dma->buflist[i];
		drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
}


/* ================================================================
 * CP packet submission
 */

int radeon_wait_ring( drm_radeon_private_t *dev_priv, int n )
{
	drm_radeon_ring_buffer_t *ring = &dev_priv->ring;
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		ring->space = *ring->head - ring->tail;
		if ( ring->space <= 0 )
			ring->space += ring->size;

		if ( ring->space >= n )
			return 0;

		udelay( 1 );
	}

	/* FIXME: This return value is ignored in the BEGIN_RING macro! */
	return -EBUSY;
}

void radeon_update_ring_snapshot( drm_radeon_private_t *dev_priv )
{
	drm_radeon_ring_buffer_t *ring = &dev_priv->ring;

	ring->space = *ring->head - ring->tail;
	if ( ring->space == 0 )
		atomic_inc( &dev_priv->idle_count );
	if ( ring->space <= 0 )
		ring->space += ring->size;
}

#if 0
static int radeon_verify_command( drm_radeon_private_t *dev_priv,
				  u32 cmd, int *size )
{
	int writing = 1;

	*size = 0;

	switch ( cmd & RADEON_CP_PACKET_MASK ) {
	case RADEON_CP_PACKET0:
		if ( (cmd & RADEON_CP_PACKET0_REG_MASK) <= (0x1004 >> 2) &&
		     (cmd & RADEON_CP_PACKET0_REG_MASK) !=
		     (RADEON_PM4_VC_FPU_SETUP >> 2) ) {
			writing = 0;
		}
		*size = ((cmd & RADEON_CP_PACKET_COUNT_MASK) >> 16) + 2;
		break;

	case RADEON_CP_PACKET1:
		if ( (cmd & RADEON_CP_PACKET1_REG0_MASK) <= (0x1004 >> 2) &&
		     (cmd & RADEON_CP_PACKET1_REG0_MASK) !=
		     (RADEON_PM4_VC_FPU_SETUP >> 2) ) {
			writing = 0;
		}
		if ( (cmd & RADEON_CP_PACKET1_REG1_MASK) <= (0x1004 << 9) &&
		     (cmd & RADEON_CP_PACKET1_REG1_MASK) !=
		     (RADEON_PM4_VC_FPU_SETUP << 9) ) {
			writing = 0;
		}
		*size = 3;
		break;

	case RADEON_CP_PACKET2:
		break;

	case RADEON_CP_PACKET3:
		*size = ((cmd & RADEON_CP_PACKET_COUNT_MASK) >> 16) + 2;
		break;

	}

	return writing;
}

static int radeon_submit_packet_ring_secure( drm_radeon_private_t *dev_priv,
					     u32 *commands, int *count )
{
#if 0
	int write = dev_priv->sarea_priv->ring_write;
	int *write_ptr = dev_priv->ring_start + write;
	int c = *count;
	u32 tmp = 0;
	int psize = 0;
	int writing = 1;
	int timeout;

	while ( c > 0 ) {
		tmp = *commands++;
		if ( !psize ) {
			writing = radeon_verify_command( dev_priv, tmp, &psize );
		}
		psize--;

		if ( writing ) {
			write++;
			*write_ptr++ = tmp;
		}
		if ( write >= dev_priv->ring_entries ) {
			write = 0;
			write_ptr = dev_priv->ring_start;
		}
		timeout = 0;
		while ( write == *dev_priv->ring_read_ptr ) {
			RADEON_READ( RADEON_PM4_BUFFER_DL_RPTR );
			if ( timeout++ >= dev_priv->usec_timeout )
				return -EBUSY;
			udelay( 1 );
		}
		c--;
	}

	/* Make sure WC cache has been flushed */
	radeon_flush_write_combine();

	dev_priv->sarea_priv->ring_write = write;
	RADEON_WRITE( RADEON_PM4_BUFFER_DL_WPTR, write );

	*count = 0;
#endif
	return 0;
}

static int radeon_submit_packet_ring_insecure( drm_radeon_private_t *dev_priv,
					       u32 *commands, int *count )
{
#if 0
	int write = dev_priv->sarea_priv->ring_write;
	int *write_ptr = dev_priv->ring_start + write;
	int c = *count;
	int timeout;

	while ( c > 0 ) {
		write++;
		*write_ptr++ = *commands++;
		if ( write >= dev_priv->ring_entries ) {
			write = 0;
			write_ptr = dev_priv->ring_start;
		}

		timeout = 0;
		while ( write == *dev_priv->ring_read_ptr ) {
			RADEON_READ( RADEON_PM4_BUFFER_DL_RPTR );
			if ( timeout++ >= dev_priv->usec_timeout )
				return -EBUSY;
			udelay( 1 );
		}
		c--;
	}

	/* Make sure WC cache has been flushed */
	radeon_flush_write_combine();

	dev_priv->sarea_priv->ring_write = write;
	RADEON_WRITE( RADEON_PM4_BUFFER_DL_WPTR, write );

	*count = 0;
#endif
	return 0;
}
#endif

/* Internal packet submission routine.  This uses the insecure versions
 * of the packet submission functions, and thus should only be used for
 * packets generated inside the kernel module.
 */
int radeon_do_submit_packet( drm_radeon_private_t *dev_priv,
			     u32 *buffer, int count )
{
	int c = count;
	int ret = 0;

#if 0
	int left = 0;

	if ( c >= dev_priv->ring_entries ) {
		c = dev_priv->ring_entries - 1;
		left = count - c;
	}

	/* Since this is only used by the kernel we can use the
	 * insecure ring buffer submit packet routine.
	 */
	ret = radeon_submit_packet_ring_insecure( dev_priv, buffer, &c );
	c += left;
#endif

	return ( ret < 0 ) ? ret : c;
}

static int radeon_do_cp_packet( drm_radeon_private_t *dev_priv,
				u32 *commands, int count )
{
	int c;
	RING_LOCALS;

	/* FIXME: Optimize!!! */
	for ( c = 0 ; c < count ; c++ ) {
		BEGIN_RING( 1 );
		OUT_RING( commands[c] );
		ADVANCE_RING();
	}

	return 0;
}

/* External packet submission routine.  This uses the secure versions
 * by default, and can thus submit packets received from user space.
 */
int radeon_cp_packet( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
        drm_file_t *priv = filp->private_data;
        drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_packet_t packet;
	u32 *buffer;
	int ret;

	if ( dev_priv->cp_secure && !capable( CAP_SYS_ADMIN ) ) {
		DRM_ERROR( "radeon_cp_packet called in secure mode "
			   "without permission\n" );
		return -EACCES;
	}

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "radeon_cp_packet called without lock held\n" );
		return -EINVAL;
	}

	if ( copy_from_user( &packet, (drm_radeon_packet_t *)arg,
			     sizeof(packet) ) )
		return -EFAULT;

	buffer = kmalloc( packet.count * sizeof(*buffer), 0 );
	if ( buffer == NULL)
		return -ENOMEM;
	if ( copy_from_user( buffer, packet.buffer,
			     packet.count * sizeof(*buffer) ) )
		return -EFAULT;

	ret = radeon_do_cp_packet( dev_priv, buffer, packet.count );

	kfree( buffer );

	packet.count = 0;
	if ( copy_to_user( (drm_radeon_packet_t *)arg, &packet,
			   sizeof(packet) ) )
		return -EFAULT;

	return ret;
}

#if 0
static int radeon_send_vertbufs( drm_device_t *dev, drm_radeon_vertex_t *v )
{
	drm_device_dma_t    *dma      = dev->dma;
	drm_radeon_private_t  *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	drm_buf_t           *buf;
	int                  i, ret;
	RING_LOCALS;

	/* Make sure we have valid data */
	for (i = 0; i < v->send_count; i++) {
		int idx = v->send_indices[i];

		if (idx < 0 || idx >= dma->buf_count) {
			DRM_ERROR("Index %d (of %d max)\n",
				  idx, dma->buf_count - 1);
			return -EINVAL;
		}
		buf = dma->buflist[idx];
		if (buf->pid != current->pid) {
			DRM_ERROR("Process %d using buffer owned by %d\n",
				  current->pid, buf->pid);
			return -EINVAL;
		}
		if (buf->pending) {
			DRM_ERROR("Sending pending buffer:"
				  " buffer %d, offset %d\n",
				  v->send_indices[i], i);
			return -EINVAL;
		}
	}

	/* Wait for idle, if we've wrapped to make sure that all pending
           buffers have been processed */
	if (dev_priv->submit_age == RADEON_MAX_VBUF_AGE) {
		if ((ret = radeon_do_cp_idle(dev)) < 0) return ret;
		dev_priv->submit_age = 0;
		radeon_freelist_reset(dev);
	}

	/* Make sure WC cache has been flushed (if in PIO mode) */
	if (!dev_priv->cp_is_bm_mode) radeon_flush_write_combine();

	/* FIXME: Add support for sending vertex buffer to the CP here
	   instead of in client code.  The v->prim holds the primitive
	   type that should be drawn.  Loop over the list buffers in
	   send_indices[] and submit a packet for each VB.

	   This will require us to loop over the clip rects here as
	   well, which implies that we extend the kernel driver to allow
	   cliprects to be stored here.  Note that the cliprects could
	   possibly come from the X server instead of the client, but
	   this will require additional changes to the DRI to allow for
	   this optimization. */

	/* Submit a CP packet that writes submit_age to RADEON_VB_AGE_REG */
#if 0
	cp_buffer[0] = RADEONCP0(RADEON_CP_PACKET0, RADEON_VB_AGE_REG, 0);
	cp_buffer[1] = dev_priv->submit_age;

	if ((ret = radeon_do_submit_packet(dev, cp_buffer, 2)) < 0) {
		/* Until we add support for sending VBs to the CP in
		   this routine, we can recover from this error.  After
		   we add that support, we won't be able to easily
		   recover, so we will probably have to implement
		   another mechanism for handling timeouts from packets
		   submitted directly by the kernel. */
		return ret;
	}
#else
	BEGIN_RING( 2 );

	OUT_RING( CP_PACKET0( RADEON_VB_AGE_REG, 0 ) );
	OUT_RING( dev_priv->submit_age );

	ADVANCE_RING();
#endif
	/* Now that the submit packet request has succeeded, we can mark
           the buffers as pending */
	for (i = 0; i < v->send_count; i++) {
		buf = dma->buflist[v->send_indices[i]];
		buf->pending = 1;

		buf_priv      = buf->dev_private;
		buf_priv->age = dev_priv->submit_age;
	}

	dev_priv->submit_age++;

	return 0;
}
#endif




static int radeon_cp_get_buffers( drm_device_t *dev, drm_dma_t *d )
{
	int i;
	drm_buf_t *buf;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = radeon_freelist_get( dev );
		if ( !buf ) return -EAGAIN;

		buf->pid = current->pid;

		if ( copy_to_user( &d->request_indices[i], &buf->idx,
				   sizeof(buf->idx) ) )
			return -EFAULT;
		if ( copy_to_user( &d->request_sizes[i], &buf->total,
				   sizeof(buf->total) ) )
			return -EFAULT;

		d->granted_count++;
	}
	return 0;
}

int radeon_cp_buffers( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	int ret = 0;
	drm_dma_t d;

	if ( copy_from_user( &d, (drm_dma_t *) arg, sizeof(d) ) )
		return -EFAULT;

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}

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
		ret = radeon_cp_get_buffers( dev, &d );
	}

	if ( copy_to_user( (drm_dma_t *) arg, &d, sizeof(d) ) )
		return -EFAULT;

	return ret;
}
