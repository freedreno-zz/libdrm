/* r128_drv.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Mon Dec 13 09:47:27 1999 by faith@precisioninsight.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 *          Kevin E. Martin <kevin@precisioninsight.com>
 * 
 * $XFree86$
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "r128_drv.h"

#include <linux/interrupt.h>	/* For task queue support */



#define R128_PC_NGUI_CTLSTAT	0x0184
#       define R128_PC_FLUSH_ALL	0x00ff
#       define R128_PC_BUSY		(1 << 31)

#define R128_CLOCK_CNTL_INDEX	0x0008
#define R128_CLOCK_CNTL_DATA	0x000c
#       define R128_PLL_WR_EN		(1 << 7)

#define R128_MCLK_CNTL		0x000f
#       define R128_FORCE_GCP		(1 << 16)
#       define R128_FORCE_PIPE3D_CPP	(1 << 17)

#define R128_GEN_RESET_CNTL	0x00f0
#       define R128_SOFT_RESET_GUI	(1 <<  0)

#define R128_PM4_BUFFER_CNTL	0x0704
#       define R128_PM4_NONPM4			(0  << 28)
#       define R128_PM4_192PIO			(1  << 28)
#       define R128_PM4_192BM			(2  << 28)
#       define R128_PM4_128PIO_64INDBM		(3  << 28)
#       define R128_PM4_128BM_64INDBM		(4  << 28)
#       define R128_PM4_64PIO_128INDBM		(5  << 28)
#       define R128_PM4_64BM_128INDBM		(6  << 28)
#       define R128_PM4_64PIO_64VCBM_64INDBM	(7  << 28)
#       define R128_PM4_64BM_64VCBM_64INDBM	(8  << 28)
#       define R128_PM4_64PIO_64VCPIO_64INDPIO	(15 << 28)


#define R128_PM4_BUFFER_DL_RPTR	0x0710
#define R128_PM4_BUFFER_DL_WPTR	0x0714
#       define R128_PM4_BUFFER_DL_DONE		(1 << 31)

#define R128_PM4_VC_FPU_SETUP	0x071c

#define R128_PM4_STAT		0x07b8
#       define R128_PM4_FIFOCNT_MASK		0x0fff
#       define R128_PM4_BUSY			(1 << 16)
#       define R128_PM4_GUI_ACTIVE		(1 << 31)

#define R128_PM4_FIFO_DATA_EVEN	0x1000
#define R128_PM4_FIFO_DATA_ODD	0x1004

#define R128_CCE_PACKET0	0x00000000
#define R128_CCE_PACKET1	0x40000000
#define R128_CCE_PACKET2	0x80000000
#       define R128_CCE_PACKET_MASK		0xC0000000
#       define R128_CCE_PACKET_COUNT_MASK	0x3fff0000
#       define R128_CCE_PACKET0_REG_MASK	0x000007ff
#       define R128_CCE_PACKET1_REG0_MASK	0x000007ff
#       define R128_CCE_PACKET1_REG1_MASK	0x003ff800

#define R128_TIMEOUT		2000000

/* WARNING!!! MAGIC NUMBERS!!!  The number of regions already added to
   the kernel must be specified here.  This must match the order the X
   server uses for instantiating register regions, or must be passed in
   a new ioctl. */
#define R128_SAREA()		0
#define R128_FB()		1
#define R128_AGP_RING()		2
#define R128_AGP_READ_PTR()	3
#define R128_AGP_VERTBUFS()	4
#define R128_AGP_INDIRECTBUFS()	5
#define R128_AGP_TEXTURES()	6
#define R128_REG(reg)		7

#define R128_BASE(reg)		((u32)((drm_device_t *)dev)->maplist[R128_REG(reg)]->handle)
#define R128_ADDR(reg)		(R128_BASE(reg) + reg)

#define R128_DEREF(reg)		*(__volatile__ int *)R128_ADDR(reg)
#define R128_READ(reg)		R128_DEREF(reg)
#define R128_WRITE(reg,val)	do { R128_DEREF(reg) = val; } while (0)

#define R128_DEREF8(reg)	*(__volatile__ char *)R128_ADDR(reg)
#define R128_READ8(reg)		R128_DEREF8(reg)
#define R128_WRITE8(reg,val)	do { R128_DEREF8(reg) = val; } while (0)

#define R128_WRITE_PLL(addr,val)                                              \
do {                                                                          \
	R128_WRITE8(R128_CLOCK_CNTL_INDEX, ((addr) & 0x1f) | R128_PLL_WR_EN); \
	R128_WRITE(R128_CLOCK_CNTL_DATA, (val));                              \
} while (0)

