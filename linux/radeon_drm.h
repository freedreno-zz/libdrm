/* radeon_drm.h -- Public header for the radeon driver -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

#ifndef _RADEON_DRM_H_
#define _RADEON_DRM_H_

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the X server file (radeon_sarea.h)
 */
#ifndef __RADEON_SAREA_DEFINES__
#define __RADEON_SAREA_DEFINES__

/* What needs to be changed for the current vertex buffer?
 */
#define RADEON_UPLOAD_CONTEXT		0x001
#define RADEON_UPLOAD_SETUP		0x002
#define RADEON_UPLOAD_TEX0		0x004
#define RADEON_UPLOAD_TEX1		0x008
#define RADEON_UPLOAD_TEX0IMAGES	0x010
#define RADEON_UPLOAD_TEX1IMAGES	0x020
#define RADEON_UPLOAD_CORE		0x040
#define RADEON_UPLOAD_MASKS		0x080
#define RADEON_UPLOAD_WINDOW		0x100
#define RADEON_UPLOAD_CLIPRECTS		0x200	/* handled client-side */
#define RADEON_REQUIRE_QUIESCENCE	0x400
#define RADEON_UPLOAD_ALL		0x7ff

#define RADEON_FRONT			0x1
#define RADEON_BACK			0x2
#define RADEON_DEPTH			0x4

/* Primitive types
 */
#define RADEON_POINTS			0x1
#define RADEON_LINES			0x2
#define RADEON_LINE_STRIP		0x3
#define RADEON_TRIANGLES		0x4
#define RADEON_TRIANGLE_FAN		0x5
#define RADEON_TRIANGLE_STRIP		0x6

/* Vertex/indirect buffer size
 */
#if 1
#define RADEON_BUFFER_SIZE		16384
#else
#define RADEON_BUFFER_SIZE		(128 * 1024)
#endif

/* Byte offsets for indirect buffer data
 */
#define RADEON_INDEX_PRIM_OFFSET	20
#define RADEON_HOSTDATA_BLIT_OFFSET	32

/* 2048x2048 @ 32bpp texture requires this many indirect buffers
 */
#define RADEON_MAX_BLIT_BUFFERS		((2048 * 2048 * 4)/RADEON_BUFFER_SIZE)

/* Keep these small for testing.
 */
#define RADEON_NR_SAREA_CLIPRECTS	12

/* There are 2 heaps (local/AGP).  Each region within a heap is a
 *  minimum of 64k, and there are at most 64 of them per heap.
 */
#define RADEON_LOCAL_TEX_HEAP		0
#define RADEON_AGP_TEX_HEAP		1
#define RADEON_NR_TEX_HEAPS		2
#define RADEON_NR_TEX_REGIONS		16
#define RADEON_LOG_TEX_GRANULARITY	16

#define RADEON_NR_CONTEXT_REGS		12
#define RADEON_TEX_MAXLEVELS		11

#endif /* __RADEON_SAREA_DEFINES__ */

typedef struct {
	/* Context state - can be written in one large chunk */
	unsigned int dst_pitch_offset_c;
	unsigned int dp_gui_master_cntl_c;
	unsigned int sc_top_left_c;
	unsigned int sc_bottom_right_c;
	unsigned int z_offset_c;
	unsigned int z_pitch_c;
	unsigned int z_sten_cntl_c;
	unsigned int tex_cntl_c;
	unsigned int misc_3d_state_cntl_reg;
	unsigned int texture_clr_cmp_clr_c;
	unsigned int texture_clr_cmp_msk_c;
	unsigned int fog_color_c;

	/* Texture state */
	unsigned int tex_size_pitch_c;
	unsigned int constant_color_c;

	/* Setup state */
	unsigned int pm4_vc_fpu_setup;
	unsigned int setup_cntl;

	/* Mask state */
	unsigned int dp_write_mask;
	unsigned int sten_ref_mask_c;
	unsigned int plane_3d_mask_c;

	/* Window state */
	unsigned int window_xy_offset;

	/* Core state */
	unsigned int scale_3d_cntl;
} drm_radeon_context_regs_t;

/* Setup registers for each texture unit */
typedef struct {
	unsigned int tex_cntl;
	unsigned int tex_combine_cntl;
	unsigned int tex_size_pitch;
	unsigned int tex_offset[RADEON_TEX_MAXLEVELS];
	unsigned int tex_border_color;
} drm_radeon_texture_regs_t;


typedef struct drm_radeon_tex_region {
	unsigned char next, prev;
	unsigned char in_use;
	int age;
} drm_radeon_tex_region_t;

typedef struct drm_radeon_sarea {
	/* The channel for communication of state information to the kernel
	 * on firing a vertex buffer.
	 */
	drm_radeon_context_regs_t context_state;
	drm_radeon_texture_regs_t tex_state[RADEON_NR_TEX_HEAPS];
	unsigned int dirty;
	unsigned int vertsize;
	unsigned int vc_format;

	/* The current cliprects, or a subset thereof.
	 */
	drm_clip_rect_t boxes[RADEON_NR_SAREA_CLIPRECTS];
	unsigned int nbox;

	/* Counters for client-side throttling of rendering clients.
	 */
	unsigned int last_frame;
	unsigned int last_dispatch;

	drm_radeon_tex_region_t tex_list[RADEON_NR_TEX_HEAPS][RADEON_NR_TEX_REGIONS+1];
	int tex_age[RADEON_NR_TEX_HEAPS];
	int ctx_owner;
} drm_radeon_sarea_t;


/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmRADEON.h)
 */
typedef struct drm_radeon_init {
	enum {
		RADEON_INIT_CP    = 0x01,
		RADEON_CLEANUP_CP = 0x02
	} func;
	int sarea_priv_offset;
	int is_pci;
	int cp_mode;
	int cp_secure;
	int agp_size;
	int ring_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int front_x, front_y;
	unsigned int back_offset, back_pitch;
	unsigned int back_x, back_y;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;
	unsigned int depth_x, depth_y;

	unsigned int fb_offset;
	unsigned int mmio_offset;
	unsigned int ring_offset;
	unsigned int ring_rptr_offset;
	unsigned int buffers_offset;
	unsigned int agp_textures_offset;
} drm_radeon_init_t;

typedef struct drm_radeon_cp_stop {
	int flush;
	int idle;
} drm_radeon_cp_stop_t;

typedef struct drm_radeon_clear {
	unsigned int flags;
	int x, y, w, h;
	unsigned int clear_color;
	unsigned int clear_depth;
	unsigned int color_mask;
	unsigned int depth_mask;
} drm_radeon_clear_t;

typedef struct drm_radeon_vertex {
	int prim;
	int idx;			/* Index of vertex buffer */
	int count;			/* Number of vertices in buffer */
	int discard;			/* Client finished with buffer? */
} drm_radeon_vertex_t;

typedef struct drm_radeon_indices {
	int prim;
	int idx;
	int start;
	int end;
	int discard;			/* Client finished with buffer? */
} drm_radeon_indices_t;

typedef struct drm_radeon_blit_rect {
	int index;
	unsigned short x, y;
	unsigned short width, height;
	int padding;
} drm_radeon_blit_rect_t;

typedef struct drm_radeon_blit {
	int pitch;
	int offset;
	int format;
	drm_radeon_blit_rect_t *rects;
	int count;
} drm_radeon_blit_t;

typedef struct drm_radeon_packet {
	unsigned int *buffer;
	int count;
	int flags;
} drm_radeon_packet_t;

#endif
