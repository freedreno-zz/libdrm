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

	BEGIN_RING( 3 );

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

	BEGIN_RING( 3 );

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
}


/* ================================================================
 * CCE command dispatch functions
 */

static void r128_cce_dispatch_swap( drm_device_t *dev )
{

}

static void r128_cce_dispatch_clear( drm_device_t *dev, int flags,
				     unsigned int clear_color,
				     unsigned int clear_depth )
{

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
	int offset = dev_priv->agp_vertbufs->offset
		+ buf->offset - dev->agp->base;
	int size = buf->used / (vertsize * sizeof(u32));
	int prim;
	int i = 0;
	RING_LOCALS;
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	DRM_DEBUG( "vertex buffer index = %d\n", index );
	DRM_DEBUG( "vertex buffer offset = 0x%x\n", offset );
	DRM_DEBUG( "vertex buffer size = %d bytes\n", size );
	DRM_DEBUG( "vertex size = %d\n", vertsize );
	DRM_DEBUG( "vertex format = 0x%x\n", format );

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

		OUT_RING( CCE_PACKET0( R128_VB_AGE_REG, 0 ) );
		OUT_RING( dev_priv->submit_age );

		ADVANCE_RING();

		buf->pending = 1;

		/* FIXME: Check dispatched field */
		buf_priv->dispatched = 0;
		buf_priv->age = dev_priv->submit_age;
	}

	dev_priv->submit_age++;

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
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	buf = r128_freelist_get( dev );
	if ( !buf ) return;

	buf->pid = current->pid;

	v->index = buf->idx;
	v->granted = 1;
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
	DRM_DEBUG( "%s\n", __FUNCTION__ );

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
	DRM_DEBUG( "%d: index=%d used=%d flags=%s%s%s\n",
		   current->pid, vertex.index, vertex.used,
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

	DRM_DEBUG( "%d: returning, index=%d\n", current->pid, vertex.index );
	if ( copy_to_user( (drm_r128_vertex_t *)arg, &vertex,
			   sizeof(vertex) ) )
		return -EFAULT;

	return 0;
}
