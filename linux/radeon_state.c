/* radeon_state.c -- State support for Radeon -*- linux-c -*-
 *
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
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "radeon_drv.h"
#include "drm.h"

/* This must be defined to 1 for now */
#define USE_OLD_BLITS	1

static drm_radeon_blit_rect_t rects[RADEON_MAX_BLIT_BUFFERS];


/* ================================================================
 * CP hardware state programming functions
 */

static void radeon_emit_clip_rects( drm_radeon_private_t *dev_priv,
				    drm_clip_rect_t *boxes, int count )
{
#if 0
	unsigned int aux_sc_cntl = 0x00000000;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 17 );

	if ( count >= 1 ) {
		OUT_RING( CP_PACKET0( RADEON_AUX1_SC_LEFT, 3 ) );
		OUT_RING( boxes[0].x1 );
		OUT_RING( boxes[0].x2 - 1 );
		OUT_RING( boxes[0].y1 );
		OUT_RING( boxes[0].y2 - 1 );

		aux_sc_cntl |= (RADEON_AUX1_SC_EN | RADEON_AUX1_SC_MODE_OR);
	}
	if ( count >= 2 ) {
		OUT_RING( CP_PACKET0( RADEON_AUX2_SC_LEFT, 3 ) );
		OUT_RING( boxes[1].x1 );
		OUT_RING( boxes[1].x2 - 1 );
		OUT_RING( boxes[1].y1 );
		OUT_RING( boxes[1].y2 - 1 );

		aux_sc_cntl |= (RADEON_AUX2_SC_EN | RADEON_AUX2_SC_MODE_OR);
	}
	if ( count >= 3 ) {
		OUT_RING( CP_PACKET0( RADEON_AUX3_SC_LEFT, 3 ) );
		OUT_RING( boxes[2].x1 );
		OUT_RING( boxes[2].x2 - 1 );
		OUT_RING( boxes[2].y1 );
		OUT_RING( boxes[2].y2 - 1 );

		aux_sc_cntl |= (RADEON_AUX3_SC_EN | RADEON_AUX3_SC_MODE_OR);
	}

	OUT_RING( CP_PACKET0( RADEON_AUX_SC_CNTL, 0 ) );
	OUT_RING( aux_sc_cntl );

	ADVANCE_RING();
#endif
}

static inline void radeon_emit_context( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 15 );

	OUT_RING( CP_PACKET0( RADEON_PP_MISC, 6 ) );
	OUT_RING( ctx->pp_misc );
	OUT_RING( ctx->pp_fog_color );
	OUT_RING( ctx->re_solid_color );
	OUT_RING( ctx->rb3d_blendcntl );
	OUT_RING( ctx->rb3d_depthoffset );
	OUT_RING( ctx->rb3d_depthpitch );
	OUT_RING( ctx->rb3d_zstencilcntl );

	OUT_RING( CP_PACKET0( RADEON_PP_CNTL, 5 ) );
	OUT_RING( ctx->pp_cntl );
	OUT_RING( ctx->rb3d_cntl );
	OUT_RING( ctx->rb3d_coloroffset );
	OUT_RING( ctx->re_width_height );
	OUT_RING( ctx->rb3d_colorpitch );
	OUT_RING( ctx->se_cntl );

	ADVANCE_RING();
}

static inline void radeon_emit_vertfmt( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 2 );

	OUT_RING( CP_PACKET0( RADEON_SE_COORD_FMT, 0 ) );
	OUT_RING( ctx->se_coord_fmt );

	ADVANCE_RING();
}

static inline void radeon_emit_line( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 5 );

	OUT_RING( CP_PACKET0( RADEON_RE_LINE_PATTERN, 1 ) );
	OUT_RING( ctx->re_line_pattern );
	OUT_RING( ctx->re_line_state );

	OUT_RING( CP_PACKET0( RADEON_SE_LINE_WIDTH, 0 ) );
	OUT_RING( ctx->se_line_width );

	ADVANCE_RING();
}

static inline void radeon_emit_bumpmap( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 5 );

	OUT_RING( CP_PACKET0( RADEON_PP_LUM_MATRIX, 0 ) );
	OUT_RING( ctx->pp_lum_matrix );

	OUT_RING( CP_PACKET0( RADEON_PP_ROT_MATRIX_0, 1 ) );
	OUT_RING( ctx->pp_rot_matrix_0 );
	OUT_RING( ctx->pp_rot_matrix_1 );

	ADVANCE_RING();
}

static inline void radeon_emit_masks( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 4 );

	OUT_RING( CP_PACKET0( RADEON_RB3D_STENCILREFMASK, 2 ) );
	OUT_RING( ctx->rb3d_stencilrefmask );
	OUT_RING( ctx->rb3d_ropcntl );
	OUT_RING( ctx->rb3d_planemask );

	ADVANCE_RING();
}

static inline void radeon_emit_viewport( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 7 );

	OUT_RING( CP_PACKET0( RADEON_SE_VPORT_XSCALE, 5 ) );
	OUT_RING( ctx->se_vport_xscale );
	OUT_RING( ctx->se_vport_xoffset );
	OUT_RING( ctx->se_vport_yscale );
	OUT_RING( ctx->se_vport_yoffset );
	OUT_RING( ctx->se_vport_zscale );
	OUT_RING( ctx->se_vport_zoffset );

	ADVANCE_RING();
}

static inline void radeon_emit_setup( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	/* Note this duplicates the uploading of se_cntl, which is part
           of the context, but adding it here optimizes the reduced
           primitive change since we currently render points and lines
           with triangles.  In the future, we probably won't need this
           optimization. */

#if 0
	/* Why doesn't CP_PACKET1 work? */
	BEGIN_RING( 3 );

	OUT_RING( CP_PACKET1( RADEON_SE_CNTL, RADEON_SE_CNTL_STATUS ) );
	OUT_RING( ctx->se_cntl );
	OUT_RING( ctx->se_cntl_status );
#else
	BEGIN_RING( 4 );

	OUT_RING( CP_PACKET0( RADEON_SE_CNTL, 0 ) );
	OUT_RING( ctx->se_cntl );
	OUT_RING( CP_PACKET0( RADEON_SE_CNTL_STATUS, 0 ) );
	OUT_RING( ctx->se_cntl_status );
#endif

	ADVANCE_RING();
}

