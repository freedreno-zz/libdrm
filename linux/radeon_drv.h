/* radeon_drv.h -- Private header for radeon driver -*- linux-c -*-
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
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
 * Authors:
 *   Rickard E. (Rik) Faith <faith@valinux.com>
 *   Kevin E. Martin <martin@valinux.com>
 *   Gareth Hughes <gareth@valinux.com>
 *
 */

#ifndef __RADEON_DRV_H__
#define __RADEON_DRV_H__

typedef struct drm_radeon_freelist {
   	unsigned int age;
   	drm_buf_t *buf;
   	struct drm_radeon_freelist *next;
   	struct drm_radeon_freelist *prev;
} drm_radeon_freelist_t;

typedef struct drm_radeon_ring_buffer {
	u32 *start;
	u32 *end;
	int size;
	int size_l2qw;

	volatile u32 *head;
	u32 tail;
	u32 tail_mask;
	int space;
} drm_radeon_ring_buffer_t;

typedef struct drm_radeon_private {
	drm_radeon_ring_buffer_t ring;
	drm_radeon_sarea_t *sarea_priv;

	int agp_size;

	int cp_mode;
	int cp_secure;
	int cp_running;

   	drm_radeon_freelist_t *head;
   	drm_radeon_freelist_t *tail;

	int usec_timeout;
	int is_pci;

	atomic_t idle_count;

	unsigned int fb_bpp;
	unsigned int front_offset;
	unsigned int front_pitch;
	unsigned int front_x;
	unsigned int front_y;
	unsigned int back_offset;
	unsigned int back_pitch;
	unsigned int back_x;
	unsigned int back_y;

	unsigned int depth_bpp;
	unsigned int depth_offset;
	unsigned int depth_pitch;
	unsigned int depth_x;
	unsigned int depth_y;

	drm_map_t *sarea;
	drm_map_t *fb;
	drm_map_t *mmio;
	drm_map_t *cp_ring;
	drm_map_t *ring_rptr;
	drm_map_t *buffers;
	drm_map_t *agp_textures;
} drm_radeon_private_t;

typedef struct drm_radeon_buf_priv {
	u32 age;
	int prim;
	int discard;
	int dispatched;
   	drm_radeon_freelist_t *list_entry;
} drm_radeon_buf_priv_t;

				/* radeon_drv.c */
