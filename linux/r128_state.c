/* r128_state.c -- State support for r128 -*- linux-c -*-
 * Created: Thu Jan 27 02:53:43 2000 by gareth@valinux.com
 *
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
 * Authors: Gareth Hughes <gareth@valinux.com>
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "r128_drv.h"
#include "drm.h"


/* ================================================================
 * CCE hardware state programming functions
 */

static void r128_emit_clip_rects( drm_r128_private_t *dev_priv,
				  drm_clip_rect_t *boxes, int count )
{
	unsigned int aux_sc_cntl = 0x00000000;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 17 );

	if ( count >= 1 ) {
		OUT_RING( CCE_PACKET0( R128_AUX1_SC_LEFT, 3 ) );
		OUT_RING( boxes[0].x1 );
		OUT_RING( boxes[0].x2 - 1 );
		OUT_RING( boxes[0].y1 );
		OUT_RING( boxes[0].y2 - 1 );

		aux_sc_cntl |= (R128_AUX1_SC_EN | R128_AUX1_SC_MODE_OR);
	}

	if ( count >= 2 ) {
		OUT_RING( CCE_PACKET0( R128_AUX2_SC_LEFT, 3 ) );
		OUT_RING( boxes[1].x1 );
		OUT_RING( boxes[1].x2 - 1 );
		OUT_RING( boxes[1].y1 );
		OUT_RING( boxes[1].y2 - 1 );

		aux_sc_cntl |= (R128_AUX2_SC_EN | R128_AUX2_SC_MODE_OR);
	}
	if ( count >= 3 ) {
		OUT_RING( CCE_PACKET0( R128_AUX3_SC_LEFT, 3 ) );
		OUT_RING( boxes[2].x1 );
		OUT_RING( boxes[2].x2 - 1 );
		OUT_RING( boxes[2].y1 );
		OUT_RING( boxes[2].y2 - 1 );

		aux_sc_cntl |= (R128_AUX3_SC_EN | R128_AUX3_SC_MODE_OR);
	}

	OUT_RING( CCE_PACKET0( R128_AUX_SC_CNTL, 0 ) );
	OUT_RING( aux_sc_cntl );

	ADVANCE_RING();
}

static inline void r128_emit_core( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 2 );

	OUT_RING( CCE_PACKET0( R128_SCALE_3D_CNTL, 0 ) );
	OUT_RING( ctx->scale_3d_cntl );

	ADVANCE_RING();
}

static inline void r128_emit_context( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 13 );

	OUT_RING( CCE_PACKET0( R128_DST_PITCH_OFFSET_C, 11 ) );
	OUT_RING( ctx->dst_pitch_offset_c );
	OUT_RING( ctx->dp_gui_master_cntl_c );
	OUT_RING( ctx->sc_top_left_c );
	OUT_RING( ctx->sc_bottom_right_c );
	OUT_RING( ctx->z_offset_c );
	OUT_RING( ctx->z_pitch_c );
	OUT_RING( ctx->z_sten_cntl_c );
	OUT_RING( ctx->tex_cntl_c );
	OUT_RING( ctx->misc_3d_state_cntl_reg );
	OUT_RING( ctx->texture_clr_cmp_clr_c );
	OUT_RING( ctx->texture_clr_cmp_msk_c );
	OUT_RING( ctx->fog_color_c );

	ADVANCE_RING();
}

static inline void r128_emit_setup( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 3 );

	OUT_RING( CCE_PACKET1( R128_SETUP_CNTL, R128_PM4_VC_FPU_SETUP ) );
	OUT_RING( ctx->setup_cntl );
	OUT_RING( ctx->pm4_vc_fpu_setup );

	ADVANCE_RING();
}

static inline void r128_emit_masks( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 5 );

	OUT_RING( CCE_PACKET0( R128_DP_WRITE_MASK, 0 ) );
	OUT_RING( ctx->dp_write_mask );

	OUT_RING( CCE_PACKET0( R128_STEN_REF_MASK_C, 1 ) );
	OUT_RING( ctx->sten_ref_mask_c );
	OUT_RING( ctx->plane_3d_mask_c );

	ADVANCE_RING();
}

static inline void r128_emit_window( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_context_regs_t *ctx = &sarea_priv->context_state;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 2 );

	OUT_RING( CCE_PACKET0( R128_WINDOW_XY_OFFSET, 0 ) );
	OUT_RING( ctx->window_xy_offset );

	ADVANCE_RING();
}

