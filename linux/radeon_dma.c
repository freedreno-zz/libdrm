/* radeon_dma.c -- ATI Radeon driver -*- linux-c -*-
 * Created: Wed Apr  5 19:24:19 2000 by kevin@precisioninsight.com
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 * Authors: Kevin E. Martin <martin@valinux.com>
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "radeon_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>



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


#define RADEON_MAX_VBUF_AGE	0x10000000
#define RADEON_VB_AGE_REG	RADEON_GUI_SCRATCH_REG0

int RADEON_READ_PLL(drm_device_t *dev, int addr)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX, addr & 0x1f);
	return RADEON_READ(RADEON_CLOCK_CNTL_DATA);
}

#define radeon_flush_write_combine()	mb()


static void radeon_status(drm_device_t *dev)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;

	printk("GUI_STAT           = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_GUI_STAT));
	printk("PM4_STAT           = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_PM4_STAT));
	printk("PM4_BUFFER_DL_WPTR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_PM4_BUFFER_DL_WPTR));
	printk("PM4_BUFFER_DL_RPTR = 0x%08x\n",
	       (unsigned int)RADEON_READ(RADEON_PM4_BUFFER_DL_RPTR));
}

static int radeon_do_cleanup_cp(drm_device_t *dev)
{
	if (dev->dev_private) {
		drm_radeon_private_t *dev_priv = dev->dev_private;

		if (!dev_priv->is_pci) {
			DO_REMAPFREE(dev_priv->agp_ring);
			DO_REMAPFREE(dev_priv->agp_read_ptr);
			DO_REMAPFREE(dev_priv->agp_vertbufs);
			DO_REMAPFREE(dev_priv->agp_indbufs);
			DO_REMAPFREE(dev_priv->agp_textures);
		}

		drm_free(dev->dev_private, sizeof(drm_radeon_private_t),
			 DRM_MEM_DRIVER);
		dev->dev_private = NULL;
	}

	return 0;
}

static int radeon_do_init_cp(drm_device_t *dev, drm_radeon_init_t *init)
{
	drm_radeon_private_t *dev_priv;
        int                  i;

	dev_priv = drm_alloc(sizeof(drm_radeon_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL) return -ENOMEM;
	dev->dev_private = (void *)dev_priv;

	memset(dev_priv, 0, sizeof(drm_radeon_private_t));

	dev_priv->is_pci         = init->is_pci;

	dev_priv->usec_timeout   = init->usec_timeout;
	if (dev_priv->usec_timeout < 1 ||
	    dev_priv->usec_timeout > RADEON_MAX_USEC_TIMEOUT) {
		drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);
		dev->dev_private = NULL;
		return -EINVAL;
	}

	dev_priv->cp_mode       = init->cp_mode;
	dev_priv->cp_fifo_size  = init->cp_fifo_size;
#if 1
	dev_priv->cp_is_bm_mode = 1;
#else
	dev_priv->cp_is_bm_mode =
		((init->cp_mode == RADEON_CSQ_PRIBM_INDDIS) ||
		 (init->cp_mode == RADEON_CSQ_PRIBM_INDBM));
#endif
	dev_priv->cp_secure     = init->cp_secure;

	if (dev_priv->cp_is_bm_mode && dev_priv->is_pci) {
		drm_free(dev_priv, sizeof(*dev_priv), DRM_MEM_DRIVER);
		dev->dev_private = NULL;
		return -EINVAL;
	}

	for (i = 0; i < dev->map_count; i++) {
		if (dev->maplist[i]->type == _DRM_SHM) {
			dev_priv->sarea = dev->maplist[i];
			break;
		}
	}

	DO_FIND_MAP(dev_priv->fb,           init->fb_offset);
	if (!dev_priv->is_pci) {
		DO_FIND_MAP(dev_priv->agp_ring,     init->agp_ring_offset);
		DO_FIND_MAP(dev_priv->agp_read_ptr, init->agp_read_ptr_offset);
		DO_FIND_MAP(dev_priv->agp_vertbufs, init->agp_vertbufs_offset);
		DO_FIND_MAP(dev_priv->agp_indbufs,  init->agp_indbufs_offset);
		DO_FIND_MAP(dev_priv->agp_textures, init->agp_textures_offset);
	}
	DO_FIND_MAP(dev_priv->mmio,         init->mmio_offset);

	dev_priv->sarea_priv =
		(drm_radeon_sarea_t *)((u8 *)dev_priv->sarea->handle +
				       init->sarea_priv_offset);

	if (!dev_priv->is_pci) {
		DO_REMAP(dev_priv->agp_ring);
		DO_REMAP(dev_priv->agp_read_ptr);
		DO_REMAP(dev_priv->agp_vertbufs);
#if 0
		DO_REMAP(dev_priv->agp_indirectbufs);
		DO_REMAP(dev_priv->agp_textures);
#endif

		dev_priv->ring_size     = init->ring_size;
		dev_priv->ring_sizel2qw = drm_order(init->ring_size/8);
		dev_priv->ring_entries  = init->ring_size/sizeof(u32);
		dev_priv->ring_read_ptr = ((__volatile__ u32 *)
					   dev_priv->agp_read_ptr->handle);
		dev_priv->ring_start    = (u32 *)dev_priv->agp_ring->handle;
		dev_priv->ring_end      = ((u32 *)dev_priv->agp_ring->handle
					   + dev_priv->ring_entries);
	}

	dev_priv->submit_age    = 0;
#if 0
	RADEON_WRITE(RADEON_VB_AGE_REG, dev_priv->submit_age);
#endif

	return 0;
}

int radeon_init_cp(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
        drm_file_t        *priv   = filp->private_data;
        drm_device_t      *dev    = priv->dev;
	drm_radeon_init_t init;

	if (copy_from_user(&init, (drm_radeon_init_t *)arg, sizeof(init)))
		return -EFAULT;

	switch (init.func) {
	case RADEON_INIT_CP:
		return radeon_do_init_cp(dev, &init);
	case RADEON_CLEANUP_CP:
		return radeon_do_cleanup_cp(dev);
	}

	return -EINVAL;
}

static void radeon_mark_vertbufs_done(drm_device_t *dev)
{
	drm_device_dma_t   *dma      = dev->dma;
	int                i;

	for (i = 0; i < dma->buf_count; i++) {
		drm_buf_t             *buf      = dma->buflist[i];
		drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
		buf_priv->age = 0;
	}
}

static int radeon_do_pixcache_flush(drm_device_t *dev)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32                  tmp;
	int                  i;

	tmp = RADEON_READ(RADEON_PC_NGUI_CTLSTAT) | RADEON_PC_FLUSH_ALL;
	RADEON_WRITE(RADEON_PC_NGUI_CTLSTAT, tmp);

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(RADEON_READ(RADEON_PC_NGUI_CTLSTAT) & RADEON_PC_BUSY))
			return 0;
		udelay(1);
	}

	return -EBUSY;
#else
	return 0;
#endif
}

static int radeon_do_wait_for_fifo(drm_device_t *dev, int entries)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int                  i;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		int slots = RADEON_READ(RADEON_GUI_STAT) & RADEON_GUI_FIFOCNT_MASK;
		if (slots >= entries) return 0;
		udelay(1);
	}
	return -EBUSY;
#else
	return 0;
#endif
}

static int radeon_do_wait_for_idle(drm_device_t *dev)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int                  i, ret;

	if (!(ret = radeon_do_wait_for_fifo(dev, 64))) return ret;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		if (!(RADEON_READ(RADEON_GUI_STAT) & RADEON_GUI_ACTIVE)) {
			(void)radeon_do_pixcache_flush(dev);
			return 0;
		}
		udelay(1);
	}
	return -EBUSY;
#else
	return 0;
#endif
}

int radeon_do_engine_reset(drm_device_t *dev)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32                  clock_cntl_index, mclk_cntl, gen_reset_cntl;

	(void)radeon_do_pixcache_flush(dev);

	clock_cntl_index = RADEON_READ(RADEON_CLOCK_CNTL_INDEX);
	mclk_cntl        = RADEON_READ_PLL(dev, RADEON_MCLK_CNTL);

	RADEON_WRITE_PLL(RADEON_MCLK_CNTL,
			 mclk_cntl | RADEON_FORCE_GCP | RADEON_FORCE_PIPE3D_CP);

	gen_reset_cntl   = RADEON_READ(RADEON_GEN_RESET_CNTL);

	RADEON_WRITE(RADEON_GEN_RESET_CNTL, gen_reset_cntl | RADEON_SOFT_RESET_GUI);
	(void)RADEON_READ(RADEON_GEN_RESET_CNTL);
	RADEON_WRITE(RADEON_GEN_RESET_CNTL, gen_reset_cntl & ~RADEON_SOFT_RESET_GUI);
	(void)RADEON_READ(RADEON_GEN_RESET_CNTL);

	RADEON_WRITE_PLL(RADEON_MCLK_CNTL,    mclk_cntl);
	RADEON_WRITE(RADEON_CLOCK_CNTL_INDEX, clock_cntl_index);
	RADEON_WRITE(RADEON_GEN_RESET_CNTL,   gen_reset_cntl);

	/* For CP ring buffer only */
	if (dev_priv->cp_is_bm_mode) {
		RADEON_WRITE(RADEON_PM4_BUFFER_DL_WPTR, 0);
		RADEON_WRITE(RADEON_PM4_BUFFER_DL_RPTR, 0);
		*dev_priv->ring_read_ptr = 0;
		dev_priv->sarea_priv->ring_write = 0;
	}

	/* Reset the CP mode */
	(void)radeon_do_wait_for_idle(dev);
	RADEON_WRITE(RADEON_PM4_BUFFER_CNTL,
		     dev_priv->cp_mode | dev_priv->ring_sizel2qw);
	(void)RADEON_READ(RADEON_PM4_BUFFER_ADDR); /* as per the sample code */
	RADEON_WRITE(RADEON_PM4_MICRO_CNTL, RADEON_PM4_MICRO_FREERUN);

	radeon_mark_vertbufs_done(dev);
	return 0;
