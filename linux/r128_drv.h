/* r128_drv.h -- Private header for r128 driver -*- linux-c -*-
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

#ifndef _R128_DRV_H_
#define _R128_DRV_H_

typedef struct drm_r128_freelist {
   	unsigned int	age;
   	drm_buf_t	*buf;
   	struct drm_r128_freelist *next;
   	struct drm_r128_freelist *prev;
} drm_r128_freelist_t;

typedef struct drm_r128_ring_buffer {
	__volatile__ u32 *read_ptr;

	u32	start;
	u32	end;
	int	size;
	int	size_l2qw;
	u32	*virtual_start;

	int	head;
	int	tail;
	u32	tail_mask;
	int	space;
} drm_r128_ring_buffer_t;

typedef struct drm_r128_private {
	__volatile__ u32 *ring_read_ptr;
	drm_r128_sarea_t *sarea_priv;

	u32	*ring_start;
	u32	*ring_end;
	int	ring_size;
	int	ring_sizel2qw;
	int	ring_entries;

	int	cce_mode;
	int	cce_fifo_size;
	int	cce_is_bm_mode;
	int	cce_secure;
	u32	*cce_buffer;

   	drm_r128_freelist_t *head;
   	drm_r128_freelist_t *tail;

	unsigned int	submit_age;

	int	usec_timeout;
	int	is_pci;

	drm_map_t	*sarea;
	drm_map_t	*fb;
	drm_map_t	*agp_ring;
	drm_map_t	*agp_read_ptr;
	drm_map_t	*agp_vertbufs;
	drm_map_t	*agp_indbufs;
	drm_map_t	*agp_textures;
	drm_map_t	*mmio;
} drm_r128_private_t;

typedef struct drm_r128_buf_priv {
	u32	age;
	int	discard;
	int	dispatched;
   	drm_r128_freelist_t *my_freelist;
} drm_r128_buf_priv_t;

				/* r128_drv.c */