static inline void r128_emit_tex0( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_context_regs_t *ctx = &sarea_priv->context_state;
	drm_r128_texture_regs_t *tex = &sarea_priv->tex_state[0];
	int i;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 7 + R128_TEX_MAXLEVELS );

	OUT_RING( CCE_PACKET0( R128_PRIM_TEX_CNTL_C,
			       2 + R128_TEX_MAXLEVELS ) );
	OUT_RING( tex->tex_cntl );
	OUT_RING( tex->tex_combine_cntl );
	OUT_RING( ctx->tex_size_pitch_c );
	for ( i = 0 ; i < R128_TEX_MAXLEVELS ; i++ ) {
		OUT_RING( tex->tex_offset[i] );
	}

	OUT_RING( CCE_PACKET0( R128_CONSTANT_COLOR_C, 1 ) );
	OUT_RING( ctx->constant_color_c );
	OUT_RING( tex->tex_border_color );

	ADVANCE_RING();
}

static inline void r128_emit_tex1( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_texture_regs_t *tex = &sarea_priv->tex_state[1];
	int i;
	RING_LOCALS;
	DRM_DEBUG( "    %s\n", __FUNCTION__ );

	BEGIN_RING( 5 + R128_TEX_MAXLEVELS );

	OUT_RING( CCE_PACKET0( R128_SEC_TEX_CNTL_C,
			       1 + R128_TEX_MAXLEVELS ) );
	OUT_RING( tex->tex_cntl );
	OUT_RING( tex->tex_combine_cntl );
	for ( i = 0 ; i < R128_TEX_MAXLEVELS ; i++ ) {
		OUT_RING( tex->tex_offset[i] );
	}

	OUT_RING( CCE_PACKET0( R128_SEC_TEXTURE_BORDER_COLOR_C, 0 ) );
	OUT_RING( tex->tex_border_color );

	ADVANCE_RING();
}