#ifdef TCL_ENABLE
static inline void radeon_emit_tcl( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 29 );

	OUT_RING( CP_PACKET0( RADEON_SE_TCL_MATERIAL_EMMISSIVE_RED, 27 ) );
	OUT_RING( ctx->se_tcl_material_emmissive.red );
	OUT_RING( ctx->se_tcl_material_emmissive.green );
	OUT_RING( ctx->se_tcl_material_emmissive.blue );
	OUT_RING( ctx->se_tcl_material_emmissive.alpha );
	OUT_RING( ctx->se_tcl_material_ambient.red );
	OUT_RING( ctx->se_tcl_material_ambient.green );
	OUT_RING( ctx->se_tcl_material_ambient.blue );
	OUT_RING( ctx->se_tcl_material_ambient.alpha );
	OUT_RING( ctx->se_tcl_material_diffuse.red );
	OUT_RING( ctx->se_tcl_material_diffuse.green );
	OUT_RING( ctx->se_tcl_material_diffuse.blue );
	OUT_RING( ctx->se_tcl_material_diffuse.alpha );
	OUT_RING( ctx->se_tcl_material_specular.red );
	OUT_RING( ctx->se_tcl_material_specular.green );
	OUT_RING( ctx->se_tcl_material_specular.blue );
	OUT_RING( ctx->se_tcl_material_specular.alpha );
	OUT_RING( ctx->se_tcl_shininess );
	OUT_RING( ctx->se_tcl_output_vtx_fmt );
	OUT_RING( ctx->se_tcl_output_vtx_sel );
	OUT_RING( ctx->se_tcl_matrix_select_0 );
	OUT_RING( ctx->se_tcl_matrix_select_1 );
	OUT_RING( ctx->se_tcl_ucp_vert_blend_ctl );
	OUT_RING( ctx->se_tcl_texture_proc_ctl );
	OUT_RING( ctx->se_tcl_light_model_ctl );
	for (i = 0; i < 4; i++)
		OUT_RING( ctx->se_tcl_per_light_ctl[i] );

	ADVANCE_RING();
}
#endif

static inline void radeon_emit_misc( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 3 );

	OUT_RING( CP_PACKET0( RADEON_RE_TOP_LEFT, 1 ) );
	OUT_RING( ctx->re_top_left );
	OUT_RING( ctx->re_misc );

	ADVANCE_RING();
}

static inline void radeon_emit_tex0( drm_radeon_private_t *dev_priv )
{
#if 0
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_context_regs_t *ctx = &sarea_priv->context_state;
	drm_radeon_texture_regs_t *tex = &sarea_priv->tex_state[0];
	int i;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 7 + RADEON_TEX_MAXLEVELS );

	OUT_RING( CP_PACKET0( RADEON_PRIM_TEX_CNTL_C,
			       2 + RADEON_TEX_MAXLEVELS ) );
	OUT_RING( tex->tex_cntl );
	OUT_RING( tex->tex_combine_cntl );
	OUT_RING( ctx->tex_size_pitch_c );
	for ( i = 0 ; i < RADEON_TEX_MAXLEVELS ; i++ ) {
		OUT_RING( tex->tex_offset[i] );
	}

	OUT_RING( CP_PACKET0( RADEON_CONSTANT_COLOR_C, 1 ) );
	OUT_RING( ctx->constant_color_c );
	OUT_RING( tex->tex_border_color );

	ADVANCE_RING();
#endif
}

static inline void radeon_emit_tex1( drm_radeon_private_t *dev_priv )
{
#if 0
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_texture_regs_t *tex = &sarea_priv->tex_state[1];
	int i;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 5 + RADEON_TEX_MAXLEVELS );

	OUT_RING( CP_PACKET0( RADEON_SEC_TEX_CNTL_C,
			       1 + RADEON_TEX_MAXLEVELS ) );
	OUT_RING( tex->tex_cntl );
	OUT_RING( tex->tex_combine_cntl );
	for ( i = 0 ; i < RADEON_TEX_MAXLEVELS ; i++ ) {
		OUT_RING( tex->tex_offset[i] );
	}

	OUT_RING( CP_PACKET0( RADEON_SEC_TEXTURE_BORDER_COLOR_C, 0 ) );
	OUT_RING( tex->tex_border_color );

	ADVANCE_RING();
#endif
}

static inline void radeon_emit_tex2( drm_radeon_private_t *dev_priv )
{
#if 0
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_texture_regs_t *tex = &sarea_priv->tex_state[2];
	int i;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 5 + RADEON_TEX_MAXLEVELS );

	OUT_RING( CP_PACKET0( RADEON_SEC_TEX_CNTL_C,
			       1 + RADEON_TEX_MAXLEVELS ) );
	OUT_RING( tex->tex_cntl );
	OUT_RING( tex->tex_combine_cntl );
	for ( i = 0 ; i < RADEON_TEX_MAXLEVELS ; i++ ) {
		OUT_RING( tex->tex_offset[i] );
	}

	OUT_RING( CP_PACKET0( RADEON_SEC_TEXTURE_BORDER_COLOR_C, 0 ) );
	OUT_RING( tex->tex_border_color );

	ADVANCE_RING();
#endif
}

