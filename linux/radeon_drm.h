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

#ifndef __RADEON_DRM_H__
#define __RADEON_DRM_H__

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the X server file (radeon_sarea.h)
 */
#ifndef __RADEON_SAREA_DEFINES__
#define __RADEON_SAREA_DEFINES__

/* What needs to be changed for the current vertex buffer?
 */
#define RADEON_UPLOAD_CONTEXT		0x00000001
#define RADEON_UPLOAD_MISC		0x00000002
#define RADEON_UPLOAD_LINE		0x00000004
#define RADEON_UPLOAD_BUMPMAP		0x00000008
#define RADEON_UPLOAD_MASKS		0x00000010
#define RADEON_UPLOAD_VIEWPORT		0x00000020
#define RADEON_UPLOAD_SETUP		0x00000040
#define RADEON_UPLOAD_TCL		0x00000080
#define RADEON_UPLOAD_TEX0		0x00000100
#define RADEON_UPLOAD_TEX1		0x00000200
#define RADEON_UPLOAD_TEX2		0x00000400
#define RADEON_UPLOAD_CONTEXT_ALL	0x000000ff
#define RADEON_UPLOAD_ALL		0x000007ff

#define RADEON_FRONT			0x1
#define RADEON_BACK			0x2
#define RADEON_DEPTH			0x4


/* Vertex/indirect buffer size
 */
#define RADEON_BUFFER_SIZE		65536

/* Byte offsets for indirect buffer data
 */
#define RADEON_INDEX_PRIM_OFFSET	20
#define RADEON_HOSTDATA_BLIT_OFFSET	32

#define RADEON_SCRATCH_REG_OFFSET	32

/* Keep these small for testing
 */
#define RADEON_NR_SAREA_CLIPRECTS	12

/* There are 2 heaps (local/AGP).  Each region within a heap is a
 * minimum of 64k, and there are at most 32 of them per heap.
 */
#define RADEON_LOCAL_TEX_HEAP		0
#define RADEON_AGP_TEX_HEAP		1
#define RADEON_NR_TEX_HEAPS		2
#define RADEON_NR_TEX_REGIONS		32
#define RADEON_LOG_TEX_GRANULARITY	16

#define RADEON_MAX_TEXTURE_LEVELS	11
#define RADEON_MAX_TEXTURE_UNITS	3

#endif /* __RADEON_SAREA_DEFINES__ */

typedef struct {
	unsigned int red;
	unsigned int green;
	unsigned int blue;
	unsigned int alpha;
} radeon_color_regs_t;

/* Context state */
typedef struct {
	unsigned int pp_misc;				/* 0x1c14 */
	unsigned int pp_fog_color;
	unsigned int re_solid_color;
	unsigned int rb3d_blendcntl;
	unsigned int rb3d_depthoffset;
	unsigned int rb3d_depthpitch;
	unsigned int rb3d_zstencilcntl;
	unsigned int pp_cntl;				/* 0x1c38 */
	unsigned int rb3d_cntl;
	unsigned int rb3d_coloroffset;
	unsigned int re_width_height;
	unsigned int rb3d_colorpitch;
} drm_radeon_context_state_t;


/* Line state */
typedef struct {
	unsigned int re_line_pattern;			/* 0x1cd0 */
	unsigned int re_line_state;
	unsigned int se_line_width;			/* 0x1db8 */
} drm_radeon_line_state_t;

/* Bumpmap state */
typedef struct {
	unsigned int pp_lum_matrix;			/* 0x1d00 */
	unsigned int pp_rot_matrix_0;			/* 0x1d58 */
	unsigned int pp_rot_matrix_1;
} drm_radeon_bumpmap_state_t;

/* Mask state */
typedef struct {
	unsigned int rb3d_stencilrefmask;		/* 0x1d7c */
	unsigned int rb3d_ropcntl;
	unsigned int rb3d_planemask;
} drm_radeon_mask_state_t;

/* Viewport state */
typedef struct {
	unsigned int se_vport_xscale;			/* 0x1d98 */
	unsigned int se_vport_xoffset;
	unsigned int se_vport_yscale;
	unsigned int se_vport_yoffset;
	unsigned int se_vport_zscale;
	unsigned int se_vport_zoffset;
} drm_radeon_viewport_state_t;

/* Setup state */
typedef struct {
	unsigned int se_cntl;
	unsigned int se_cntl_status;			/* 0x2140 */
	unsigned int se_coord_fmt;			/* 0x1c50 */
} drm_radeon_setup_state_t;

#ifdef TCL_ENABLE
/* TCL state */
typedef struct {
	radeon_color_regs_t se_tcl_material_emmissive;	/* 0x2210 */
	radeon_color_regs_t se_tcl_material_ambient;
	radeon_color_regs_t se_tcl_material_diffuse;
	radeon_color_regs_t se_tcl_material_specular;
	unsigned int se_tcl_shininess;
	unsigned int se_tcl_output_vtx_fmt;
	unsigned int se_tcl_output_vtx_sel;
	unsigned int se_tcl_matrix_select_0;
	unsigned int se_tcl_matrix_select_1;
	unsigned int se_tcl_ucp_vert_blend_ctl;
	unsigned int se_tcl_texture_proc_ctl;
	unsigned int se_tcl_light_model_ctl;
	unsigned int se_tcl_per_light_ctl[4];
} drm_radeon_tcl_state_t;
#endif