extern int  radeon_version( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int  radeon_open( struct inode *inode, struct file *filp );
extern int  radeon_release( struct inode *inode, struct file *filp );
extern int  radeon_ioctl( struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg );
extern int  radeon_lock( struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg );
extern int  radeon_unlock( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );

				/* radeon_cp.c */
extern int radeon_cp_init( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int radeon_cp_start( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int radeon_cp_stop( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int radeon_cp_reset( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int radeon_cp_idle( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int radeon_engine_reset( struct inode *inode, struct file *filp,
				unsigned int cmd, unsigned long arg );
extern int radeon_cp_packet( struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg );
extern int radeon_cp_buffers( struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg );

extern void radeon_freelist_reset( drm_device_t *dev );
extern drm_buf_t *radeon_freelist_get( drm_device_t *dev );

extern int radeon_wait_ring( drm_radeon_private_t *dev_priv, int n );
extern void radeon_update_ring_snapshot( drm_radeon_private_t *dev_priv );

				/* radeon_state.c */
extern int radeon_cp_clear( struct inode *inode, struct file *filp,
			    unsigned int cmd, unsigned long arg );
extern int radeon_cp_swap( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );
extern int radeon_cp_vertex( struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg );
extern int radeon_cp_indices( struct inode *inode, struct file *filp,
			      unsigned int cmd, unsigned long arg );
extern int radeon_cp_blit( struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg );

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

#if 0
#define RADEON_AUX_SC_CNTL		0x1660
#	define RADEON_AUX1_SC_EN		(1 << 0)
#	define RADEON_AUX1_SC_MODE_OR		(0 << 1)
#	define RADEON_AUX1_SC_MODE_NAND		(1 << 1)
#	define RADEON_AUX2_SC_EN		(1 << 2)
#	define RADEON_AUX2_SC_MODE_OR		(0 << 3)
#	define RADEON_AUX2_SC_MODE_NAND		(1 << 3)
#	define RADEON_AUX3_SC_EN		(1 << 4)
#	define RADEON_AUX3_SC_MODE_OR		(0 << 5)
#	define RADEON_AUX3_SC_MODE_NAND		(1 << 5)
#define RADEON_AUX1_SC_LEFT		0x1664
#define RADEON_AUX1_SC_RIGHT		0x1668
#define RADEON_AUX1_SC_TOP		0x166c
#define RADEON_AUX1_SC_BOTTOM		0x1670
#define RADEON_AUX2_SC_LEFT		0x1674
#define RADEON_AUX2_SC_RIGHT		0x1678
#define RADEON_AUX2_SC_TOP		0x167c
#define RADEON_AUX2_SC_BOTTOM		0x1680
#define RADEON_AUX3_SC_LEFT		0x1684
#define RADEON_AUX3_SC_RIGHT		0x1688
#define RADEON_AUX3_SC_TOP		0x168c
#define RADEON_AUX3_SC_BOTTOM		0x1690
#endif

#define RADEON_BUS_CNTL			0x0030
#	define RADEON_BUS_MASTER_DIS		(1 << 6)

#define RADEON_CLOCK_CNTL_DATA		0x000c
#	define RADEON_PLL_WR_EN			(1 << 7)
#define RADEON_CLOCK_CNTL_INDEX		0x0008

#define RADEON_CONFIG_APER_SIZE		0x0108
#if 0
#define RADEON_CONSTANT_COLOR_C		0x1d34
#endif

#if 0
#define RADEON_DP_GUI_MASTER_CNTL	0x146c
#       define RADEON_GMC_SRC_PITCH_OFFSET_CNTL	(1    <<  0)
#       define RADEON_GMC_DST_PITCH_OFFSET_CNTL	(1    <<  1)
#	define RADEON_GMC_BRUSH_SOLID_COLOR	(13   <<  4)
#	define RADEON_GMC_BRUSH_NONE		(15   <<  4)
#	define RADEON_GMC_DST_16BPP		(4    <<  8)
#	define RADEON_GMC_DST_24BPP		(5    <<  8)
#	define RADEON_GMC_DST_32BPP		(6    <<  8)
#       define RADEON_GMC_DST_DATATYPE_SHIFT	8
#	define RADEON_GMC_SRC_DATATYPE_COLOR	(3    << 12)
#	define RADEON_DP_SRC_SOURCE_MEMORY	(2    << 24)
#	define RADEON_DP_SRC_SOURCE_HOST_DATA	(3    << 24)
#	define RADEON_GMC_CLR_CMP_CNTL_DIS	(1    << 28)
#	define RADEON_GMC_AUX_CLIP_DIS		(1    << 29)
#	define RADEON_GMC_WR_MSK_DIS		(1    << 30)
#	define RADEON_ROP3_S			0x00cc0000
#	define RADEON_ROP3_P			0x00f00000
#define RADEON_DP_WRITE_MASK		0x16cc
#define RADEON_DST_PITCH_OFFSET_C	0x1c80
#endif

#if 0
#define RADEON_GEN_RESET_CNTL		0x00f0
#	define RADEON_SOFT_RESET_GUI		(1 <<  0)
#endif

#define RADEON_GUI_SCRATCH_REG0		0x15e0
#define RADEON_GUI_SCRATCH_REG1		0x15e4
#define RADEON_GUI_SCRATCH_REG2		0x15e8
#define RADEON_GUI_SCRATCH_REG3		0x15ec
#define RADEON_GUI_SCRATCH_REG4		0x15f0
#define RADEON_GUI_SCRATCH_REG5		0x15f4

#if 0
#define RADEON_GUI_STAT			0x1740
#	define RADEON_GUI_FIFOCNT_MASK		0x0fff
#	define RADEON_GUI_ACTIVE		(1 << 31)
#endif

#define RADEON_MC_AGP_LOCATION		0x014c
#define RADEON_MC_FB_LOCATION		0x0148
#define RADEON_MCLK_CNTL		0x0012

#if 0
#define RADEON_PC_GUI_CTLSTAT		0x1748
#define RADEON_PC_NGUI_CTLSTAT		0x0184
#	define RADEON_PC_FLUSH_GUI		(3 << 0)
#	define RADEON_PC_RI_GUI			(1 << 2)
#	define RADEON_PC_FLUSH_ALL		0x00ff
#	define RADEON_PC_BUSY			(1 << 31)
#define RADEON_PRIM_TEX_CNTL_C		0x1cb0
#endif

#define RADEON_RB2D_DSTCACHE_CTLSTAT	0x342c
#	define RADEON_RB2D_DC_FLUSH_ALL		0xf
#	define RADEON_RB2D_DC_BUSY		(1 << 31)
#define RADEON_RBBM_SOFT_RESET		0x00f0
#	define RADEON_SOFT_RESET_CP		(1 <<  0)
#	define RADEON_SOFT_RESET_HI		(1 <<  1)
#	define RADEON_SOFT_RESET_SE		(1 <<  2)
#	define RADEON_SOFT_RESET_RE		(1 <<  3)
#	define RADEON_SOFT_RESET_PP		(1 <<  4)
#	define RADEON_SOFT_RESET_E2		(1 <<  5)
#	define RADEON_SOFT_RESET_RB		(1 <<  6)
#	define RADEON_SOFT_RESET_HDP		(1 <<  7)
#define RADEON_RBBM_STATUS		0x0e40
#	define RADEON_RBBM_FIFOCNT_MASK		0x007f
#	define RADEON_RBBM_ACTIVE		(1 << 31)

#if 0
#define RADEON_SCALE_3D_CNTL		0x1a00
#define RADEON_SEC_TEX_CNTL_C		0x1d00
#define RADEON_SEC_TEXTURE_BORDER_COLOR_C 0x1d3c
#define RADEON_SETUP_CNTL		0x1bc4
#define RADEON_STEN_REF_MASK_C		0x1d40
#endif

#if 0
#define RADEON_TEX_CNTL_C		0x1c9c
#	define RADEON_TEX_CACHE_FLUSH		(1 << 23)
#endif

#if 0
#define RADEON_WINDOW_XY_OFFSET		0x1bcc
#endif


/* CP registers */
#define RADEON_CP_ME_RAM_ADDR		0x07d4
#define RADEON_CP_ME_RAM_RADDR		0x07d8
#define RADEON_CP_ME_RAM_DATAH		0x07dc
#define RADEON_CP_ME_RAM_DATAL		0x07e0

#define RADEON_CP_RB_BASE		0x0700
#define RADEON_CP_RB_CNTL		0x0704
#define RADEON_CP_RB_RPTR_ADDR		0x070c
#define RADEON_CP_RB_RPTR		0x0710
#define RADEON_CP_RB_WPTR		0x0714

#define RADEON_CP_RB_WPTR_DELAY		0x0718
#	define RADEON_PRE_WRITE_TIMER_SHIFT	0
#	define RADEON_PRE_WRITE_LIMIT_SHIFT	23

#define RADEON_CP_CSQ_CNTL		0x0740
#	define RADEON_CSQ_CNT_PRIMARY_MASK	(0xff << 0)
#	define RADEON_CSQ_PRIDIS_INDDIS		(0    << 28)
#	define RADEON_CSQ_PRIPIO_INDDIS		(1    << 28)
#	define RADEON_CSQ_PRIBM_INDDIS		(2    << 28)
#	define RADEON_CSQ_PRIPIO_INDBM		(3    << 28)
#	define RADEON_CSQ_PRIBM_INDBM		(4    << 28)
#	define RADEON_CSQ_PRIPIO_INDPIO		(15   << 28)

#define RADEON_AIC_CNTL			0x01d0
#	define RADEON_PCIGART_TRANSLATE_EN	(1 << 0)

#if 0
/* CP command packets */
#define RADEON_CP_PACKET0		0x00000000
#define RADEON_CP_PACKET1		0x40000000
#define RADEON_CP_PACKET2		0x80000000
#define RADEON_CP_PACKET3		0xC0000000
#	define RADEON_CNTL_HOSTDATA_BLT		0x00009400
#	define RADEON_CNTL_PAINT_MULTI		0x00009A00
#	define RADEON_CNTL_BITBLT_MULTI		0x00009B00
#	define RADEON_3D_RNDR_GEN_INDX_PRIM	0x00002300

#define RADEON_CP_PACKET_MASK		0xC0000000
#define RADEON_CP_PACKET_COUNT_MASK	0x3fff0000
#define RADEON_CP_PACKET0_REG_MASK	0x000007ff
#define RADEON_CP_PACKET1_REG0_MASK	0x000007ff
#define RADEON_CP_PACKET1_REG1_MASK	0x003ff800

#define RADEON_CP_VC_CNTL_PRIM_TYPE_NONE	0x00000000
#define RADEON_CP_VC_CNTL_PRIM_TYPE_POINT	0x00000001
#define RADEON_CP_VC_CNTL_PRIM_TYPE_LINE	0x00000002
#define RADEON_CP_VC_CNTL_PRIM_TYPE_POLY_LINE	0x00000003
#define RADEON_CP_VC_CNTL_PRIM_TYPE_TRI_LIST	0x00000004
#define RADEON_CP_VC_CNTL_PRIM_TYPE_TRI_FAN	0x00000005
#define RADEON_CP_VC_CNTL_PRIM_TYPE_TRI_STRIP	0x00000006
#define RADEON_CP_VC_CNTL_PRIM_TYPE_TRI_TYPE2	0x00000007
#define RADEON_CP_VC_CNTL_PRIM_WALK_IND		0x00000010
#define RADEON_CP_VC_CNTL_PRIM_WALK_LIST	0x00000020
#define RADEON_CP_VC_CNTL_PRIM_WALK_RING	0x00000030
#define RADEON_CP_VC_CNTL_NUM_SHIFT		16

#define RADEON_DATATYPE_CI8		2
#define RADEON_DATATYPE_ARGB1555	3
#define RADEON_DATATYPE_RGB565		4
#define RADEON_DATATYPE_RGB888		5
#define RADEON_DATATYPE_ARGB8888	6
#define RADEON_DATATYPE_RGB332		7
#define RADEON_DATATYPE_RGB8		9
#define RADEON_DATATYPE_ARGB4444	15
#endif

/* Constants */
#define RADEON_MAX_USEC_TIMEOUT	100000	/* 100 ms */

#define RADEON_LAST_FRAME_REG		RADEON_GUI_SCRATCH_REG0
#define RADEON_LAST_DISPATCH_REG	RADEON_GUI_SCRATCH_REG1
#define RADEON_MAX_VB_AGE		0xffffffff

#define RADEON_MAX_VB_VERTS		(0xffff)


#define RADEON_BASE(reg)	((u32)(dev_priv->mmio->handle))
#define RADEON_ADDR(reg)	(RADEON_BASE(reg) + reg)

#define RADEON_DEREF(reg)	*(__volatile__ u32 *)RADEON_ADDR(reg)
#define RADEON_READ(reg)	RADEON_DEREF(reg)
#define RADEON_WRITE(reg,val)	do { RADEON_DEREF(reg) = val; } while (0)

#define RADEON_DEREF8(reg)	*(__volatile__ u8 *)RADEON_ADDR(reg)
#define RADEON_READ8(reg)	RADEON_DEREF8(reg)
#define RADEON_WRITE8(reg,val)	do { RADEON_DEREF8(reg) = val; } while (0)

#define RADEON_WRITE_PLL(addr,val)                                            \
do {                                                                          \
	RADEON_WRITE8(RADEON_CLOCK_CNTL_INDEX,                                \
		      ((addr) & 0x1f) | RADEON_PLL_WR_EN);                    \
	RADEON_WRITE(RADEON_CLOCK_CNTL_DATA, (val));                          \
} while (0)

extern int RADEON_READ_PLL(drm_device_t *dev, int addr);



#define CP_PACKET0( reg, n )						\
	(RADEON_CP_PACKET0 | ((n) << 16) | ((reg) >> 2))
#define CP_PACKET1( reg0, reg1 )					\
	(RADEON_CP_PACKET1 | (((reg1) >> 2) << 11) | ((reg0) >> 2))
#define CP_PACKET2()							\
	(RADEON_CP_PACKET2)
#define CP_PACKET3( pkt, n )						\
	(RADEON_CP_PACKET3 | (pkt) | ((n) << 16))


#define radeon_flush_write_combine()	mb()


#define RADEON_VERBOSE	0

#define RING_LOCALS	int write; unsigned int mask; volatile u32 *ring;

#define BEGIN_RING( n ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "BEGIN_RING( %d ) in %s\n",			\
			   n, __FUNCTION__ );				\
	}								\
	if ( dev_priv->ring.space < n * sizeof(u32) ) {			\
		radeon_wait_ring( dev_priv, n * sizeof(u32) );		\
	}								\
	dev_priv->ring.space -= n * sizeof(u32);			\
	ring = dev_priv->ring.start;					\
	write = dev_priv->ring.tail;					\
	mask = dev_priv->ring.tail_mask;				\
} while (0)

#define ADVANCE_RING() do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "ADVANCE_RING() tail=0x%06x wr=0x%06x\n",	\
			  write, dev_priv->ring.tail );			\
	}								\
	radeon_flush_write_combine();					\
	dev_priv->ring.tail = write;					\
	RADEON_WRITE( RADEON_CP_RB_WPTR, write );			\
} while (0)

#define OUT_RING( x ) do {						\
	if ( RADEON_VERBOSE ) {						\
		DRM_INFO( "   OUT_RING( 0x%08x ) at 0x%x\n",		\
			   (unsigned int)(x), write );			\
	}								\
	ring[write++] = x;						\
	write &= mask;							\
} while (0)

#define RADEON_PERFORMANCE_BOXES	0

#endif /* __RADEON_DRV_H__ */