static inline void radeon_emit_state( drm_radeon_private_t *dev_priv )
{
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	DRM_DEBUG( "%s: dirty=0x%08x\n", __FUNCTION__, dirty );

	if ( dirty & RADEON_UPLOAD_CONTEXT ) {
		radeon_emit_context( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_CONTEXT;
	}

	if ( dirty & RADEON_UPLOAD_VERTFMT ) {
		radeon_emit_vertfmt( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_VERTFMT;
	}

	if ( dirty & RADEON_UPLOAD_LINE ) {
		radeon_emit_line( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_LINE;
	}

	if ( dirty & RADEON_UPLOAD_BUMPMAP ) {
		radeon_emit_bumpmap( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_BUMPMAP;
	}

	if ( dirty & RADEON_UPLOAD_MASKS ) {
		radeon_emit_masks( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_MASKS;
	}

	if ( dirty & RADEON_UPLOAD_VIEWPORT ) {
		radeon_emit_viewport( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_VIEWPORT;
	}

	if ( dirty & RADEON_UPLOAD_SETUP ) {
		radeon_emit_setup( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_SETUP;
	}

#ifdef TCL_ENABLE
	if ( dirty & RADEON_UPLOAD_TCL ) {
		radeon_emit_tcl( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_TCL;
	}
#endif

	if ( dirty & RADEON_UPLOAD_MISC ) {
		radeon_emit_misc( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_MISC;
	}

	if ( dirty & RADEON_UPLOAD_TEX0 ) {
		radeon_emit_tex0( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_TEX0;
	}

	if ( dirty & RADEON_UPLOAD_TEX1 ) {
		radeon_emit_tex1( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_TEX1;
	}

	if ( dirty & RADEON_UPLOAD_TEX2 ) {
		radeon_emit_tex1( dev_priv );
		sarea_priv->dirty &= ~RADEON_UPLOAD_TEX1;
	}

#if 0
	/* Turn off the texture cache flushing */
	sarea_priv->context_state.tex_cntl_c &= ~RADEON_TEX_CACHE_FLUSH;
#endif

	sarea_priv->dirty &= ~(RADEON_UPLOAD_TEX0IMAGES |
			       RADEON_UPLOAD_TEX1IMAGES |
			       RADEON_UPLOAD_TEX2IMAGES |
			       RADEON_REQUIRE_QUIESCENCE);
}


#if RADEON_PERFORMANCE_BOXES
/* ================================================================
 * Performance monitoring functions
 */

static void radeon_clear_box( drm_radeon_private_t *dev_priv,
			      int x, int y, int w, int h,
			      int r, int g, int b )
{
	u32 pitch, offset;
	u32 fb_bpp, color;
	RING_LOCALS;

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = RADEON_GMC_DST_16BPP;
		color = (((r & 0xf8) << 8) |
			 ((g & 0xfc) << 3) |
			 ((b & 0xf8) >> 3));
		break;
	case 32:
	default:
		fb_bpp = RADEON_GMC_DST_32BPP;
		color = (((0xff) << 24) | (r << 16) | (g <<  8) | b);
		break;
	}

	offset = dev_priv->back_offset;
	pitch = dev_priv->back_pitch >> 3;

	BEGIN_RING( 6 );

	OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
	OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL
		  | RADEON_GMC_BRUSH_SOLID_COLOR
		  | fb_bpp
		  | RADEON_GMC_SRC_DATATYPE_COLOR
		  | RADEON_ROP3_P
		  | RADEON_GMC_CLR_CMP_CNTL_DIS );

	OUT_RING( (pitch << 22) | (offset >> 5) );
	OUT_RING( color );

	OUT_RING( (x << 16) | y );
	OUT_RING( (w << 16) | h );

	ADVANCE_RING();
}

static void radeon_cp_performance_boxes( drm_radeon_private_t *dev_priv )
{
	if ( atomic_read( &dev_priv->idle_count ) == 0 ) {
		radeon_clear_box( dev_priv, 64, 4, 8, 8, 0, 255, 0 );
	} else {
		atomic_set( &dev_priv->idle_count, 0 );
	}
}

#endif


/* ================================================================
 * CP command dispatch functions
 */

static void radeon_print_dirty( const char *msg, unsigned int flags )
{
	DRM_DEBUG( "%s: (0x%x) %s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
		   msg,
		   flags,
		   (flags & RADEON_UPLOAD_CONTEXT)     ? "context, " : "",
		   (flags & RADEON_UPLOAD_VERTFMT)     ? "vertfmt, " : "",
		   (flags & RADEON_UPLOAD_LINE)        ? "line, " : "",
		   (flags & RADEON_UPLOAD_BUMPMAP)     ? "bumpmap, " : "",
		   (flags & RADEON_UPLOAD_MASKS)       ? "masks, " : "",
		   (flags & RADEON_UPLOAD_VIEWPORT)    ? "viewport, " : "",
		   (flags & RADEON_UPLOAD_SETUP)       ? "setup, " : "",
		   (flags & RADEON_UPLOAD_TCL)         ? "tcl, " : "",
		   (flags & RADEON_UPLOAD_MISC)        ? "misc, " : "",
		   (flags & RADEON_UPLOAD_TEX0)        ? "tex0, " : "",
		   (flags & RADEON_UPLOAD_TEX1)        ? "tex1, " : "",
		   (flags & RADEON_UPLOAD_TEX2)        ? "tex2, " : "",
		   (flags & RADEON_UPLOAD_CLIPRECTS)   ? "cliprects, " : "",
		   (flags & RADEON_REQUIRE_QUIESCENCE) ? "quiescence, " : "" );
}

static void radeon_cp_dispatch_clear( drm_device_t *dev,
				      unsigned int flags,
				      int cx, int cy, int cw, int ch,
				      unsigned int clear_color,
				      unsigned int clear_depth,
				      unsigned int color_mask,
				      unsigned int depth_mask )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	u32 fb_bpp, depth_bpp;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	radeon_update_ring_snapshot( dev_priv );

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = RADEON_GMC_DST_16BPP;
		break;
	case 32:
	default:
		fb_bpp = RADEON_GMC_DST_32BPP;
		break;
	}
	switch ( dev_priv->depth_bpp ) {
	case 16:
		depth_bpp = RADEON_GMC_DST_16BPP;
		break;
	case 32:
		depth_bpp = RADEON_GMC_DST_32BPP;
		break;
	default:
		return;
	}

	for ( i = 0 ; i < nbox ; i++ ) {
		int x = pbox[i].x1;
		int y = pbox[i].y1;
		int w = pbox[i].x2 - x;
		int h = pbox[i].y2 - y;

		DRM_DEBUG( "dispatch clear %d,%d-%d,%d flags 0x%x\n",
			   pbox[i].x1, pbox[i].y1, pbox[i].x2,
			   pbox[i].y2, flags );

		if ( flags & (RADEON_FRONT | RADEON_BACK) ) {
			BEGIN_RING( 2 );

			OUT_RING( CP_PACKET0( RADEON_DP_WRITE_MASK, 0 ) );
			OUT_RING( color_mask );

			ADVANCE_RING();
		}

#if USE_OLD_BLITS
		if ( flags & RADEON_FRONT ) {
			int fx = x + dev_priv->front_x;
			int fy = y + dev_priv->front_y;

			DRM_DEBUG( "clear front: x=%d y=%d\n",
				   dev_priv->front_x, dev_priv->front_y );
			BEGIN_RING( 5 );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 3 ) );
			OUT_RING( RADEON_GMC_BRUSH_SOLID_COLOR
				  | fb_bpp
				  | RADEON_GMC_SRC_DATATYPE_COLOR
				  | RADEON_ROP3_P
				  | RADEON_GMC_CLR_CMP_CNTL_DIS );
			OUT_RING( clear_color );
			OUT_RING( (fx << 16) | fy );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & RADEON_BACK ) {
			int bx = x + dev_priv->back_x;
			int by = y + dev_priv->back_y;

			DRM_DEBUG( "clear back: x=%d y=%d\n",
				   dev_priv->back_x, dev_priv->back_y );
			BEGIN_RING( 5 );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 3 ) );
			OUT_RING( RADEON_GMC_BRUSH_SOLID_COLOR
				  | fb_bpp
				  | RADEON_GMC_SRC_DATATYPE_COLOR
				  | RADEON_ROP3_P
				  | RADEON_GMC_CLR_CMP_CNTL_DIS );
			OUT_RING( clear_color );
			OUT_RING( (bx << 16) | by );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & RADEON_DEPTH ) {
			int dx = x;
			int dy = y;
			drm_radeon_context_regs_t *ctx =
				&sarea_priv->context_state;
			u32 rb3d_cntl = ctx->rb3d_cntl;
			u32 rb3d_zstencilcntl = ctx->rb3d_zstencilcntl;
			u32 se_cntl = ctx->se_cntl;

			DRM_DEBUG( "clear depth: x=%d y=%d\n",
				   dev_priv->depth_x, dev_priv->depth_y );

			rb3d_cntl |= ( RADEON_PLANE_MASK_ENABLE
				       | RADEON_Z_ENABLE );

			rb3d_zstencilcntl &= ~RADEON_Z_TEST_MASK;
			rb3d_zstencilcntl |= ( RADEON_Z_TEST_ALWAYS
					       | RADEON_Z_WRITE_ENABLE );

			se_cntl &= ~( RADEON_VPORT_XY_XFORM_ENABLE
				      | RADEON_VPORT_Z_XFORM_ENABLE
				      | RADEON_FFACE_CULL_MASK
				      | RADEON_BFACE_CULL_MASK );
			se_cntl |= ( RADEON_FFACE_SOLID
				     | RADEON_BFACE_SOLID );

			BEGIN_RING( 28 );

			OUT_RING( CP_PACKET0( RADEON_RB3D_CNTL, 0 ) );
			OUT_RING( rb3d_cntl );

			OUT_RING( CP_PACKET0( RADEON_RB3D_ZSTENCILCNTL, 0 ) );
			OUT_RING( rb3d_zstencilcntl );

			OUT_RING( CP_PACKET0( RADEON_RB3D_PLANEMASK, 0 ) );
			OUT_RING( 0x00000000 );

			OUT_RING( CP_PACKET0( RADEON_SE_CNTL, 0 ) );
			OUT_RING( se_cntl );

			/* Draw rectangle */
			OUT_RING( CP_PACKET3( RADEON_3D_DRAW_IMMD, 10 ) );
			OUT_RING( RADEON_CP_VC_FRMT_XY
				  | RADEON_CP_VC_FRMT_Z);
			OUT_RING( RADEON_CP_VC_CNTL_VTX_FMT_RADEON_MODE
				  | RADEON_CP_VC_CNTL_MAOS_ENABLE
				  | RADEON_CP_VC_CNTL_PRIM_WALK_RING
				  | RADEON_CP_VC_CNTL_PRIM_TYPE_RECT_LIST
				  | ( 3 << RADEON_CP_VC_CNTL_NUM_SHIFT ) );
			{
				union {
					float f;
					u32 u;
				} val;

				val.f = dx;          OUT_RING( val.u );
				val.f = dy;          OUT_RING( val.u );
				val.f = clear_depth; OUT_RING( val.u );

				val.f = dx;          OUT_RING( val.u );
				val.f = dy + h;      OUT_RING( val.u );
				val.f = clear_depth; OUT_RING( val.u );

				val.f = dx + w;      OUT_RING( val.u );
				val.f = dy + h;      OUT_RING( val.u );
				val.f = clear_depth; OUT_RING( val.u );
			}

			OUT_RING( CP_PACKET0( RADEON_RB3D_CNTL, 0 ) );
			OUT_RING( ctx->rb3d_cntl );

			OUT_RING( CP_PACKET0( RADEON_RB3D_ZSTENCILCNTL, 0 ) );
			OUT_RING( ctx->rb3d_zstencilcntl );

			OUT_RING( CP_PACKET0( RADEON_RB3D_PLANEMASK, 0 ) );
			OUT_RING( ctx->rb3d_planemask );

			OUT_RING( CP_PACKET0( RADEON_SE_CNTL, 0 ) );
			OUT_RING( ctx->se_cntl );

			ADVANCE_RING();
		}
#else
		if ( flags & RADEON_FRONT ) {
			BEGIN_RING( 6 );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
			OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL
				  | RADEON_GMC_BRUSH_SOLID_COLOR
				  | fb_bpp
				  | RADEON_GMC_SRC_DATATYPE_COLOR
				  | RADEON_ROP3_P
				  | RADEON_GMC_CLR_CMP_CNTL_DIS );

			OUT_RING( ((dev_priv->front_pitch/8) << 21) |
				  (dev_priv->front_offset >> 5) );
			OUT_RING( clear_color );

			OUT_RING( (x << 16) | y );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & RADEON_BACK ) {
			BEGIN_RING( 6 );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
			OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL
				  | RADEON_GMC_BRUSH_SOLID_COLOR
				  | fb_bpp
				  | RADEON_GMC_SRC_DATATYPE_COLOR
				  | RADEON_ROP3_P
				  | RADEON_GMC_CLR_CMP_CNTL_DIS );

			OUT_RING( ((dev_priv->back_pitch/8) << 21) |
				  (dev_priv->back_offset >> 5) );
			OUT_RING( clear_color );

			OUT_RING( (x << 16) | y );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & RADEON_DEPTH ) {
			BEGIN_RING( 8 );

			OUT_RING( CP_PACKET0( RADEON_DP_WRITE_MASK, 0 ) );
			OUT_RING( depth_mask );

			OUT_RING( CP_PACKET3( RADEON_CNTL_PAINT_MULTI, 4 ) );
			OUT_RING( RADEON_GMC_DST_PITCH_OFFSET_CNTL
				  | RADEON_GMC_BRUSH_SOLID_COLOR
				  | depth_bpp
				  | RADEON_GMC_SRC_DATATYPE_COLOR
				  | RADEON_ROP3_P
				  | RADEON_GMC_CLR_CMP_CNTL_DIS );

			OUT_RING( ((dev_priv->depth_pitch/8) << 21) |
				  (dev_priv->depth_offset >> 5) );
			OUT_RING( clear_depth );

			OUT_RING( (x << 16) | y );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}
#endif
	}
}

static void radeon_cp_dispatch_swap( drm_device_t *dev )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	u32 fb_bpp;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	radeon_update_ring_snapshot( dev_priv );

#if RADEON_PERFORMANCE_BOXES
	/* Do some trivial performance monitoring...
	 */
	radeon_cp_performance_boxes( dev_priv );
#endif

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = RADEON_GMC_DST_16BPP;
		break;
	case 24:
		fb_bpp = RADEON_GMC_DST_24BPP;
		break;
	case 32:
	default:
		fb_bpp = RADEON_GMC_DST_32BPP;
		break;
	}

	for ( i = 0 ; i < nbox ; i++ ) {
		int fx = pbox[i].x1;
		int fy = pbox[i].y1;
		int fw = pbox[i].x2 - fx;
		int fh = pbox[i].y2 - fy;
#if USE_OLD_BLITS
		int bx = fx + dev_priv->back_x;
		int by = fy + dev_priv->back_y;

		fx += dev_priv->front_x;
		fy += dev_priv->front_x;

		BEGIN_RING( 5 );

		OUT_RING( CP_PACKET3( RADEON_CNTL_BITBLT_MULTI, 3 ) );
		OUT_RING( RADEON_GMC_BRUSH_NONE
			  | RADEON_GMC_SRC_DATATYPE_COLOR
			  | RADEON_DP_SRC_SOURCE_MEMORY
			  | fb_bpp
			  | RADEON_ROP3_S
			  | RADEON_GMC_CLR_CMP_CNTL_DIS
			  | RADEON_GMC_WR_MSK_DIS );

		OUT_RING( (bx << 16) | by );
		OUT_RING( (fx << 16) | fy );
		OUT_RING( (fw << 16) | fh );

		ADVANCE_RING();
#else
		BEGIN_RING( 7 );

		OUT_RING( CP_PACKET3( RADEON_CNTL_BITBLT_MULTI, 5 ) );
		OUT_RING( RADEON_GMC_SRC_PITCH_OFFSET_CNTL
			  | RADEON_GMC_DST_PITCH_OFFSET_CNTL
			  | RADEON_GMC_BRUSH_NONE
			  | RADEON_GMC_SRC_DATATYPE_COLOR
			  | RADEON_DP_SRC_SOURCE_MEMORY
			  | fb_bpp
			  | RADEON_ROP3_S
			  | RADEON_GMC_CLR_CMP_CNTL_DIS
			  | RADEON_GMC_WR_MSK_DIS );

		OUT_RING( ((dev_priv->back_pitch/8) << 21) |
			  (dev_priv->back_offset >> 5) );
		OUT_RING( ((dev_priv->front_pitch/8) << 21) |
			  (dev_priv->front_offset >> 5) );

		OUT_RING( (fx << 16) | fy );
		OUT_RING( (fx << 16) | fy );
		OUT_RING( (fw << 16) | fh );

		ADVANCE_RING();
#endif
	}

	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->last_frame++;

	BEGIN_RING( 2 );

	OUT_RING( CP_PACKET0( RADEON_LAST_FRAME_REG, 0 ) );
	OUT_RING( dev_priv->sarea_priv->last_frame );

	ADVANCE_RING();
}