static int R128_READ_PLL(drm_device_t *dev, int addr)
{
	R128_WRITE8(R128_CLOCK_CNTL_INDEX, addr & 0x1f);
	return R128_READ(R128_CLOCK_CNTL_DATA);
}

static int r128_do_init_cce(drm_device_t *dev, drm_r128_init_t *init)
{
	drm_r128_private_t *dev_priv;
	drm_map_t          *map = NULL;

	dev_priv = drm_alloc(sizeof(drm_r128_private_t), DRM_MEM_DRIVER);
	if (dev_priv == NULL) return -ENOMEM;
	dev->dev_private = (void *)dev_priv;

	memset(dev_priv, 0, sizeof(drm_r128_private_t));

	dev_priv->cce_mode       = init->cce_mode;
	dev_priv->cce_fifo_size  = init->cce_fifo_size;
	dev_priv->cce_is_bm_mode =
		((init->cce_mode == R128_PM4_192BM) ||
		 (init->cce_mode == R128_PM4_128BM_64INDBM) ||
		 (init->cce_mode == R128_PM4_64BM_128INDBM) ||
		 (init->cce_mode == R128_PM4_64BM_64VCBM_64INDBM));
	dev_priv->cce_secure     = init->cce_secure;

	map = dev->maplist[R128_SAREA()];
	dev_priv->sarea_priv = (drm_r128_sarea_t *)((u8 *)map->handle +
						    init->sarea_priv_offset);

#define DO_REMAP(v)                                                         \
	do {                                                                \
		drm_map_t *_m;                                              \
		_m = dev->maplist[(v)];                                     \
		_m->handle = drm_ioremap(_m->offset, _m->size);             \
	} while (0)

	DO_REMAP(R128_AGP_RING());
	DO_REMAP(R128_AGP_READ_PTR());
	DO_REMAP(R128_AGP_VERTBUFS());
#if 0
	DO_REMAP(R128_AGP_INDIRECTBUFS());
	DO_REMAP(R128_AGP_TEXTURES());
#endif

	dev_priv->ring_size     = init->ring_size;
	dev_priv->ring_entries  = init->ring_size/sizeof(u32);
	dev_priv->ring_read_ptr = (__volatile__ u32 *)
				   dev->maplist[R128_AGP_READ_PTR()]->handle;
	dev_priv->ring_start    = (u32 *)dev->maplist[R128_AGP_RING()]->handle;
	dev_priv->ring_end      = ((u32 *)dev->maplist[R128_AGP_RING()]->handle
				   + dev_priv->ring_entries);

	return 0;
}

static int r128_do_cleanup_cce(drm_device_t *dev)
{
#define DO_REMAPFREE(v)                                                     \
	do {                                                                \
		drm_map_t *_m;                                              \
		_m = dev->maplist[(v)];                                     \
		drm_ioremapfree(_m->handle, _m->size);                      \
	} while (0)

	DO_REMAPFREE(R128_AGP_RING());
	DO_REMAPFREE(R128_AGP_READ_PTR());
	DO_REMAPFREE(R128_AGP_VERTBUFS());
#if 0
	DO_REMAPFREE(R128_AGP_INDIRECTBUFS());
	DO_REMAPFREE(R128_AGP_TEXTURES());
#endif

	return 0;
}

int r128_init_cce(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
        drm_file_t        *priv   = filp->private_data;
        drm_device_t      *dev    = priv->dev;
	drm_r128_init_t    init;

	copy_from_user_ret(&init, (drm_r128_init_t *)arg, sizeof(init),
			   -EFAULT);

	switch (init.func) {
	case R128_INIT_CCE:
		return r128_do_init_cce(dev, &init);
	case R128_CLEANUP_CCE:
		return r128_do_cleanup_cce(dev);
	}

	return -EINVAL;
}

static int r128_do_engine_flush(drm_device_t *dev)
{
	u32 tmp;
	int i;

	tmp = R128_READ(R128_PC_NGUI_CTLSTAT) | R128_PC_FLUSH_ALL;
	R128_WRITE(R128_PC_NGUI_CTLSTAT, tmp);

	for (i = 0; i < R128_TIMEOUT; i++)
		if (!(R128_READ(R128_PC_NGUI_CTLSTAT) & R128_PC_BUSY))
			return 0;

	return -EBUSY;
}

