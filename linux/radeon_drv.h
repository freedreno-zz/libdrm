/* radeon_drv.h -- Private header for radeon driver -*- linux-c -*-
 * Created: Mon Dec 13 09:51:11 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
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
 *          Kevin E. Martin <martin@valinux.com>
 *
 */

#ifndef _RADEON_DRV_H_
#define _RADEON_DRV_H_

typedef struct drm_radeon_private {
	int                is_pci;

	int                cp_mode;
	int                cp_fifo_size;
	int                cp_is_bm_mode;
	int                cp_secure;

	drm_radeon_sarea_t *sarea_priv;

	__volatile__ u32   *ring_read_ptr;

	u32                *ring_start;
	u32                *ring_end;
	int                ring_size;
	int                ring_sizel2qw;
	int                ring_entries;

	int                submit_age;

	int                usec_timeout;

	drm_map_t          *sarea;
	drm_map_t          *fb;
	drm_map_t          *agp_ring;
	drm_map_t          *agp_read_ptr;
	drm_map_t          *agp_vertbufs;
	drm_map_t          *agp_indbufs;
	drm_map_t          *agp_textures;
	drm_map_t          *mmio;
} drm_radeon_private_t;

typedef struct drm_radeon_buf_priv {
	u32               age;
} drm_radeon_buf_priv_t;

				/* radeon_drv.c */