static void radeon_cp_dispatch_vertex( drm_device_t *dev,
				       drm_buf_t *buf )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int vertsize = sarea_priv->vertsize;
	int format = sarea_priv->vc_format;
	int index = buf->idx;
	int offset = dev_priv->buffers->offset + buf->offset - dev->agp->base;
	int size = buf->used;
	int prim = buf_priv->prim;
	int i = 0;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	DRM_DEBUG( "vertex buffer index = %d\n", index );
	DRM_DEBUG( "vertex buffer offset = 0x%x\n", offset );
	DRM_DEBUG( "vertex buffer size = %d vertices\n", size );
	DRM_DEBUG( "vertex size = %d\n", vertsize );
	DRM_DEBUG( "vertex format = 0x%x\n", format );

	radeon_update_ring_snapshot( dev_priv );

	if ( 0 )
		radeon_print_dirty( "dispatch_vertex", sarea_priv->dirty );

	if ( buf->used ) {
		buf_priv->dispatched = 1;

		if ( sarea_priv->dirty & ~RADEON_UPLOAD_CLIPRECTS ) {
			radeon_emit_state( dev_priv );
		}

		do {
			/* Emit the next set of up to three cliprects */
			if ( i < sarea_priv->nbox ) {
				radeon_emit_clip_rects( dev_priv,
						      &sarea_priv->boxes[i],
						      sarea_priv->nbox - i );
			}

			/* Emit the vertex buffer rendering commands */
			BEGIN_RING( 5 );

			OUT_RING( CP_PACKET3( RADEON_3D_RNDR_GEN_INDX_PRIM,
					      3 ) );
			OUT_RING( offset + dev_priv->agp_vm_start );
			OUT_RING( size );
			OUT_RING( format );
			OUT_RING( prim | RADEON_CP_VC_CNTL_PRIM_WALK_LIST |
				  (size << RADEON_CP_VC_CNTL_NUM_SHIFT) );

			ADVANCE_RING();

			i += 3;
		} while ( i < sarea_priv->nbox );
	}

	if ( buf_priv->discard ) {
		buf_priv->age = dev_priv->sarea_priv->last_dispatch;

		/* Emit the vertex buffer age */
		BEGIN_RING( 2 );

		OUT_RING( CP_PACKET0( RADEON_LAST_DISPATCH_REG, 0 ) );
		OUT_RING( buf_priv->age );

		ADVANCE_RING();

		buf->pending = 1;

		/* FIXME: Check dispatched field */
		buf_priv->dispatched = 0;
	}

	dev_priv->sarea_priv->last_dispatch++;