extern int  r128_version(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  r128_open(struct inode *inode, struct file *filp);
extern int  r128_release(struct inode *inode, struct file *filp);
extern int  r128_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  r128_lock(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);
extern int  r128_unlock(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

				/* r128_dma.c */
extern int r128_init_cce(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);
extern int r128_eng_reset(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int r128_eng_flush(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int r128_cce_packet(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int r128_cce_idle(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

extern void r128_flush_write_combine( void );
extern int r128_do_wait_for_idle( drm_r128_private_t *dev_priv );
extern int r128_do_submit_packet( drm_r128_private_t *dev_priv,
				  u32 *buffer, int count );
extern drm_buf_t *r128_freelist_get( drm_device_t *dev );

				/* r128_state.c */
extern int r128_cce_vertex(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);

				/* r128_bufs.c */
extern int r128_addbufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int r128_mapbufs(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);

				/* r128_context.c */
extern int  r128_resctx(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  r128_addctx(struct inode *inode, struct file *filp,
		        unsigned int cmd, unsigned long arg);
extern int  r128_modctx(struct inode *inode, struct file *filp,
		        unsigned int cmd, unsigned long arg);
extern int  r128_getctx(struct inode *inode, struct file *filp,
		        unsigned int cmd, unsigned long arg);
extern int  r128_switchctx(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int  r128_newctx(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  r128_rmctx(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);

extern int  r128_context_switch(drm_device_t *dev, int old, int new);
extern int  r128_context_switch_complete(drm_device_t *dev, int new);


/* Register definitions, register access macros and drmAddMap constants
 * for Rage 128 kernel driver.
 */

#define R128_AUX_SC_CNTL		0x1660
#	define R128_AUX1_SC_EN			(1 << 0)
#	define R128_AUX1_SC_MODE_OR		(0 << 1)
#	define R128_AUX1_SC_MODE_NAND		(1 << 1)
#	define R128_AUX2_SC_EN			(1 << 2)
#	define R128_AUX2_SC_MODE_OR		(0 << 3)
#	define R128_AUX2_SC_MODE_NAND		(1 << 3)
#	define R128_AUX3_SC_EN			(1 << 4)
#	define R128_AUX3_SC_MODE_OR		(0 << 5)
#	define R128_AUX3_SC_MODE_NAND		(1 << 5)
#define R128_AUX1_SC_LEFT		0x1664
#define R128_AUX1_SC_RIGHT		0x1668
#define R128_AUX1_SC_TOP		0x166c
#define R128_AUX1_SC_BOTTOM		0x1670
#define R128_AUX2_SC_LEFT		0x1674
#define R128_AUX2_SC_RIGHT		0x1678
#define R128_AUX2_SC_TOP		0x167c
#define R128_AUX2_SC_BOTTOM		0x1680
#define R128_AUX3_SC_LEFT		0x1684
#define R128_AUX3_SC_RIGHT		0x1688
#define R128_AUX3_SC_TOP		0x168c
#define R128_AUX3_SC_BOTTOM		0x1690

#define R128_CLOCK_CNTL_INDEX		0x0008
#define R128_CLOCK_CNTL_DATA		0x000c
#	define R128_PLL_WR_EN			(1 << 7)

#define R128_CONSTANT_COLOR_C		0x1d34

#define R128_DP_WRITE_MASK		0x16cc
#define R128_DST_PITCH_OFFSET_C		0x1c80

#define R128_GEN_RESET_CNTL		0x00f0
#	define R128_SOFT_RESET_GUI		(1 <<  0)

#define R128_GUI_SCRATCH_REG0		0x15e0
#define R128_GUI_SCRATCH_REG1		0x15e4
#define R128_GUI_SCRATCH_REG2		0x15e8
#define R128_GUI_SCRATCH_REG3		0x15ec
#define R128_GUI_SCRATCH_REG4		0x15f0
#define R128_GUI_SCRATCH_REG5		0x15f4

#define R128_GUI_STAT			0x1740
#	define R128_GUI_FIFOCNT_MASK		0x0fff
#	define R128_GUI_ACTIVE			(1 << 31)

#define R128_MCLK_CNTL			0x000f
#	define R128_FORCE_GCP			(1 << 16)
#	define R128_FORCE_PIPE3D_CP		(1 << 17)
#	define R128_FORCE_RCP			(1 << 18)

#define R128_PC_NGUI_CTLSTAT		0x0184
#	define R128_PC_FLUSH_ALL		0x00ff
#	define R128_PC_BUSY			(1 << 31)

#define R128_PM4_BUFFER_CNTL		0x0704
#	define R128_PM4_NONPM4			(0  << 28)
#	define R128_PM4_192PIO			(1  << 28)
#	define R128_PM4_192BM			(2  << 28)
#	define R128_PM4_128PIO_64INDBM		(3  << 28)
#	define R128_PM4_128BM_64INDBM		(4  << 28)
#	define R128_PM4_64PIO_128INDBM		(5  << 28)
#	define R128_PM4_64BM_128INDBM		(6  << 28)
#	define R128_PM4_64PIO_64VCBM_64INDBM	(7  << 28)
#	define R128_PM4_64BM_64VCBM_64INDBM	(8  << 28)
#	define R128_PM4_64PIO_64VCPIO_64INDPIO	(15 << 28)

#define R128_PM4_BUFFER_DL_RPTR		0x0710
#define R128_PM4_BUFFER_DL_WPTR		0x0714
#	define R128_PM4_BUFFER_DL_DONE		(1 << 31)

#define R128_PM4_VC_FPU_SETUP		0x071c

#define R128_PM4_STAT			0x07b8
#	define R128_PM4_FIFOCNT_MASK		0x0fff
#	define R128_PM4_BUSY			(1 << 16)
#	define R128_PM4_GUI_ACTIVE		(1 << 31)

#define R128_PM4_BUFFER_ADDR		0x07f0
#define R128_PM4_MICRO_CNTL		0x07fc
#	define R128_PM4_MICRO_FREERUN		(1 << 30)

#define R128_PM4_FIFO_DATA_EVEN		0x1000
#define R128_PM4_FIFO_DATA_ODD		0x1004

#define R128_PRIM_TEX_CNTL_C		0x1cb0

#define R128_SCALE_3D_CNTL		0x1a00
#define R128_SEC_TEX_CNTL_C		0x1d00
#define R128_SEC_TEXTURE_BORDER_COLOR_C	0x1d3c
#define R128_SETUP_CNTL			0x1bc4
#define R128_STEN_REF_MASK_C		0x1d40

#define R128_TEX_CNTL_C			0x1c9c
#	define R128_TEX_CACHE_FLUSH		(1 << 23)

#define R128_WINDOW_XY_OFFSET		0x1bcc


/* CCE command packets
 */
#define R128_CCE_PACKET0		0x00000000
#define R128_CCE_PACKET1		0x40000000
#define R128_CCE_PACKET2		0x80000000
#define R128_CCE_PACKET3		0xC0000000
#	define R128_3D_RNDR_GEN_INDX_PRIM	0x00002300

#define R128_CCE_PACKET_MASK		0xC0000000
#define R128_CCE_PACKET_COUNT_MASK	0x3fff0000
#define R128_CCE_PACKET0_REG_MASK	0x000007ff
#define R128_CCE_PACKET1_REG0_MASK	0x000007ff
#define R128_CCE_PACKET1_REG1_MASK	0x003ff800

#define R128_CCE_VC_CNTL_PRIM_TYPE_NONE		0x00000000
#define R128_CCE_VC_CNTL_PRIM_TYPE_POINT	0x00000001
#define R128_CCE_VC_CNTL_PRIM_TYPE_LINE		0x00000002
#define R128_CCE_VC_CNTL_PRIM_TYPE_POLY_LINE	0x00000003
#define R128_CCE_VC_CNTL_PRIM_TYPE_TRI_LIST	0x00000004
#define R128_CCE_VC_CNTL_PRIM_TYPE_TRI_FAN	0x00000005
#define R128_CCE_VC_CNTL_PRIM_TYPE_TRI_STRIP	0x00000006
#define R128_CCE_VC_CNTL_PRIM_TYPE_TRI_TYPE2	0x00000007
#define R128_CCE_VC_CNTL_PRIM_WALK_IND		0x00000010
#define R128_CCE_VC_CNTL_PRIM_WALK_LIST		0x00000020
#define R128_CCE_VC_CNTL_PRIM_WALK_RING		0x00000030
#define R128_CCE_VC_CNTL_NUM_SHIFT		16


#define R128_MAX_USEC_TIMEOUT	100000	/* 100 ms */

#define R128_MAX_VB_AGE		0xffffffff
#define R128_VB_AGE_REG		R128_GUI_SCRATCH_REG0


#define R128_BASE(reg)		((u32)(dev_priv->mmio->handle))
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

extern int R128_READ_PLL(drm_device_t *dev, int addr);

#define R128CCE0(p,r,n)   ((p) | ((n) << 16) | ((r) >> 2))
#define R128CCE1(p,r1,r2) ((p) | (((r2) >> 2) << 11) | ((r1) >> 2))
#define R128CCE2(p)       ((p))
#define R128CCE3(p,n)     ((p) | ((n) << 16))




#define CCE_PACKET0( reg, n )		(R128_CCE_PACKET0 |		\
					 ((n) << 16) | ((reg) >> 2))
#define CCE_PACKET1( reg0, reg1 )	(R128_CCE_PACKET1 |		\
					 (((reg1) >> 2) << 11) | ((reg0) >> 2))
#define CCE_PACKET2()			(R128_CCE_PACKET2)
#define CCE_PACKET3( pkt, n )		(R128_CCE_PACKET3 |		\
					 (pkt) | ((n) << 16))


#define R128_VERBOSE	0

#if 0

#define RING_LOCALS	unsigned long outring, ringmask; volatile u32 *virt;

#define BEGIN_RING( n ) do {						\
	if ( R128_VERBOSE ) {						\
		DRM_DEBUG( "BEGIN_RING( %d ) in %s\n",			\
			   n, __FUNCTION__ );				\
	}								\
	if ( dev_priv->ring.space < n * sizeof(u32) ) {			\
		r128_wait_ring( dev_priv, n * sizeof(u32) );		\
	}								\
	dev_priv->ring.space -= n * sizeof(u32);			\
	outring = dev_priv->ring.tail;					\
	ringmask = dev_priv->ring.tail_mask;				\
	virt = dev_priv->->ring.virtual_start;				\
} while (0)

#define ADVANCE_RING() do {						\
	if ( R128_VERBOSE ) {						\
		DRM_DEBUG( "ADVANCE_RING()\n" );			\
	}								\
	dev_priv->sarea_priv->ring_write = write;
	R128_WRITE( R128_PM4_BUFFER_DL_WPTR, write );
	__ret = r128_do_submit_packet( dev_priv, buffer, outring );	\
	if ( __ret < 0 ) {						\
		DRM_ERROR( "ADVANCE_RING fucked up!\n" );		\
	}								\
} while (0)

#define OUT_RING( x ) do {						\
	if ( R128_VERBOSE ) {						\
		DRM_DEBUG( "   OUT_RING( 0x%08x )\n",			\
			   (unsigned int)(x) );				\
	}								\
	buffer[outring++] = x;						\
} while (0)

#else

#define RING_LOCALS	unsigned long outring; u32 *buffer; int __ret;

#define BEGIN_RING( n ) do {						\
	if ( R128_VERBOSE ) {						\
		DRM_DEBUG( "BEGIN_RING( %d ) in %s\n",			\
			   n, __FUNCTION__ );				\
	}								\
	outring = 0;							\
	buffer = dev_priv->cce_buffer;					\
} while (0)

#define ADVANCE_RING() do {						\
	if ( R128_VERBOSE ) {						\
		DRM_DEBUG( "ADVANCE_RING()\n" );			\
	}								\
	__ret = r128_do_submit_packet( dev_priv, buffer, outring );	\
	if ( __ret < 0 ) {						\
		DRM_ERROR( "ADVANCE_RING fucked up!\n" );		\
	}								\
} while (0)

#define OUT_RING( x ) do {						\
	if ( R128_VERBOSE ) {						\
		DRM_DEBUG( "   OUT_RING( 0x%08x )\n",			\
			   (unsigned int)(x) );				\
	}								\
	buffer[outring++] = x;						\
} while (0)

#endif

#endif