int r128_do_engine_reset(drm_device_t *dev)
{
	drm_r128_private_t *dev_priv = (drm_r128_private_t *)dev->dev_private;
	u32                 clock_cntl_index, mclk_cntl, gen_reset_cntl;
	int                 ret;

	ret = r128_do_engine_flush(dev);

	clock_cntl_index = R128_READ(R128_CLOCK_CNTL_INDEX);
	mclk_cntl        = R128_READ_PLL(dev, R128_MCLK_CNTL);

	R128_WRITE_PLL(R128_MCLK_CNTL,
		       mclk_cntl | R128_FORCE_GCP | R128_FORCE_PIPE3D_CPP);

	gen_reset_cntl   = R128_READ(R128_GEN_RESET_CNTL);

	R128_WRITE(R128_GEN_RESET_CNTL, gen_reset_cntl | R128_SOFT_RESET_GUI);
	(void)R128_READ(R128_GEN_RESET_CNTL);
	R128_WRITE(R128_GEN_RESET_CNTL, gen_reset_cntl & ~R128_SOFT_RESET_GUI);
	(void)R128_READ(R128_GEN_RESET_CNTL);

	R128_WRITE_PLL(R128_MCLK_CNTL,    mclk_cntl);
	R128_WRITE(R128_CLOCK_CNTL_INDEX, clock_cntl_index);
	R128_WRITE(R128_GEN_RESET_CNTL,   gen_reset_cntl);

	/* For CCE ring buffer only */
	if (dev_priv->cce_is_bm_mode) {
		R128_WRITE(R128_PM4_BUFFER_DL_WPTR, 0);
		R128_WRITE(R128_PM4_BUFFER_DL_RPTR, 0);
		*dev_priv->ring_read_ptr = 0;
		dev_priv->sarea_priv->ring_write = 0;
	}

	return 0;
}

int r128_engine_reset(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
        drm_file_t        *priv   = filp->private_data;
        drm_device_t      *dev    = priv->dev;

	return r128_do_engine_reset(dev);
}

static int r128_do_wait_for_fifo(drm_device_t *dev, int entries)
{
	int i;

	for (i = 0; i < R128_TIMEOUT; i++) {
		int slots = R128_READ(R128_PM4_STAT) & R128_PM4_FIFOCNT_MASK;
		if (slots >= entries) return 0;
	}
	(void)r128_do_engine_reset(dev);
	return -EBUSY;
}

int r128_do_wait_for_idle(drm_device_t *dev)
{
	drm_r128_private_t *dev_priv = (drm_r128_private_t *)dev->dev_private;
	int                 i;

	if (dev_priv->cce_is_bm_mode) {
		u32 tmp;
		tmp = R128_READ(R128_PM4_BUFFER_DL_WPTR);
		R128_WRITE(R128_PM4_BUFFER_DL_WPTR,
			   tmp | R128_PM4_BUFFER_DL_DONE);

		for (i = 0; i < R128_TIMEOUT; i++) {
			if (*dev_priv->ring_read_ptr == dev_priv->sarea_priv->ring_write) {
				int pm4stat = R128_READ(R128_PM4_STAT);
				if ((pm4stat & R128_PM4_FIFOCNT_MASK) == dev_priv->cce_fifo_size &&
				    !(pm4stat & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE)))
					return 0;
			}
		}
		(void)r128_do_engine_reset(dev);
		return -EBUSY;
	} else {
		int ret = r128_do_wait_for_fifo(dev, dev_priv->cce_fifo_size);
		if (ret < 0) return ret;

		for (i = 0; i < R128_TIMEOUT; i++) {
			int pm4stat = R128_READ(R128_PM4_STAT);
			if (!(pm4stat & (R128_PM4_BUSY | R128_PM4_GUI_ACTIVE))) {
				return r128_do_engine_flush(dev);
			}
		}
		(void)r128_do_engine_reset(dev);
		return -EBUSY;
	}
}

int r128_wait_for_idle(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
        drm_file_t         *priv     = filp->private_data;
        drm_device_t       *dev      = priv->dev;

	return r128_do_wait_for_idle(dev);
}