static inline void r128_emit_state( drm_r128_private_t *dev_priv )
{
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	DRM_DEBUG( "%s: dirty=0x%08x\n", __FUNCTION__, dirty );

	if ( dirty & R128_UPLOAD_CORE ) {
		r128_emit_core( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_CORE;
	}

	if ( dirty & R128_UPLOAD_CONTEXT ) {
		r128_emit_context( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_CONTEXT;
	}

	if ( dirty & R128_UPLOAD_SETUP ) {
		r128_emit_setup( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_SETUP;
	}

	if ( dirty & R128_UPLOAD_MASKS ) {
		r128_emit_masks( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_MASKS;
	}

	if ( dirty & R128_UPLOAD_WINDOW ) {
		r128_emit_window( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_WINDOW;
	}

	if ( dirty & R128_UPLOAD_TEX0 ) {
		r128_emit_tex0( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_TEX0;
	}

	if ( dirty & R128_UPLOAD_TEX1 ) {
		r128_emit_tex1( dev_priv );
		sarea_priv->dirty &= ~R128_UPLOAD_TEX1;
	}

	/* Turn off the texture cache flushing */
	sarea_priv->context_state.tex_cntl_c &= ~R128_TEX_CACHE_FLUSH;

	sarea_priv->dirty &= ~(R128_UPLOAD_TEX0IMAGES |
			       R128_UPLOAD_TEX1IMAGES |
			       R128_REQUIRE_QUIESCENCE);
}


/* ================================================================
 * CCE command dispatch functions
 */

static void r128_print_dirty( const char *msg, unsigned int flags )
{
	DRM_DEBUG( "%s: (0x%x) %s%s%s%s%s%s%s%s%s\n",
		   msg,
		   flags,
		   (flags & R128_UPLOAD_CORE)        ? "core, " : "",
		   (flags & R128_UPLOAD_CONTEXT)     ? "context, " : "",
		   (flags & R128_UPLOAD_SETUP)       ? "setup, " : "",
		   (flags & R128_UPLOAD_TEX0)        ? "tex0, " : "",
		   (flags & R128_UPLOAD_TEX1)        ? "tex1, " : "",
		   (flags & R128_UPLOAD_MASKS)       ? "masks, " : "",
		   (flags & R128_UPLOAD_WINDOW)      ? "window, " : "",
		   (flags & R128_UPLOAD_CLIPRECTS)   ? "cliprects, " : "",
		   (flags & R128_REQUIRE_QUIESCENCE) ? "quiescence, " : "" );
}

static void r128_cce_dispatch_clear( drm_device_t *dev,
				     unsigned int flags,
				     int cx, int cy, int cw, int ch,
				     unsigned int clear_color,
				     unsigned int clear_depth,
				     unsigned int color_mask,
				     unsigned int depth_mask )
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	u32 fb_bpp, depth_bpp;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = R128_GMC_DST_16BPP;
		break;
	case 24:
		fb_bpp = R128_GMC_DST_24BPP;
		break;
	case 32:
	default:
		fb_bpp = R128_GMC_DST_32BPP;
		break;
	}
	switch ( dev_priv->depth_bpp ) {
	case 16:
		depth_bpp = R128_GMC_DST_16BPP;
		break;
	case 24:
		depth_bpp = R128_GMC_DST_32BPP;
		break;
	case 32:
		depth_bpp = R128_GMC_DST_32BPP;
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

		if ( flags & (R128_FRONT | R128_BACK) ) {
			BEGIN_RING( 7 );

			OUT_RING( CCE_PACKET0( R128_DP_WRITE_MASK, 0 ) );
			OUT_RING( color_mask );

			ADVANCE_RING();
		}

		if ( flags & R128_FRONT ) {
			int fx = x + dev_priv->front_x;
			int fy = y + dev_priv->front_y;

			DRM_DEBUG( "clear front: x=%d y=%d\n",
				   dev_priv->front_x, dev_priv->front_y );
			BEGIN_RING( 5 );

			OUT_RING( CCE_PACKET3( R128_CNTL_PAINT_MULTI, 3 ) );
			OUT_RING( R128_GMC_BRUSH_SOLID_COLOR
				  | fb_bpp
				  | R128_GMC_SRC_DATATYPE_COLOR
				  | R128_ROP3_P
				  | R128_GMC_CLR_CMP_CNTL_DIS
				  | R128_GMC_AUX_CLIP_DIS );
			OUT_RING( clear_color );
			OUT_RING( (fx << 16) | fy );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & R128_BACK ) {
			int bx = x + dev_priv->back_x;
			int by = y + dev_priv->back_y;

			DRM_DEBUG( "clear back: x=%d y=%d\n",
				   dev_priv->back_x, dev_priv->back_y );
			BEGIN_RING( 5 );

			OUT_RING( CCE_PACKET3( R128_CNTL_PAINT_MULTI, 3 ) );
			OUT_RING( R128_GMC_BRUSH_SOLID_COLOR
				  | fb_bpp
				  | R128_GMC_SRC_DATATYPE_COLOR
				  | R128_ROP3_P
				  | R128_GMC_CLR_CMP_CNTL_DIS
				  | R128_GMC_AUX_CLIP_DIS );
			OUT_RING( clear_color );
			OUT_RING( (bx << 16) | by );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}

		if ( flags & R128_DEPTH ) {
			int dx = x + dev_priv->depth_x;
			int dy = y + dev_priv->depth_y;

			DRM_DEBUG( "clear depth: x=%d y=%d\n",
				   dev_priv->depth_x, dev_priv->depth_y );
			BEGIN_RING( 7 );

			OUT_RING( CCE_PACKET0( R128_DP_WRITE_MASK, 0 ) );
			OUT_RING( depth_mask );

			OUT_RING( CCE_PACKET3( R128_CNTL_PAINT_MULTI, 3 ) );
			OUT_RING( R128_GMC_BRUSH_SOLID_COLOR
				  | depth_bpp
				  | R128_GMC_SRC_DATATYPE_COLOR
				  | R128_ROP3_P
				  | R128_GMC_CLR_CMP_CNTL_DIS
				  | R128_GMC_AUX_CLIP_DIS );
			OUT_RING( clear_depth );
			OUT_RING( (dx << 16) | dy );
			OUT_RING( (w << 16) | h );

			ADVANCE_RING();
		}
	}
}

static void r128_cce_dispatch_swap( drm_device_t *dev )
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	u32 fb_bpp;
	int i;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	switch ( dev_priv->fb_bpp ) {
	case 16:
		fb_bpp = R128_GMC_DST_16BPP;
		break;
	case 24:
		fb_bpp = R128_GMC_DST_24BPP;
		break;
	case 32:
	default:
		fb_bpp = R128_GMC_DST_32BPP;
		break;
	}

	for ( i = 0 ; i < nbox ; i++ ) {
		int fx = pbox[i].x1;
		int fy = pbox[i].y1;
		int fw = pbox[i].x2 - fx;
		int fh = pbox[i].y2 - fy;
		int bx = fx + dev_priv->back_x;
		int by = fy + dev_priv->back_y;

		fx += dev_priv->front_x;
		fy += dev_priv->front_x;

		BEGIN_RING( 5 );

		OUT_RING( CCE_PACKET3( R128_CNTL_BITBLT_MULTI, 3 ) );
		OUT_RING( R128_GMC_BRUSH_NONE
			  | R128_GMC_SRC_DATATYPE_COLOR
			  | R128_DP_SRC_SOURCE_MEMORY
			  | fb_bpp
			  | R128_ROP3_S
			  | R128_GMC_CLR_CMP_CNTL_DIS
			  | R128_GMC_AUX_CLIP_DIS
			  | R128_GMC_WR_MSK_DIS );

		OUT_RING( (bx << 16) | by );
		OUT_RING( (fx << 16) | fy );
		OUT_RING( (fw << 16) | fh );

		ADVANCE_RING();
	}

	/* Increment the frame counter.  The client-side 3D driver must
	 * throttle the framerate by waiting for this value before
	 * performing the swapbuffer ioctl.
	 */
	dev_priv->sarea_priv->last_frame++;

	BEGIN_RING( 2 );

	OUT_RING( CCE_PACKET0( R128_LAST_FRAME_REG, 0 ) );
	OUT_RING( dev_priv->sarea_priv->last_frame );

	ADVANCE_RING();
}

static void r128_cce_dispatch_vertex( drm_device_t *dev,
				      drm_buf_t *buf )
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_buf_priv_t *buf_priv = buf->dev_private;
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int vertsize = sarea_priv->vertsize;
	int format = sarea_priv->vc_format;
	int index = buf->idx;
	int offset = dev_priv->vertex_buffers->offset
		+ buf->offset - dev->agp->base;
	int size = buf->used / (vertsize * sizeof(u32));
	int prim;
	int i = 0;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	DRM_DEBUG( "vertex buffer index = %d\n", index );
	DRM_DEBUG( "vertex buffer offset = 0x%x\n", offset );
	DRM_DEBUG( "vertex buffer size = %d vertices, %d bytes\n",
		   size, buf->used );
	DRM_DEBUG( "vertex size = %d\n", vertsize );
	DRM_DEBUG( "vertex format = 0x%x\n", format );

	r128_update_ring_snapshot( dev_priv );

	if ( 0 )
		r128_print_dirty( "dispatch_vertex", sarea_priv->dirty );

	prim = R128_CCE_VC_CNTL_PRIM_TYPE_TRI_LIST;

	if ( buf->used ) {
		buf_priv->dispatched = 1;

		if ( sarea_priv->dirty & ~R128_UPLOAD_CLIPRECTS ) {
			r128_emit_state( dev_priv );
		}

		do {
			/* Emit the next set of up to three cliprects */
			if ( i < sarea_priv->nbox ) {
				r128_emit_clip_rects( dev_priv,
						      &sarea_priv->boxes[i],
						      sarea_priv->nbox - i );
			}

			/* Emit the vertex buffer rendering commands */
			BEGIN_RING( 5 );

			OUT_RING( CCE_PACKET3( R128_3D_RNDR_GEN_INDX_PRIM, 3 ) );
			OUT_RING( offset );
			OUT_RING( size );
			OUT_RING( format );
			OUT_RING( prim | R128_CCE_VC_CNTL_PRIM_WALK_LIST |
				  (size << R128_CCE_VC_CNTL_NUM_SHIFT) );

			ADVANCE_RING();

			i += 3;
		} while ( i < sarea_priv->nbox );
	}

	if ( buf_priv->discard ) {
		/* Emit the vertex buffer age */
		BEGIN_RING( 2 );

		OUT_RING( CCE_PACKET0( R128_LAST_VB_REG, 0 ) );
		OUT_RING( dev_priv->sarea_priv->last_dispatch );

		ADVANCE_RING();

		buf->pending = 1;

		/* FIXME: Check dispatched field */
		buf_priv->dispatched = 0;
		buf_priv->age = dev_priv->sarea_priv->last_dispatch;
	}

	dev_priv->sarea_priv->last_dispatch++;

#if 0
	if ( dev_priv->submit_age == R128_MAX_VB_AGE ) {
		ret = r128_do_cce_idle( dev_priv );
		if ( ret < 0 ) return ret;
		dev_priv->submit_age = 0;
		r128_freelist_reset( dev );
	}
#endif
}


/* ================================================================
 *
 */

static void r128_get_vertex_buffer( drm_device_t *dev, drm_r128_vertex_t *v )
{
	drm_buf_t *buf;

	buf = r128_freelist_get( dev );
	if ( !buf ) return;

	buf->pid = current->pid;

	v->index = buf->idx;
	v->granted = 1;
}

int r128_cce_clear( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_r128_clear_t clear;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "r128_cce_clear called without lock held\n" );
		return -EINVAL;
	}

	if ( copy_from_user( &clear, (drm_r128_clear_t *) arg,
			     sizeof(clear) ) )
		return -EFAULT;

	if ( sarea_priv->nbox > R128_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = R128_NR_SAREA_CLIPRECTS;

	r128_cce_dispatch_clear( dev, clear.flags,
				 clear.x, clear.y, clear.w, clear.h,
				 clear.clear_color, clear.clear_depth,
				 clear.color_mask, clear.depth_mask );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= R128_UPLOAD_CONTEXT | R128_UPLOAD_MASKS;

	return 0;
}

int r128_cce_swap( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_r128_sarea_t *sarea_priv = dev_priv->sarea_priv;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "r128_cce_swap called without lock held\n" );
		return -EINVAL;
	}

	if ( sarea_priv->nbox > R128_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = R128_NR_SAREA_CLIPRECTS;

	r128_cce_dispatch_swap( dev );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= R128_UPLOAD_CONTEXT | R128_UPLOAD_MASKS;

	return 0;
}