#if 0
	if ( dev_priv->submit_age == RADEON_MAX_VB_AGE ) {
		ret = radeon_do_cp_idle( dev_priv );
		if ( ret < 0 ) return ret;
		dev_priv->submit_age = 0;
		radeon_freelist_reset( dev );
	}
#endif

	sarea_priv->dirty &= ~RADEON_UPLOAD_CLIPRECTS;
	sarea_priv->nbox = 0;
}




static void radeon_cp_dispatch_indirect( drm_device_t *dev,
					 drm_buf_t *buf,
					 int start, int end )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
	RING_LOCALS;
	DRM_DEBUG( "indirect: buf=%d s=0x%x e=0x%x\n",
		   buf->idx, start, end );

	radeon_update_ring_snapshot( dev_priv );

	if ( start != end ) {
		int offset = (dev_priv->buffers->offset - dev->agp->base
			      + buf->offset + start);
		int dwords = (end - start + 3) / sizeof(u32);

		/* Indirect buffer data must be an even number of
		 * dwords, so if we've been given an odd number we must
		 * pad the data with a Type-2 CP packet.
		 */
		if ( dwords & 1 ) {
			u32 *data = (u32 *)
				((char *)dev_priv->buffers->handle
				 + buf->offset + start);
			data[dwords++] = RADEON_CP_PACKET2;
		}

		DRM_DEBUG( "indirect: offset=0x%x dwords=%d\n",
			   offset, dwords );

		if ( 0 ) {
			u32 *data = (u32 *)
				((char *)dev_priv->buffers->handle
				 + buf->offset + start);
			int i;
			DRM_INFO( "data = %p\n", data );
			for ( i = 0 ; i < dwords ; i++ ) {
				DRM_INFO( "data[0x%x] = 0x%08x\n",
					  i, data[i] );
			}
		}

		buf_priv->dispatched = 1;

		/* Fire off the indirect buffer */
		BEGIN_RING( 3 );

		OUT_RING( CP_PACKET0( RADEON_CP_IB_BASE, 1 ) );
		OUT_RING( offset );
		OUT_RING( dwords );

		ADVANCE_RING();
	}

	if ( buf_priv->discard ) {
		buf_priv->age = dev_priv->sarea_priv->last_dispatch;

		/* Emit the indirect buffer age */
		BEGIN_RING( 2 );

		OUT_RING( CP_PACKET0( RADEON_LAST_DISPATCH_REG, 0 ) );
		OUT_RING( buf_priv->age );

		ADVANCE_RING();

		buf->pending = 1;
		/* FIXME: Check dispatched field */
		buf_priv->dispatched = 0;
	}

	dev_priv->sarea_priv->last_dispatch++;