/* Misc state */
typedef struct {
	unsigned int re_misc;
} drm_radeon_misc_state_t;

/* Setup registers for each texture unit
 */
typedef struct {
	unsigned int pp_txfilter;
	unsigned int pp_txformat;
	unsigned int pp_txoffset;
	unsigned int pp_txcblend;
	unsigned int pp_txablend;
	unsigned int pp_tfactor;
	unsigned int pp_border_color;
#ifdef CUBIC_ENABLE
	unsigned int pp_cubic_faces;
	unsigned int pp_cubic_offset[5];
#endif
} drm_radeon_texture_regs_t;

typedef struct {
	unsigned char next, prev;
	unsigned char in_use;
	int age;
} drm_radeon_tex_region_t;


typedef struct {
	drm_radeon_context_state_t context;
	drm_radeon_line_state_t line;
	drm_radeon_bumpmap_state_t bumpmap;
	drm_radeon_mask_state_t mask;
	drm_radeon_viewport_state_t viewport;
	drm_radeon_setup_state_t setup;
	drm_radeon_misc_state_t misc;
	drm_radeon_texture_regs_t texunit[RADEON_MAX_TEXTURE_UNITS];
	int dirty;
} drm_radeon_state_t;

/* Space is crucial; there is some redunancy here:
 */
typedef struct {
	unsigned int start;	
	unsigned int finish;	
	unsigned int prim:8;
	unsigned int stateidx:8;
	unsigned int numverts:16; /* overloaded as offset/64 for elt prims */
        unsigned int vc_format;		
} drm_radeon_prim_t;

/* Really need a bigger area to hold the primitive and state data:
 */
#define RADEON_MAX_PRIMS  30
#define RADEON_MAX_STATES 2


typedef struct {
	/* The channel for communication of state information to the kernel
	 * on firing a vertex buffer.
	 */
	drm_radeon_state_t state[RADEON_MAX_STATES];
	drm_radeon_prim_t prim[RADEON_MAX_PRIMS];
	unsigned int primnr;

	/* The current cliprects, or a subset thereof.
	 */
	drm_clip_rect_t boxes[RADEON_NR_SAREA_CLIPRECTS];
	unsigned int nbox;

	/* Counters for client-side throttling of rendering clients.
	 */
	unsigned int last_frame;
	unsigned int last_dispatch;
	unsigned int last_clear;

	drm_radeon_tex_region_t tex_list[RADEON_NR_TEX_HEAPS][RADEON_NR_TEX_REGIONS+1];
	int tex_age[RADEON_NR_TEX_HEAPS];
	int ctx_owner;
} drm_radeon_sarea_t;


/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (xf86drmRadeon.h)
 */
typedef struct drm_radeon_init {
	enum {
		RADEON_INIT_CP    = 0x01,
		RADEON_CLEANUP_CP = 0x02
	} func;
	int sarea_priv_offset;
	int is_pci;
	int cp_mode;
	int agp_size;
	int ring_size;
	int usec_timeout;

	unsigned int fb_bpp;
	unsigned int front_offset, front_pitch;
	unsigned int back_offset, back_pitch;
	unsigned int depth_bpp;
	unsigned int depth_offset, depth_pitch;

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

typedef struct drm_radeon_fullscreen {
	enum {
		RADEON_INIT_FULLSCREEN    = 0x01,
		RADEON_CLEANUP_FULLSCREEN = 0x02
	} func;
} drm_radeon_fullscreen_t;

#define CLEAR_X1	0
#define CLEAR_Y1	1
#define CLEAR_X2	2
#define CLEAR_Y2	3
#define CLEAR_DEPTH	4

typedef struct drm_radeon_clear {
	unsigned int flags;
	int x, y, w, h;
	unsigned int clear_color;
	unsigned int clear_colormask;
	unsigned int clear_depth;
	union {
		float f[5];
		unsigned int ui[5];
	} rect;
} drm_radeon_clear_t;

typedef struct drm_radeon_vertex {
	int idx;			/* Index of vertex buffer */
	int discard;			/* Client finished with buffer? */
} drm_radeon_vertex_t;

typedef struct drm_radeon_blit {
	int idx;
	int pitch;
	int offset;
	int format;
	unsigned short x, y;
	unsigned short width, height;
} drm_radeon_blit_t;

typedef struct drm_radeon_stipple {
	unsigned int *mask;
} drm_radeon_stipple_t;

typedef struct drm_radeon_indirect {
	int idx;
	int start;
	int end;
	int discard;
} drm_radeon_indirect_t;

#endif