int r128_cce_vertex( struct inode *inode, struct file *filp,
		     unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_r128_buf_priv_t *buf_priv;
	drm_r128_vertex_t vertex;

	if ( !_DRM_LOCK_IS_HELD( dev->lock.hw_lock->lock ) ||
	     dev->lock.pid != current->pid ) {
		DRM_ERROR( "r128_cce_vertex called without lock held\n" );
		return -EINVAL;
	}
	if ( !dev_priv || dev_priv->is_pci ) {
		DRM_ERROR( "r128_cce_vertex called with a PCI card\n" );
		return -EINVAL;
	}

	if ( copy_from_user( &vertex, (drm_r128_vertex_t *)arg,
			     sizeof(vertex) ) )
		return -EFAULT;

	DRM_DEBUG( "%s: pid=%d index=%d used=%d flags=%s%s%s\n",
		   __FUNCTION__, current->pid, vertex.index, vertex.used,
		   ( vertex.send ) ?    "S" : ".",
		   ( vertex.discard ) ? "D" : ".",
		   ( vertex.request ) ? "R" : "." );

	/* You can send us buffers.
	 */
	if ( vertex.send || vertex.discard ) {
		if ( vertex.index < 0 || vertex.index >= dma->buf_count ) {
			DRM_ERROR( "buffer index %d (of %d max)\n",
				   vertex.index, dma->buf_count - 1 );
			return -EINVAL;
		}

		buf = dma->buflist[vertex.index];
		buf_priv = buf->dev_private;

		if ( buf->pid != current->pid ) {
			DRM_ERROR( "process %d using buffer owned by %d\n",
				   current->pid, buf->pid );
			return -EINVAL;
		}
		if ( buf->pending ) {
			DRM_ERROR( "sending pending buffer %d\n",
				   vertex.index );
			return -EINVAL;
		}

		buf->used = vertex.used;
		buf_priv->discard = vertex.discard;

		r128_cce_dispatch_vertex( dev, buf );
	}

	/* And we'll give you new ones too.
	 */
	if ( vertex.request ) {
		r128_get_vertex_buffer( dev, &vertex );
	}

	DRM_DEBUG( "%s: returning, pid=%d index=%d\n",
		   __FUNCTION__, current->pid, vertex.index );
	if ( copy_to_user( (drm_r128_vertex_t *)arg, &vertex,
			   sizeof(vertex) ) )
		return -EFAULT;

	return 0;
}