#if 0
	if ( dev_priv->submit_age == RADEON_MAX_VB_AGE ) {
		ret = radeon_do_cp_idle( dev_priv );
		if ( ret < 0 ) return ret;
		dev_priv->submit_age = 0;
		radeon_freelist_reset( dev );
	}
#endif
}

static void radeon_cp_dispatch_indices( drm_device_t *dev,
					drm_buf_t *buf,
					int start, int end )
{
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_buf_priv_t *buf_priv = buf->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;

	int vertsize = sarea_priv->vertsize;
	int format = sarea_priv->vc_format;
	int index = buf->idx;
	int offset = dev_priv->buffers->offset - dev->agp->base;
	int prim = buf_priv->prim;

	u32 *data;

	int dwords;
	int i = 0;
	RING_LOCALS;
	DRM_DEBUG( "%s: start=%d end=%d\n", __FUNCTION__, start, end );

	radeon_update_ring_snapshot( dev_priv );

	if ( 0 )
		radeon_print_dirty( "dispatch_indices", sarea_priv->dirty );

	if ( start != end ) {
		buf_priv->dispatched = 1;

		if ( sarea_priv->dirty & ~RADEON_UPLOAD_CLIPRECTS ) {
			radeon_emit_state( dev_priv );
		}

		/* Adjust start offset to include packet header
		 */
		start -= RADEON_INDEX_PRIM_OFFSET;
		dwords = (end - start + 3) / sizeof(u32);

		data = (u32 *)((char *)dev_priv->buffers->handle
			       + buf->offset + start);

		data[0] = CP_PACKET3( RADEON_3D_RNDR_GEN_INDX_PRIM, dwords-2 );

		data[1] = offset + dev_priv->agp_vm_start;
		data[2] = RADEON_MAX_VB_VERTS;
		data[3] = format;
		data[4] = (prim | RADEON_CP_VC_CNTL_PRIM_WALK_IND |
			   (RADEON_MAX_VB_VERTS << 16));

		if ( (end - start) & 0x3 ) {
			data[dwords-1] &= 0x0000ffff;
		}

		if ( 0 ) {
			int i;
			DRM_INFO( "data = %p\n", data );
			for ( i = 0 ; i < dwords ; i++ ) {
				DRM_INFO( "data[0x%x] = 0x%08x\n",
					  i, data[i] );
			}
		}

		do {
			/* Emit the next set of up to three cliprects */
			if ( i < sarea_priv->nbox ) {
				radeon_emit_clip_rects( dev_priv,
						      &sarea_priv->boxes[i],
						      sarea_priv->nbox - i );
			}

			radeon_cp_dispatch_indirect( dev, buf, start, end );

			i += 3;
		} while ( i < sarea_priv->nbox );
	}

	if ( buf_priv->discard ) {
		buf_priv->age = dev_priv->sarea_priv->last_dispatch;

		/* Emit the vertex buffer age */
		BEGIN_RING( 2 );

		OUT_RING( CP_PACKET0( RADEON_LAST_DISPATCH_REG, 0 ) );
		OUT_RING( buf_priv->age );

		ADVANCE_RING();

		buf->pending = 1;
		/* FIXME: Check dispatched field */
		buf_priv->dispatched = 0;
	}

	dev_priv->sarea_priv->last_dispatch++;

#if 0
	if ( dev_priv->submit_age == RADEON_MAX_VB_AGE ) {
		ret = radeon_do_cp_idle( dev_priv );
		if ( ret < 0 ) return ret;
		dev_priv->submit_age = 0;
		radeon_freelist_reset( dev );
	}
#endif

	sarea_priv->dirty &= ~RADEON_UPLOAD_CLIPRECTS;
	sarea_priv->nbox = 0;
}