#else
	return 0;
#endif
}

int radeon_eng_reset(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
        drm_file_t        *priv   = filp->private_data;
        drm_device_t      *dev    = priv->dev;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||
	    dev->lock.pid != current->pid) {
		DRM_ERROR("radeon_eng_reset called without holding the lock\n");
		return -EINVAL;
	}

	return radeon_do_engine_reset(dev);
}

static int radeon_do_engine_flush(drm_device_t *dev)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32                 tmp;

	tmp = RADEON_READ(RADEON_PM4_BUFFER_DL_WPTR);
	RADEON_WRITE(RADEON_PM4_BUFFER_DL_WPTR, tmp | RADEON_PM4_BUFFER_DL_DONE);
#endif

	return 0;
}

int radeon_eng_flush(struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg)
{
        drm_file_t        *priv   = filp->private_data;
        drm_device_t      *dev    = priv->dev;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||
	    dev->lock.pid != current->pid) {
		DRM_ERROR("radeon_eng_flush called without holding the lock\n");
		return -EINVAL;
	}

	return radeon_do_engine_flush(dev);
}

static int radeon_do_cp_wait_for_fifo(drm_device_t *dev, int entries)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int                  i;

	for (i = 0; i < dev_priv->usec_timeout; i++) {
		int slots = RADEON_READ(RADEON_PM4_STAT) & RADEON_PM4_FIFOCNT_MASK;
		if (slots >= entries) return 0;
		udelay(1);
	}
	return -EBUSY;