static int r128_submit_packets_ring_secure(drm_device_t *dev,
					   drm_r128_packet_t *packet)
{
	drm_r128_private_t *dev_priv  = (drm_r128_private_t *)dev->dev_private;
	int                 write     = dev_priv->sarea_priv->ring_write;
	int                *write_ptr = dev_priv->ring_start + write;
	int                 count     = packet->count;
	int                 ret       = 0;
	int                 timeout;
	u32                *buffer, *commands;
	int                 size;
	u32                 tmp       = 0;
	int                 psize     = 0;
	int                 writing   = 1;

	if (count >= dev_priv->ring_entries) {
		count = dev_priv->ring_entries-1;
		ret = packet->count - count;
	}

	size = count * sizeof(u32);
	if ((commands = buffer = kmalloc(size, 0)) == NULL) return -ENOMEM;
	copy_from_user_ret(buffer, packet->buffer, size, -EFAULT);

	while (count > 0) {
		tmp = *commands++;
		if (!psize) {
			writing = 1;

			if ((tmp & R128_CCE_PACKET_MASK) == R128_CCE_PACKET0) {
				if ((tmp & R128_CCE_PACKET0_REG_MASK) <= (0x1004 >> 2)) {
					if ((tmp & R128_CCE_PACKET0_REG_MASK) !=
					    (R128_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				}
				psize = ((tmp & R128_CCE_PACKET_COUNT_MASK) >> 16) + 2;
			} else if ((tmp & R128_CCE_PACKET_MASK) == R128_CCE_PACKET1) {
				if ((tmp & R128_CCE_PACKET1_REG0_MASK) <= (0x1004 >> 2)) {
					if ((tmp & R128_CCE_PACKET1_REG0_MASK) !=
					    (R128_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				} else if ((tmp & R128_CCE_PACKET1_REG1_MASK) <=
					   (0x1004 << 9)) {
					if ((tmp & R128_CCE_PACKET1_REG1_MASK) !=
					    (R128_PM4_VC_FPU_SETUP << 9)) {
						writing = 0;
					}
				}
				psize = 3;
			} else {
				psize = ((tmp & R128_CCE_PACKET_COUNT_MASK) >> 16) + 2;
			}
		}
		psize--;

		if (writing) {
			write++;
			*write_ptr++ = tmp;
		}
		if (write >= dev_priv->ring_entries) {
			write = 0;
			write_ptr = dev_priv->ring_start;
		}
		timeout = 0;
		while (write == *dev_priv->ring_read_ptr) {
			(void)R128_READ(R128_PM4_BUFFER_DL_RPTR);
			if (timeout++ >= R128_TIMEOUT) {
				kfree(buffer);
				return r128_do_engine_reset(dev);
			}
		}
		count--;
	}

	if (write < 32) {
		memcpy(dev_priv->ring_end,
		       dev_priv->ring_start,
		       write * sizeof(u32));
	}

	dev_priv->sarea_priv->ring_write = write;
	R128_WRITE(R128_PM4_BUFFER_DL_WPTR, write);

	kfree(buffer);

	return ret;
}

static int r128_submit_packets_pio_secure(drm_device_t *dev,
					  drm_r128_packet_t *packet)
{
	u32 *buffer, *commands;
	int  count   = packet->count;
	int  size;
	int  ret;
	u32  tmp     = 0;
	int  psize   = 0;
	int  writing = 1;
	int  addr    = R128_PM4_FIFO_DATA_EVEN;

	size = count * sizeof(u32);
	if ((commands = buffer = kmalloc(size, 0)) == NULL) return -ENOMEM;
	copy_from_user_ret(buffer, packet->buffer, size, -EFAULT);

	while (count > 0) {
		tmp = *commands++;
		if (!psize) {
			writing = 1;

			if ((tmp & R128_CCE_PACKET_MASK) == R128_CCE_PACKET0) {
				if ((tmp & R128_CCE_PACKET0_REG_MASK) <= (0x1004 >> 2)) {
					if ((tmp & R128_CCE_PACKET0_REG_MASK) !=
					    (R128_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				}
				psize = ((tmp & R128_CCE_PACKET_COUNT_MASK) >> 16) + 2;
			} else if ((tmp & R128_CCE_PACKET_MASK) == R128_CCE_PACKET1) {
				if ((tmp & R128_CCE_PACKET1_REG0_MASK) <= (0x1004 >> 2)) {
					if ((tmp & R128_CCE_PACKET1_REG0_MASK) !=
					    (R128_PM4_VC_FPU_SETUP >> 2)) {
						writing = 0;
					}
				} else if ((tmp & R128_CCE_PACKET1_REG1_MASK) <=
					   (0x1004 << 9)) {
					if ((tmp & R128_CCE_PACKET1_REG1_MASK) !=
					    (R128_PM4_VC_FPU_SETUP << 9)) {
						writing = 0;
					}
				}
				psize = 3;
			} else {
				psize = ((tmp & R128_CCE_PACKET_COUNT_MASK) >> 16) + 2;
			}
		}
		psize--;

		if (writing) {
			if ((ret = r128_do_wait_for_fifo(dev, 1)) < 0) {
				kfree(buffer);
				return ret;
			}
			R128_WRITE(addr, tmp);
			addr ^= 0x0004;
		}

		count--;
	}

	if (addr == R128_PM4_FIFO_DATA_ODD) {
		if ((ret = r128_do_wait_for_fifo(dev, 1)) < 0) {
			kfree(buffer);
			return ret;
		}
		R128_WRITE(addr, R128_CCE_PACKET2);
	}

	kfree(buffer);

	return 0;
}

static int r128_submit_packets_ring(drm_device_t *dev,
				    drm_r128_packet_t *packet)
{
	drm_r128_private_t *dev_priv  = (drm_r128_private_t *)dev->dev_private;
	int                 write     = dev_priv->sarea_priv->ring_write;
	int                *write_ptr = dev_priv->ring_start + write;
	int                 count     = packet->count;
	int                 ret       = 0;
	int                 timeout;
	u32                *buffer, *commands;
	int                 size;

	if (count >= dev_priv->ring_entries) {
		count = dev_priv->ring_entries-1;
		ret = packet->count - count;
	}

	size = count * sizeof(u32);
	if ((commands = buffer = kmalloc(size, 0)) == NULL) return -ENOMEM;
	copy_from_user_ret(buffer, packet->buffer, size, -EFAULT);

	while (count > 0) {
		write++;
		*write_ptr++ = *commands++;
		if (write >= dev_priv->ring_entries) {
			write = 0;
			write_ptr = dev_priv->ring_start;
		}
		timeout = 0;
		while (write == *dev_priv->ring_read_ptr) {
			(void)R128_READ(R128_PM4_BUFFER_DL_RPTR);
			if (timeout++ >= R128_TIMEOUT) {
				kfree(buffer);
				return r128_do_engine_reset(dev);
			}
		}
		count--;
	}

	if (write < 32) {
		memcpy(dev_priv->ring_end,
		       dev_priv->ring_start,
		       write * sizeof(u32));
	}

	dev_priv->sarea_priv->ring_write = write;
	R128_WRITE(R128_PM4_BUFFER_DL_WPTR, write);

	kfree(buffer);

	return ret;
}

static int r128_submit_packets_pio(drm_device_t *dev,
				   drm_r128_packet_t *packet)
{
	u32 *buffer, *commands;
	int  count = packet->count;
	int  size;
	int  ret;

	size = count * sizeof(u32);
	if ((commands = buffer = kmalloc(size, 0)) == NULL) return -ENOMEM;
	copy_from_user_ret(buffer, packet->buffer, size, -EFAULT);

	while (count > 1) {
		if ((ret = r128_do_wait_for_fifo(dev, 2)) < 0) {
			kfree(buffer);
			return ret;
		}
		R128_WRITE(R128_PM4_FIFO_DATA_EVEN, *commands++);
		R128_WRITE(R128_PM4_FIFO_DATA_ODD,  *commands++);
		count -= 2;
	}

	if (count) {
		if ((ret = r128_do_wait_for_fifo(dev, 2)) < 0) {
			kfree(buffer);
			return ret;
		}
		R128_WRITE(R128_PM4_FIFO_DATA_EVEN, *commands++);
		R128_WRITE(R128_PM4_FIFO_DATA_ODD,  R128_CCE_PACKET2);
	}

	kfree(buffer);

	return 0;
}

int r128_submit_packets(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
        drm_file_t         *priv     = filp->private_data;
        drm_device_t       *dev      = priv->dev;
	drm_r128_private_t *dev_priv = (drm_r128_private_t *)dev->dev_private;
	drm_r128_packet_t   packet;

	copy_from_user_ret(&packet, (drm_r128_packet_t *)arg, sizeof(packet),
			   -EFAULT);

	if (dev_priv->cce_secure) {
		if (dev_priv->cce_is_bm_mode)
			return r128_submit_packets_ring_secure(dev, &packet);
		else
			return r128_submit_packets_pio_secure(dev, &packet);
	} else {
		if (dev_priv->cce_is_bm_mode)
			return r128_submit_packets_ring(dev, &packet);
		else
			return r128_submit_packets_pio(dev, &packet);
	}
}