static int radeon_cp_dispatch_blit( drm_device_t *dev,
				    int offset, int pitch, int format,
				    drm_radeon_blit_rect_t *rects, int count )
{
#if 0
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_buf_priv_t *buf_priv;
	drm_radeon_blit_rect_t *rect;
	u32 *data;
	int dword_shift, dwords;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	radeon_update_ring_snapshot( dev_priv );

	/* The compiler won't optimize away a division by a variable,
	 * even if the only legal values are powers of two.  Thus, we'll
	 * use a shift instead.
	 */
	switch ( format ) {
	case RADEON_DATATYPE_ARGB1555:
	case RADEON_DATATYPE_RGB565:
	case RADEON_DATATYPE_ARGB4444:
		dword_shift = 1;
		break;
	case RADEON_DATATYPE_ARGB8888:
		dword_shift = 0;
		break;
	default:
		DRM_ERROR( "invalid blit format %d\n", format );
		return -EINVAL;
	}

	/* Flush the pixel cache, and mark the contents as Read Invalid.
	 * This ensures no pixel data gets mixed up with the texture
	 * data from the host data blit, otherwise part of the texture
	 * image may be corrupted.
	 */
	BEGIN_RING( 2 );

	OUT_RING( CP_PACKET0( RADEON_PC_GUI_CTLSTAT, 0 ) );
	OUT_RING( RADEON_PC_RI_GUI | RADEON_PC_FLUSH_GUI );

	ADVANCE_RING();

	/* Dispatch each of the indirect buffers.
	 */
	for ( i = 0 ; i < count ; i++ ) {
		rect = &rects[i];
		buf = dma->buflist[rect->index];
		buf_priv = buf->dev_private;

		if ( buf->pid != current->pid ) {
			DRM_ERROR( "process %d using buffer owned by %d\n",
				   current->pid, buf->pid );
			return -EINVAL;
		}
		if ( buf->pending ) {
			DRM_ERROR( "sending pending buffer %d\n",
				   rect->index );
			return -EINVAL;
		}

		buf_priv->discard = 1;

		dwords = (rect->width * rect->height) >> dword_shift;

		data = (u32 *)((char *)dev_priv->buffers->handle
			       + buf->offset);

		data[0] = CP_PACKET3( RADEON_CNTL_HOSTDATA_BLT, dwords + 6 );
		data[1] = ( RADEON_GMC_DST_PITCH_OFFSET_CNTL
			    | RADEON_GMC_BRUSH_NONE
			    | (format << 8)
			    | RADEON_GMC_SRC_DATATYPE_COLOR
			    | RADEON_ROP3_S
			    | RADEON_DP_SRC_SOURCE_HOST_DATA
			    | RADEON_GMC_CLR_CMP_CNTL_DIS
			    | RADEON_GMC_WR_MSK_DIS );

		data[2] = (pitch << 21) | (offset >> 5);
		data[3] = 0xffffffff;
		data[4] = 0xffffffff;
		data[5] = (rect->y << 16) | rect->x;
		data[6] = (rect->height << 16) | rect->width;
		data[7] = dwords;

		buf->used = (dwords + 8) * sizeof(u32);

		radeon_cp_dispatch_indirect( dev, buf, 0, buf->used );
	}

	/* Flush the pixel cache after the blit completes.  This ensures
	 * the texture data is written out to memory before rendering
	 * continues.
	 */
	BEGIN_RING( 2 );

	OUT_RING( CP_PACKET0( RADEON_PC_GUI_CTLSTAT, 0 ) );
	OUT_RING( RADEON_PC_FLUSH_GUI );

	ADVANCE_RING();
#endif

	return 0;
}