#else
	return 0;
#endif
}

int radeon_do_cp_wait_for_idle(drm_device_t *dev)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int                  i;

	if (dev_priv->cp_is_bm_mode) {
		for (i = 0; i < dev_priv->usec_timeout; i++) {
			if (*dev_priv->ring_read_ptr == dev_priv->sarea_priv->ring_write) {
				int pm4stat = RADEON_READ(RADEON_PM4_STAT);
				if ((pm4stat & RADEON_PM4_FIFOCNT_MASK) >= dev_priv->cp_fifo_size &&
				    !(pm4stat & (RADEON_PM4_BUSY | RADEON_PM4_GUI_ACTIVE))) {
					return radeon_do_pixcache_flush(dev);
				}
			}
			udelay(1);
		}
		return -EBUSY;
	} else {
		int ret = radeon_do_cp_wait_for_fifo(dev, dev_priv->cp_fifo_size);
		if (ret < 0) return ret;

		for (i = 0; i < dev_priv->usec_timeout; i++) {
			int pm4stat = RADEON_READ(RADEON_PM4_STAT);
			if (!(pm4stat & (RADEON_PM4_BUSY | RADEON_PM4_GUI_ACTIVE))) {
				return radeon_do_pixcache_flush(dev);
			}
			udelay(1);
		}
		return -EBUSY;
	}
