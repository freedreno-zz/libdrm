/* radeon_drm.h -- Public header for the radeon driver -*- linux-c -*-
 * Created: Wed Apr  5 19:24:19 2000 by kevin@precisioninsight.com
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 * Authors: Kevin E. Martin <martin@valinux.com>
 *
 */

#ifndef _RADEON_DRM_H_
#define _RADEON_DRM_H_

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
	int cp_fifo_size;
	int cp_secure;
	int ring_size;
	int usec_timeout;

	int fb_offset;
	int agp_ring_offset;
	int agp_read_ptr_offset;
	int agp_vertbufs_offset;
	int agp_indbufs_offset;
	int agp_textures_offset;
	int mmio_offset;
} drm_radeon_init_t;

typedef struct drm_radeon_packet {
	unsigned long *buffer;
	int            count;
	int            flags;
} drm_radeon_packet_t;

typedef enum drm_radeon_prim {
	_DRM_RADEON_PRIM_NONE		= 0x0001,
	_DRM_RADEON_PRIM_POINT		= 0x0002,
	_DRM_RADEON_PRIM_LINE		= 0x0004,
	_DRM_RADEON_PRIM_POLY_LINE	= 0x0008,
	_DRM_RADEON_PRIM_TRI_LIST	= 0x0010,
	_DRM_RADEON_PRIM_TRI_FAN	= 0x0020,
	_DRM_RADEON_PRIM_TRI_STRIP	= 0x0040,
	_DRM_RADEON_PRIM_TRI_TYPE2	= 0x0080
} drm_radeon_prim_t;

typedef struct drm_radeon_vertex {
				/* Indices here refer to the offset into
				   buflist in drm_buf_get_t.  */
	int		send_count;	  /* Number of buffers to send	    */
	int		*send_indices;	  /* List of handles to buffers	    */
	int		*send_sizes;	  /* Lengths of data to send	    */
	drm_radeon_prim_t prim;		  /* Primitive type		    */
	int		request_count;	  /* Number of buffers requested    */
	int		*request_indices; /* Buffer information		    */
	int		*request_sizes;
	int		granted_count;	  /* Number of buffers granted	    */
} drm_radeon_vertex_t;

/* WARNING: If you change any of these defines, make sure to change the
 * defines in the Xserver file (radeon_sarea.h)
 */
#define RADEON_LOCAL_TEX_HEAP       0
#define RADEON_AGP_TEX_HEAP         1
#define RADEON_NR_TEX_HEAPS         2
#define RADEON_NR_TEX_REGIONS      64
#define RADEON_LOG_TEX_GRANULARITY 16

#if 0
typedef struct drm_tex_region {
	unsigned char next, prev;       
	unsigned char in_use;   
	int age;                        
} drm_tex_region_t;
#endif

typedef struct drm_radeon_sarea {
	drm_tex_region_t tex_list[RADEON_NR_TEX_HEAPS][RADEON_NR_TEX_REGIONS+1];
	int              tex_age[RADEON_NR_TEX_HEAPS];
	int              ctx_owner;
	int              ring_write;
} drm_radeon_sarea_t;

#endif