/* ================================================================
 *
 */

int radeon_cp_clear( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_radeon_clear_t clear;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "radeon_cp_clear called without lock held\n" );
		return -EINVAL;
	}

	if ( copy_from_user( &clear, (drm_radeon_clear_t *) arg,
			     sizeof(clear) ) )
		return -EFAULT;

	if ( sarea_priv->nbox > RADEON_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = RADEON_NR_SAREA_CLIPRECTS;

	radeon_cp_dispatch_clear( dev, clear.flags,
				 clear.x, clear.y, clear.w, clear.h,
				 clear.clear_color, clear.clear_depth,
				 clear.color_mask, clear.depth_mask );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= ( RADEON_UPLOAD_CONTEXT
					 | RADEON_UPLOAD_MASKS );

	return 0;
}

int radeon_cp_swap( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_radeon_sarea_t *sarea_priv = dev_priv->sarea_priv;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "radeon_cp_swap called without lock held\n" );
		return -EINVAL;
	}

	if ( sarea_priv->nbox > RADEON_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = RADEON_NR_SAREA_CLIPRECTS;

	radeon_cp_dispatch_swap( dev );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= ( RADEON_UPLOAD_CONTEXT
					 | RADEON_UPLOAD_MASKS );

	return 0;
}

int radeon_cp_vertex( struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_buf_priv_t *buf_priv;
	drm_radeon_vertex_t vertex;

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}
	if ( !dev_priv || dev_priv->is_pci ) {
		DRM_ERROR( "%s called with a PCI card\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &vertex, (drm_radeon_vertex_t *)arg,
			     sizeof(vertex) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d count=%d discard=%d\n",
		   __FUNCTION__, current->pid,
		   vertex.idx, vertex.count, vertex.discard );

	if ( vertex.idx < 0 || vertex.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   vertex.idx, dma->buf_count - 1 );
		return -EINVAL;
	}
	if ( vertex.prim < 0 ||
	     vertex.prim > RADEON_CP_VC_CNTL_PRIM_TYPE_3VRT_LINE_LIST ) {
		DRM_ERROR( "buffer prim %d\n", vertex.prim );
		return -EINVAL;
	}

	buf = dma->buflist[vertex.idx];
	buf_priv = buf->dev_private;

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}
	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", vertex.idx );
		return -EINVAL;
	}

	buf->used = vertex.count;
	buf_priv->prim = vertex.prim;
	buf_priv->discard = vertex.discard;

	radeon_cp_dispatch_vertex( dev, buf );

	return 0;
}

int radeon_cp_indices( struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_radeon_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_radeon_buf_priv_t *buf_priv;
	drm_radeon_indices_t elts;

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}
	if ( !dev_priv || dev_priv->is_pci ) {
		DRM_ERROR( "%s called with a PCI card\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &elts, (drm_radeon_indices_t *)arg,
			     sizeof(elts) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d start=%d end=%d discard=%d\n",
		   __FUNCTION__, current->pid,
		   elts.idx, elts.start, elts.end, elts.discard );

	if ( elts.idx < 0 || elts.idx >= dma->buf_count ) {
		DRM_ERROR( "buffer index %d (of %d max)\n",
			   elts.idx, dma->buf_count - 1 );
		return -EINVAL;
	}
	if ( elts.prim < 0 ||
	     elts.prim > RADEON_CP_VC_CNTL_PRIM_TYPE_3VRT_LINE_LIST ) {
		DRM_ERROR( "buffer prim %d\n", elts.prim );
		return -EINVAL;
	}

	buf = dma->buflist[elts.idx];
	buf_priv = buf->dev_private;

	if ( buf->pid != current->pid ) {
		DRM_ERROR( "process %d using buffer owned by %d\n",
			   current->pid, buf->pid );
		return -EINVAL;
	}
	if ( buf->pending ) {
		DRM_ERROR( "sending pending buffer %d\n", elts.idx );
		return -EINVAL;
	}
	if ( (buf->offset + elts.start) & 0x3 ) {
		DRM_ERROR( "buffer start 0x%x\n", buf->offset + elts.start );
		return -EINVAL;
	}

	buf_priv->prim = elts.prim;
	buf_priv->discard = elts.discard;

	radeon_cp_dispatch_indices( dev, buf, elts.start, elts.end );

	return 0;
}

int radeon_cp_blit( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_radeon_blit_t blit;

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "%s called without lock held\n", __FUNCTION__ );
		return -EINVAL;
	}

	if ( copy_from_user( &blit, (drm_radeon_blit_t *)arg,
			     sizeof(blit) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d count=%d\n",
		   __FUNCTION__, current->pid, blit.count );

	if ( blit.count < 0 || blit.count > dma->buf_count ) {
		DRM_ERROR( "sending %d buffers (of %d max)\n",
			   blit.count, dma->buf_count );
		return -EINVAL;
	}

	if ( copy_from_user( &rects, blit.rects,
			     blit.count * sizeof(drm_radeon_blit_rect_t) ) )
		return -EFAULT;

	return radeon_cp_dispatch_blit( dev, blit.offset, blit.pitch,
					blit.format, rects, blit.count );
}