#else
	return 0;
#endif
}

int radeon_cp_idle(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
        drm_file_t         *priv     = filp->private_data;
        drm_device_t       *dev      = priv->dev;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||
	    dev->lock.pid != current->pid) {
		DRM_ERROR("radeon_wait_idle called without holding the lock\n");
		return -EINVAL;
	}

	return radeon_do_cp_wait_for_idle(dev);
}

static int radeon_submit_packets_ring_secure(drm_device_t *dev,
					     u32 *commands, int *count)
{
#if 0
	drm_radeon_private_t *dev_priv  = dev->dev_private;
	int                  write      = dev_priv->sarea_priv->ring_write;
	int                  *write_ptr = dev_priv->ring_start + write;
	int                  c          = *count;
	u32                  tmp        = 0;
	int                  psize      = 0;
	int                  writing    = 1;
	int                  timeout;

	while (c > 0) {
		tmp = *commands++;
		if (!psize) {
			writing = 1;

			if ((tmp & RADEON_CP_PACKET_MASK) == RADEON_CP_PACKET0) {
				if ((tmp & RADEON_CP_PACKET0_REG_MASK) <= (0x1004 >> 2)) {
					if ((tmp & RADEON_CP_PACKET0_REG_MASK) !=
					    (RADEON_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				}
				psize = ((tmp & RADEON_CP_PACKET_COUNT_MASK) >> 16) + 2;
			} else if ((tmp & RADEON_CP_PACKET_MASK) == RADEON_CP_PACKET1) {
				if ((tmp & RADEON_CP_PACKET1_REG0_MASK) <= (0x1004 >> 2)) {
					if ((tmp & RADEON_CP_PACKET1_REG0_MASK) !=
					    (RADEON_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				} else if ((tmp & RADEON_CP_PACKET1_REG1_MASK) <=
					   (0x1004 << 9)) {
					if ((tmp & RADEON_CP_PACKET1_REG1_MASK) !=
					    (RADEON_PM4_VC_FPU_SETUP << 9)) {
						writing = 0;
					}
				}
				psize = 3;
			} else {
				psize = ((tmp & RADEON_CP_PACKET_COUNT_MASK) >> 16) + 2;
			}
		}
		psize--;

		if (writing) {
			write++;
			*write_ptr++ = tmp;
		}
		if (write >= dev_priv->ring_entries) {
			write     = 0;
			write_ptr = dev_priv->ring_start;
		}
		timeout = 0;
		while (write == *dev_priv->ring_read_ptr) {
			(void)RADEON_READ(RADEON_PM4_BUFFER_DL_RPTR);
			if (timeout++ >= dev_priv->usec_timeout)
				return -EBUSY;
			udelay(1);
		}
		c--;
	}

	if (write < 32)
	    memcpy(dev_priv->ring_end,
		   dev_priv->ring_start,
		   write * sizeof(u32));

	/* Make sure WC cache has been flushed */
	radeon_flush_write_combine();

	dev_priv->sarea_priv->ring_write = write;
	RADEON_WRITE(RADEON_PM4_BUFFER_DL_WPTR, write);

	*count = 0;

	return 0;
#else
	*count = 0;
	return 0;
#endif
}

static int radeon_submit_packets_pio_secure(drm_device_t *dev,
					    u32 *commands, int *count)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	u32                  tmp       = 0;
	int                  psize     = 0;
	int                  writing   = 1;
	int                  addr      = RADEON_PM4_FIFO_DATA_EVEN;
	int                  ret;

	while (*count > 0) {
		tmp = *commands++;
		if (!psize) {
			writing = 1;

			if ((tmp & RADEON_CP_PACKET_MASK) == RADEON_CP_PACKET0) {
				if ((tmp & RADEON_CP_PACKET0_REG_MASK) <= (0x1004 >> 2)) {
					if ((tmp & RADEON_CP_PACKET0_REG_MASK) !=
					    (RADEON_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				}
				psize = ((tmp & RADEON_CP_PACKET_COUNT_MASK) >> 16) + 2;
			} else if ((tmp & RADEON_CP_PACKET_MASK) == RADEON_CP_PACKET1) {
				if ((tmp & RADEON_CP_PACKET1_REG0_MASK) <= (0x1004 >> 2)) {
					if ((tmp & RADEON_CP_PACKET1_REG0_MASK) !=
					    (RADEON_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				} else if ((tmp & RADEON_CP_PACKET1_REG1_MASK) <=
					   (0x1004 << 9)) {
					if ((tmp & RADEON_CP_PACKET1_REG1_MASK) !=
					    (RADEON_PM4_VC_FPU_SETUP << 9)) {
						writing = 0;
					}
				}
				psize = 3;
			} else {
				psize = ((tmp & RADEON_CP_PACKET_COUNT_MASK) >> 16) + 2;
			}
		}
		psize--;

		if (writing) {
			if ((ret = radeon_do_cp_wait_for_fifo(dev, 1)) < 0)
				return ret;
			RADEON_WRITE(addr, tmp);
			addr ^= 0x0004;
		}

		*count -= 1;
	}

	if (addr == RADEON_PM4_FIFO_DATA_ODD) {
		if ((ret = radeon_do_cp_wait_for_fifo(dev, 1)) < 0) return ret;
		RADEON_WRITE(addr, RADEON_CP_PACKET2);
	}

	return 0;
#else
	*count = 0;
	return 0;
#endif
}

static int radeon_submit_packets_ring(drm_device_t *dev,
				      u32 *commands, int *count)
{
#if 0
	drm_radeon_private_t *dev_priv  = dev->dev_private;
	int                  write      = dev_priv->sarea_priv->ring_write;
	int                  *write_ptr = dev_priv->ring_start + write;
	int                  c          = *count;
	int                  timeout;

	while (c > 0) {
		write++;
		*write_ptr++ = *commands++;
		if (write >= dev_priv->ring_entries) {
			write     = 0;
			write_ptr = dev_priv->ring_start;
		}
		timeout = 0;
		while (write == *dev_priv->ring_read_ptr) {
			(void)RADEON_READ(RADEON_PM4_BUFFER_DL_RPTR);
			if (timeout++ >= dev_priv->usec_timeout)
				return -EBUSY;
			udelay(1);
		}
		c--;
	}

	if (write < 32)
	    memcpy(dev_priv->ring_end,
		   dev_priv->ring_start,
		   write * sizeof(u32));

	/* Make sure WC cache has been flushed */
	radeon_flush_write_combine();

	dev_priv->sarea_priv->ring_write = write;
	RADEON_WRITE(RADEON_PM4_BUFFER_DL_WPTR, write);

	*count = 0;

	return 0;
#else
	*count = 0;
	return 0;
#endif
}

static int radeon_submit_packets_pio(drm_device_t *dev,
				     u32 *commands, int *count)
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int                  ret;

	while (*count > 1) {
		if ((ret = radeon_do_cp_wait_for_fifo(dev, 2)) < 0) return ret;
		RADEON_WRITE(RADEON_PM4_FIFO_DATA_EVEN, *commands++);
		RADEON_WRITE(RADEON_PM4_FIFO_DATA_ODD,  *commands++);
		*count -= 2;
	}

	if (*count) {
		if ((ret = radeon_do_cp_wait_for_fifo(dev, 2)) < 0) return ret;
		RADEON_WRITE(RADEON_PM4_FIFO_DATA_EVEN, *commands++);
		RADEON_WRITE(RADEON_PM4_FIFO_DATA_ODD,  RADEON_CP_PACKET2);
		*count = 0;
	}

	return 0;
#else
	*count = 0;
	return 0;
#endif
}

static int radeon_do_submit_packets(drm_device_t *dev, u32 *buffer, int count)
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	int                  c         = count;
	int                  ret;

	if (dev_priv->cp_is_bm_mode) {
		int left = 0;

		if (c >= dev_priv->ring_entries) {
			c    = dev_priv->ring_entries-1;
			left = count - c;
		}

		/* Since this is only used by the kernel we can use the
                   insecure ring buffer submit packet routine */
		ret = radeon_submit_packets_ring(dev, buffer, &c);

		c += left;
	} else {
		/* Since this is only used by the kernel we can use the
                   insecure PIO submit packet routine */
		ret = radeon_submit_packets_pio(dev, buffer, &c);
	}

	if (ret < 0) return ret;
	else         return c;
}

int radeon_submit_pkt(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
        drm_file_t           *priv     = filp->private_data;
        drm_device_t         *dev      = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_packet_t  packet;
	u32                  *buffer;
	int                  c;
	int                  size;
	int                  ret       = 0;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||
	    dev->lock.pid != current->pid) {
		DRM_ERROR("radeon_submit_pkt called without holding the lock\n");
		return -EINVAL;
	}

	if (copy_from_user(&packet, (drm_radeon_packet_t *)arg, sizeof(packet)))
		return -EFAULT;

	c    = packet.count;
	size = c * sizeof(*buffer);

	if (dev_priv->cp_is_bm_mode) {
		int left = 0;

		if (c >= dev_priv->ring_entries) {
			c    = dev_priv->ring_entries-1;
			size = c * sizeof(*buffer);
			left = packet.count - c;
		}

		if ((buffer = kmalloc(size, 0)) == NULL) return -ENOMEM;
		if (copy_from_user(buffer, packet.buffer, size))
			return -EFAULT;

		if (dev_priv->cp_secure)
			ret = radeon_submit_packets_ring_secure(dev, buffer, &c);
		else
			ret = radeon_submit_packets_ring(dev, buffer, &c);

		c += left;
	} else {
		if ((buffer = kmalloc(size, 0)) == NULL) return -ENOMEM;
		if (copy_from_user(buffer, packet.buffer, size))
			return -EFAULT;

		if (dev_priv->cp_secure)
			ret = radeon_submit_packets_pio_secure(dev, buffer, &c);
		else
			ret = radeon_submit_packets_pio(dev, buffer, &c);
	}

	kfree(buffer);

	packet.count = c;
	if (copy_to_user((drm_radeon_packet_t *)arg, &packet, sizeof(packet)))
		return -EFAULT;

	if (ret)        return ret;
	else if (c > 0) return -EAGAIN;

	return 0;
}

static int radeon_send_vertbufs(drm_device_t *dev, drm_radeon_vertex_t *v)
{
#if 0
	drm_device_dma_t      *dma      = dev->dma;
	drm_radeon_private_t  *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	drm_buf_t             *buf;
	int                   i, ret;
	u32                   cp[2];

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
		if ((ret = radeon_do_cp_wait_for_idle(dev)) < 0) return ret;
		dev_priv->submit_age = 0;
		radeon_mark_vertbufs_done(dev);
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
	cp[0] = RADEONCP0(RADEON_CP_PACKET0, RADEON_VB_AGE_REG, 0);
	cp[1] = dev_priv->submit_age;
	if ((ret = radeon_do_submit_packets(dev, cp, 2)) < 0) {
		/* Until we add support for sending VBs to the CP in
		   this routine, we can recover from this error.  After
		   we add that support, we won't be able to easily
		   recover, so we will probably have to implement
		   another mechanism for handling timeouts from packets
		   submitted directly by the kernel. */
		return ret;
	}

	/* Now that the submit packet request has succeeded, we can mark
           the buffers as pending */
	for (i = 0; i < v->send_count; i++) {
		buf           = dma->buflist[v->send_indices[i]];
		buf->pending  = 1;

		buf_priv      = buf->dev_private;
		buf_priv->age = dev_priv->submit_age;
	}

	dev_priv->submit_age++;

	return 0;
#else
	return 0;
#endif
}

static drm_buf_t *radeon_freelist_get(drm_device_t *dev)
{
	drm_device_dma_t      *dma      = dev->dma;
	drm_radeon_private_t  *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv;
	drm_buf_t             *buf;
	int                   i, t;

	/* FIXME: Optimize -- use freelist code */

	for (i = 0; i < dma->buf_count; i++) {
		buf = dma->buflist[i];
		buf_priv = buf->dev_private;
		if (buf->pid == 0) return buf;
	}

	for (t = 0; t < dev_priv->usec_timeout; t++) {
		u32 done_age = RADEON_READ(RADEON_VB_AGE_REG);

		for (i = 0; i < dma->buf_count; i++) {
			buf = dma->buflist[i];
			buf_priv = buf->dev_private;
			if (buf->pending && buf_priv->age <= done_age) {
				/* The buffer has been processed, so it
                                   can now be used */
				buf->pending = 0;
				return buf;
			}
		}
		udelay(1);
	}

	radeon_status(dev);
	return NULL;
}


static int radeon_get_vertbufs(drm_device_t *dev, drm_radeon_vertex_t *v)
{
	drm_buf_t *buf;
	int        i;

	for (i = v->granted_count; i < v->request_count; i++) {
		buf = radeon_freelist_get(dev);
		if (!buf) break;
#if 0
		buf->pid = current->pid;
#endif
		if (copy_to_user(&v->request_indices[i],
				 &buf->idx,
				 sizeof(buf->idx)) ||
		    copy_to_user(&v->request_sizes[i],
				 &buf->total,
				 sizeof(buf->total)))
			return -EFAULT;
		++v->granted_count;
	}
	return 0;
}

int radeon_vertex_buf(struct inode *inode, struct file *filp, unsigned int cmd,
		      unsigned long arg)
{
	drm_file_t	     *priv     = filp->private_data;
	drm_device_t	     *dev      = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t     *dma      = dev->dma;
	int		     retcode   = 0;
	drm_radeon_vertex_t  v;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||
	    dev->lock.pid != current->pid) {
		DRM_ERROR("radeon_vertex_buf called without holding the lock\n");
		return -EINVAL;
	}

	if (!dev_priv || dev_priv->is_pci) {
		DRM_ERROR("radeon_vertex_buf called with a PCI card\n");
		return -EINVAL;
	}

	if (copy_from_user(&v, (drm_radeon_vertex_t *)arg, sizeof(v)))
		return -EFAULT;
	DRM_DEBUG("%d: %d send, %d req\n",
		  current->pid, v.send_count, v.request_count);

	if (v.send_count < 0 || v.send_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to send %d buffers (of %d max)\n",
			  current->pid, v.send_count, dma->buf_count);
		return -EINVAL;
	}
	if (v.request_count < 0 || v.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, v.request_count, dma->buf_count);
		return -EINVAL;
	}

	if (v.send_count) {
		retcode = radeon_send_vertbufs(dev, &v);
	}

	v.granted_count = 0;

	if (!retcode && v.request_count) {
		retcode = radeon_get_vertbufs(dev, &v);
	}

	DRM_DEBUG("%d returning, granted = %d\n",
		  current->pid, v.granted_count);
	if (copy_to_user((drm_radeon_vertex_t *)arg, &v, sizeof(v)))
		return -EFAULT;

	return retcode;
}