extern int  radeon_version(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int  radeon_open(struct inode *inode, struct file *filp);
extern int  radeon_release(struct inode *inode, struct file *filp);
extern int  radeon_ioctl(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int  radeon_lock(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  radeon_unlock(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);

				/* radeon_dma.c */
extern int radeon_init_cp(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int radeon_eng_reset(struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg);
extern int radeon_eng_flush(struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg);
extern int radeon_submit_pkt(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern int radeon_cp_idle(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int radeon_vertex_buf(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg);

				/* radeon_bufs.c */
extern int radeon_addbufs(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int radeon_mapbufs(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);

				/* radeon_context.c */
extern int  radeon_resctx(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  radeon_addctx(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  radeon_modctx(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  radeon_getctx(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  radeon_switchctx(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg);
extern int  radeon_newctx(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  radeon_rmctx(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

extern int  radeon_context_switch(drm_device_t *dev, int old, int new);
extern int  radeon_context_switch_complete(drm_device_t *dev, int new);


/* Register definitions, register access macros and drmAddMap constants
 * for Radeon kernel driver.
 */

#define RADEON_PC_NGUI_CTLSTAT			0x0184
#       define RADEON_PC_FLUSH_ALL			0x00ff
#       define RADEON_PC_BUSY				(1 << 31)

#define RADEON_CLOCK_CNTL_INDEX			0x0008
#define RADEON_CLOCK_CNTL_DATA			0x000c
#       define RADEON_PLL_WR_EN				(1 << 7)

#define RADEON_MCLK_CNTL			0x000f
#       define RADEON_FORCE_GCP				(1 << 16)
#       define RADEON_FORCE_PIPE3D_CP			(1 << 17)
#       define RADEON_FORCE_RCP				(1 << 18)

#define RADEON_GEN_RESET_CNTL			0x00f0
#       define RADEON_SOFT_RESET_GUI			(1 <<  0)

#define RADEON_PM4_BUFFER_CNTL			0x0704
#       define RADEON_PM4_NONPM4			(0  << 28)
#       define RADEON_PM4_192PIO			(1  << 28)
#       define RADEON_PM4_192BM				(2  << 28)
#       define RADEON_PM4_128PIO_64INDBM		(3  << 28)
#       define RADEON_PM4_128BM_64INDBM			(4  << 28)
#       define RADEON_PM4_64PIO_128INDBM		(5  << 28)
#       define RADEON_PM4_64BM_128INDBM			(6  << 28)
#       define RADEON_PM4_64PIO_64VCBM_64INDBM		(7  << 28)
#       define RADEON_PM4_64BM_64VCBM_64INDBM		(8  << 28)
#       define RADEON_PM4_64PIO_64VCPIO_64INDPIO	(15 << 28)


#define RADEON_PM4_BUFFER_DL_RPTR		0x0710
#define RADEON_PM4_BUFFER_DL_WPTR		0x0714
#       define RADEON_PM4_BUFFER_DL_DONE		(1 << 31)

#define RADEON_PM4_VC_FPU_SETUP			0x071c

#define RADEON_PM4_STAT				0x07b8
#       define RADEON_PM4_FIFOCNT_MASK			0x0fff
#       define RADEON_PM4_BUSY				(1 << 16)
#       define RADEON_PM4_GUI_ACTIVE			(1 << 31)

#define RADEON_PM4_BUFFER_ADDR			0x07f0
#define RADEON_PM4_MICRO_CNTL			0x07fc
#       define RADEON_PM4_MICRO_FREERUN			(1 << 30)

#define RADEON_PM4_FIFO_DATA_EVEN		0x1000
#define RADEON_PM4_FIFO_DATA_ODD		0x1004

#define RADEON_GUI_SCRATCH_REG0			0x15e0
#define RADEON_GUI_SCRATCH_REG1			0x15e4
#define RADEON_GUI_SCRATCH_REG2			0x15e8
#define RADEON_GUI_SCRATCH_REG3			0x15ec
#define RADEON_GUI_SCRATCH_REG4			0x15f0
#define RADEON_GUI_SCRATCH_REG5			0x15f4

#define RADEON_GUI_STAT				0x1740
#       define RADEON_GUI_FIFOCNT_MASK			0x0fff
#       define RADEON_GUI_ACTIVE			(1 << 31)


/* CP command packets */
#define RADEON_CP_PACKET0			0x00000000
#define RADEON_CP_PACKET1			0x40000000
#define RADEON_CP_PACKET2			0x80000000
#       define RADEON_CP_PACKET_MASK			0xC0000000
#       define RADEON_CP_PACKET_COUNT_MASK		0x3fff0000
#       define RADEON_CP_PACKET0_REG_MASK		0x000007ff
#       define RADEON_CP_PACKET1_REG0_MASK		0x000007ff
#       define RADEON_CP_PACKET1_REG1_MASK		0x003ff800


#define RADEON_MAX_USEC_TIMEOUT	100000	/* 100 ms */


#define RADEON_BASE(reg)	((unsigned long)(dev_priv->mmio->handle))
#define RADEON_ADDR(reg)	(RADEON_BASE(reg) + reg)

#define RADEON_DEREF(reg)	*(__volatile__ int *)RADEON_ADDR(reg)
#define RADEON_READ(reg)	RADEON_DEREF(reg)
#define RADEON_WRITE(reg,val)	do { RADEON_DEREF(reg) = val; } while (0)

#define RADEON_DEREF8(reg)	*(__volatile__ char *)RADEON_ADDR(reg)
#define RADEON_READ8(reg)	RADEON_DEREF8(reg)
#define RADEON_WRITE8(reg,val)	do { RADEON_DEREF8(reg) = val; } while (0)

#define RADEON_WRITE_PLL(addr,val)                                            \
do {                                                                          \
	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX,                                \
		      ((addr) & 0x1f) | RADEON_PLL_WR_EN);                    \
	RADEON_WRITE(RADEON_CLOCK_CNTL_DATA, (val));                          \
} while (0)

extern int RADEON_READ_PLL(drm_device_t *dev, int addr);

#define RADEONCP0(p,r,n)   ((p) | ((n) << 16) | ((r) >> 2))
#define RADEONCP1(p,r1,r2) ((p) | (((r2) >> 2) << 11) | ((r1) >> 2))
#define RADEONCP2(p)       ((p))
#define RADEONCP3(p,n)     ((p) | ((n) << 16))

#endif
