/* mga_state.c -- State support for MGA G200/G400 -*- linux-c -*-
 * Created: Thu Jan 27 02:53:43 2000 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Jeff Hartmann <jhartmann@valinux.com>
 *   Keith Whitwell <keithw@valinux.com>
 *
 * Rewritten by:
 *   Gareth Hughes <gareth@valinux.com>
 */

#define __NO_VERSION__
#include "mga.h"
#include "drmP.h"
#include "mga_drv.h"
#include "drm.h"

#include <linux/interrupt.h>	/* For task queue support */


/* If you change the functions to set state, PLEASE
 * change these values
 */

#define MGAEMITCLIP_SIZE	10
#define MGAEMITCTX_SIZE		20
#define MGAG200EMITTEX_SIZE	20
#define MGAG400EMITTEX0_SIZE	30
#define MGAG400EMITTEX1_SIZE	25
#define MGAG400EMITPIPE_SIZE	50
#define MGAG200EMITPIPE_SIZE	15

#define MAX_STATE_SIZE ((MGAEMITCLIP_SIZE * MGA_NR_SAREA_CLIPRECTS) + \
			MGAEMITCTX_SIZE + MGAG400EMITTEX0_SIZE + \
			MGAG400EMITTEX1_SIZE + MGAG400EMITPIPE_SIZE)


/* ================================================================
 * DMA hardware state programming functions
 */

static void mga_emit_clip_rect( drm_mga_private_t *dev_priv,
				drm_clip_rect_t *box )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	unsigned int pitch = dev_priv->front_pitch / dev_priv->fb_cpp;
	DMA_LOCALS;

	return;

	BEGIN_DMA( 2 );

	/* Force reset of DWGCTL on G400 (eliminates clip disable bit).
	 */
	if ( dev_priv->chipset == MGA_CARD_TYPE_G400 ) {
		DMA_BLOCK( MGA_DWGCTL,		ctx->dwgctl,
			   MGA_LEN + MGA_EXEC,	0x80000000,
			   MGA_DWGCTL,		ctx->dwgctl,
			   MGA_LEN + MGA_EXEC,	0x80000000 );
	}
	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_CXBNDRY,	(box->x2 << 16) | box->x1,
		   MGA_YTOP,	box->y1 * pitch,
		   MGA_YBOT,	box->y2 * pitch );

	ADVANCE_DMA();
}

static inline void mga_g200_emit_context( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	DMA_LOCALS;

	BEGIN_DMA( 2 );

	DMA_BLOCK( MGA_DSTORG,		ctx->dstorg,
		   MGA_MACCESS,		ctx->maccess,
		   MGA_PLNWT,		ctx->plnwt,
		   MGA_DWGCTL,		ctx->dwgctl );

	DMA_BLOCK( MGA_ALPHACTRL,	ctx->alphactrl,
		   MGA_FOGCOL,		ctx->fogcolor,
		   MGA_WFLAG,		ctx->wflag,
		   MGA_ZORG,		dev_priv->depth_offset );

	DMA_BLOCK( MGA_FCOL,		ctx->fcol,
		   MGA_DMAPAD,		0x00000000,
		   MGA_DMAPAD,		0x00000000,
		   MGA_DMAPAD,		0x00000000 );

	ADVANCE_DMA();
}

static inline void mga_g400_emit_context( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	DMA_LOCALS;

	BEGIN_DMA( 4 );

	DMA_BLOCK( MGA_DSTORG,		ctx->dstorg,
		   MGA_MACCESS,		ctx->maccess,
		   MGA_PLNWT,		ctx->plnwt,
		   MGA_DWGCTL,		ctx->dwgctl );

	DMA_BLOCK( MGA_ALPHACTRL,	ctx->alphactrl,
		   MGA_FOGCOL,		ctx->fogcolor,
		   MGA_WFLAG,		ctx->wflag,
		   MGA_ZORG,		dev_priv->depth_offset );

	DMA_BLOCK( MGA_WFLAG1,		ctx->wflag,
		   MGA_TDUALSTAGE0,	ctx->tdualstage0,
		   MGA_TDUALSTAGE1,	ctx->tdualstage1,
		   MGA_FCOL,		ctx->fcol );

	DMA_BLOCK( MGA_STENCIL,		ctx->stencil,
		   MGA_STENCILCTL,	ctx->stencilctl,
		   MGA_DMAPAD,		0x00000000,
		   MGA_DMAPAD,		0x00000000 );

	ADVANCE_DMA();
}

static inline void mga_g200_emit_tex0( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[0];
	DMA_LOCALS;

	BEGIN_DMA( 4 );

	DMA_BLOCK( MGA_TEXCTL2,		tex->texctl2,
		   MGA_TEXCTL,		tex->texctl,
		   MGA_TEXFILTER,	tex->texfilter,
		   MGA_TEXBORDERCOL,	tex->texbordercol );

	DMA_BLOCK( MGA_TEXORG,		tex->texorg,
		   MGA_TEXORG1,		tex->texorg1,
		   MGA_TEXORG2,		tex->texorg2,
		   MGA_TEXORG3,		tex->texorg3 );

	DMA_BLOCK( MGA_TEXORG4,		tex->texorg4,
		   MGA_TEXWIDTH,	tex->texwidth,
		   MGA_TEXHEIGHT,	tex->texheight,
		   MGA_WR24,		tex->texwidth );

	DMA_BLOCK( MGA_WR34,		tex->texheight,
		   MGA_TEXTRANS,	0x0000ffff,
		   MGA_TEXTRANSHIGH,	0x0000ffff,
		   MGA_DMAPAD,		0x00000000 );

	ADVANCE_DMA();
}

static inline void mga_g400_emit_tex0( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[0];
	DMA_LOCALS;

	BEGIN_DMA( 6 );

	DMA_BLOCK( MGA_TEXCTL2,		tex->texctl2 | MGA_G400_TC2_MAGIC,
		   MGA_TEXCTL,		tex->texctl,
		   MGA_TEXFILTER,	tex->texfilter,
		   MGA_TEXBORDERCOL,	tex->texbordercol );

	DMA_BLOCK( MGA_TEXORG,		tex->texorg,
		   MGA_TEXORG1,		tex->texorg1,
		   MGA_TEXORG2,		tex->texorg2,
		   MGA_TEXORG3,		tex->texorg3 );

	DMA_BLOCK( MGA_TEXORG4,		tex->texorg4,
		   MGA_TEXWIDTH,	tex->texwidth,
		   MGA_TEXHEIGHT,	tex->texheight,
		   MGA_WR49,		0x00000000 );

	DMA_BLOCK( MGA_WR57,		0x00000000,
		   MGA_WR53,		0x00000000,
		   MGA_WR61,		0x00000000,
		   MGA_WR52,		MGA_G400_WR_MAGIC );

	DMA_BLOCK( MGA_WR60,		MGA_G400_WR_MAGIC,
		   MGA_WR54,		tex->texwidth | MGA_G400_WR_MAGIC,
		   MGA_WR62,		tex->texheight | MGA_G400_WR_MAGIC,
		   MGA_DMAPAD,		0x00000000 );

	DMA_BLOCK( MGA_DMAPAD,		0x00000000,
		   MGA_DMAPAD,		0x00000000,
		   MGA_TEXTRANS,	0x0000ffff,
		   MGA_TEXTRANSHIGH,	0x0000ffff );

	ADVANCE_DMA();
}

static inline void mga_g400_emit_tex1( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[1];
	DMA_LOCALS;

	BEGIN_DMA( 6 );

	DMA_BLOCK( MGA_TEXCTL2,		(tex->texctl2 |
					 MGA_MAP1_ENABLE |
					 MGA_G400_TC2_MAGIC),
		   MGA_TEXCTL,		tex->texctl,
		   MGA_TEXFILTER,	tex->texfilter,
		   MGA_TEXBORDERCOL,	tex->texbordercol );

	DMA_BLOCK( MGA_TEXORG,		tex->texorg,
		   MGA_TEXORG1,		tex->texorg1,
		   MGA_TEXORG2,		tex->texorg2,
		   MGA_TEXORG3,		tex->texorg3 );

	DMA_BLOCK( MGA_TEXORG4,		tex->texorg4,
		   MGA_TEXWIDTH,	tex->texwidth,
		   MGA_TEXHEIGHT,	tex->texheight,
		   MGA_WR49,		0x00000000 );

	DMA_BLOCK( MGA_WR57,		0x00000000,
		   MGA_WR53,		0x00000000,
		   MGA_WR61,		0x00000000,
		   MGA_WR52,		tex->texwidth | MGA_G400_WR_MAGIC );

	DMA_BLOCK( MGA_WR60,		tex->texheight | MGA_G400_WR_MAGIC,
		   MGA_TEXTRANS,	0x0000ffff,
		   MGA_TEXTRANSHIGH,	0x0000ffff,
		   MGA_TEXCTL2,		tex->texctl2 | MGA_G400_TC2_MAGIC );

	ADVANCE_DMA();
}

static inline void mga_g200_emit_pipe( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->warp_pipe;
	DMA_LOCALS;

	BEGIN_DMA( 3 );

	DMA_BLOCK( MGA_WIADDR,	MGA_WMODE_SUSPEND,
		   MGA_WVRTXSZ,	0x00000007,
		   MGA_WFLAG,	0x00000000,
		   MGA_WR24,	0x00000000 );

	DMA_BLOCK( MGA_WR25,	0x00000100,
		   MGA_WR34,	0x00000000,
		   MGA_WR42,	0x0000ffff,
		   MGA_WR60,	0x0000ffff );

	/* Padding required to to hardware bug.
	 */
	DMA_BLOCK( MGA_DMAPAD,	0xffffffff,
		   MGA_DMAPAD,	0xffffffff,
		   MGA_DMAPAD,	0xffffffff,
		   MGA_WIADDR,	(dev_priv->warp_pipe_phys[pipe] |
				 MGA_WMODE_START |
				 MGA_WAGP_ENABLE) );

	ADVANCE_DMA();
}

static inline void mga_g400_emit_pipe( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->warp_pipe;
	DMA_LOCALS;

	BEGIN_DMA( 10 );

	DMA_BLOCK( MGA_WIADDR2,	MGA_WMODE_SUSPEND,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000 );

	if ( pipe & MGA_T2 ) {
		DMA_BLOCK( MGA_WVRTXSZ,		0x00001e09,
			   MGA_DMAPAD,		0x00000000,
			   MGA_DMAPAD,		0x00000000,
			   MGA_DMAPAD,		0x00000000 );

		DMA_BLOCK( MGA_WACCEPTSEQ,	0x00000000,
			   MGA_WACCEPTSEQ,	0x00000000,
			   MGA_WACCEPTSEQ,	0x00000000,
			   MGA_WACCEPTSEQ,	0x1e000000 );
	} else {
		if ( dev_priv->warp_pipe & MGA_T2 ) {
			/* Flush the WARP pipe.
			 */
			DMA_BLOCK( MGA_YDST,		0x00000000,
				   MGA_FXLEFT,		0x00000000,
				   MGA_FXRIGHT,		0x00000001,
				   MGA_DWGCTL,		MGA_DWGCTL_FLUSH );

			DMA_BLOCK( MGA_LEN + MGA_EXEC,	0x00000001,
				   MGA_DWGSYNC,		0x00007000,
				   MGA_TEXCTL2,		MGA_G400_TC2_MAGIC,
				   MGA_LEN + MGA_EXEC,	0x00000000 );

			DMA_BLOCK( MGA_TEXCTL2,		(MGA_DUALTEX |
							 MGA_G400_TC2_MAGIC),
				   MGA_LEN + MGA_EXEC,	0x00000000,
				   MGA_TEXCTL2,		MGA_G400_TC2_MAGIC,
				   MGA_DMAPAD,		0x00000000 );
		}

		DMA_BLOCK( MGA_WVRTXSZ,		0x00001807,
			   MGA_DMAPAD,		0x00000000,
			   MGA_DMAPAD,		0x00000000,
			   MGA_DMAPAD,		0x00000000 );

		DMA_BLOCK( MGA_WACCEPTSEQ,	0x00000000,
			   MGA_WACCEPTSEQ,	0x00000000,
			   MGA_WACCEPTSEQ,	0x00000000,
			   MGA_WACCEPTSEQ,	0x18000000 );
	}

	DMA_BLOCK( MGA_WFLAG,	0x00000000,
		   MGA_WFLAG1,	0x00000000,
		   MGA_WR56,	MGA_G400_WR56_MAGIC,
		   MGA_DMAPAD,	0x00000000 );

	DMA_BLOCK( MGA_WR49,	0x00000000,		/* tex0              */
		   MGA_WR57,	0x00000000,		/* tex0              */
		   MGA_WR53,	0x00000000,		/* tex1              */
		   MGA_WR61,	0x00000000 );		/* tex1              */

	DMA_BLOCK( MGA_WR54,	MGA_G400_WR_MAGIC,	/* tex0 width        */
		   MGA_WR62,	MGA_G400_WR_MAGIC,	/* tex0 height       */
		   MGA_WR52,	MGA_G400_WR_MAGIC,	/* tex1 width        */
		   MGA_WR60,	MGA_G400_WR_MAGIC );	/* tex1 height       */

	/* Padding required to to hardware bug.
	 */
	DMA_BLOCK( MGA_DMAPAD,	0xffffffff,
		   MGA_DMAPAD,	0xffffffff,
		   MGA_DMAPAD,	0xffffffff,
		   MGA_WIADDR2,	(dev_priv->warp_pipe_phys[pipe] |
				 MGA_WMODE_START |
				 MGA_WAGP_ENABLE) );

	ADVANCE_DMA();
}

static void mga_g200_emit_state( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	if ( sarea_priv->warp_pipe != dev_priv->warp_pipe ) {
		mga_g200_emit_pipe( dev_priv );
		dev_priv->warp_pipe = sarea_priv->warp_pipe;
	}

	if ( dirty & MGA_UPLOAD_CONTEXT ) {
		mga_g200_emit_context( dev_priv );
		sarea_priv->dirty &= ~MGA_UPLOAD_CONTEXT;
	}

	if ( dirty & MGA_UPLOAD_TEX0 ) {
		mga_g200_emit_tex0( dev_priv );
		sarea_priv->dirty &= ~MGA_UPLOAD_TEX0;
	}
}

static void mga_g400_emit_state( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;
	int multitex = sarea_priv->warp_pipe & MGA_T2;

	if ( sarea_priv->warp_pipe != dev_priv->warp_pipe ) {
		mga_g400_emit_pipe( dev_priv );
		dev_priv->warp_pipe = sarea_priv->warp_pipe;
	}

	if ( dirty & MGA_UPLOAD_CONTEXT ) {
		mga_g400_emit_context( dev_priv );
		sarea_priv->dirty &= ~MGA_UPLOAD_CONTEXT;
	}

	if ( dirty & MGA_UPLOAD_TEX0 ) {
		mga_g400_emit_tex0( dev_priv );
		sarea_priv->dirty &= ~MGA_UPLOAD_TEX0;
	}

	if ( (dirty & MGA_UPLOAD_TEX1) && multitex ) {
		mga_g400_emit_tex1( dev_priv );
		sarea_priv->dirty &= ~MGA_UPLOAD_TEX1;
	}
}


/* ================================================================
 * SAREA state verification
 */

/* Disallow all write destinations except the front and backbuffer.
 */
static int mga_verify_context( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;

	if ( ctx->dstorg != dev_priv->front_offset &&
	     ctx->dstorg != dev_priv->back_offset ) {
		DRM_DEBUG( "*** bad DSTORG: %x (front %x, back %x)\n\n",
			   ctx->dstorg, dev_priv->front_offset,
			   dev_priv->back_offset );
		ctx->dstorg = 0;
		return -EINVAL;
	}

	return 0;
}

/* Disallow texture reads from PCI space.
 */
static int mga_verify_tex( drm_mga_private_t *dev_priv, int unit )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_texture_regs_t *tex = &sarea_priv->tex_state[unit];
	unsigned int org;

	org = tex->texorg & (MGA_TEXORGMAP_MASK | MGA_TEXORGACC_MASK);

	if ( org == (MGA_TEXORGMAP_SYSMEM | MGA_TEXORGACC_PCI) ) {
		DRM_DEBUG( "*** bad TEXORG: 0x%x, unit %d\n",
			   tex->texorg, unit );
		tex->texorg = 0;
		return -EINVAL;
	}

	return 0;
}

static int mga_verify_state( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;
	int ret = 0;

	if ( sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	if ( dirty & MGA_UPLOAD_CONTEXT )
		ret |= mga_verify_context( dev_priv );

	if ( dirty & MGA_UPLOAD_TEX0 )
		ret |= mga_verify_tex( dev_priv, 0 );

	if ( dev_priv->chipset == MGA_CARD_TYPE_G400 ) {
		if ( dirty & MGA_UPLOAD_TEX1 )
			ret |= mga_verify_tex( dev_priv, 1 );

		if ( dirty & MGA_UPLOAD_PIPE )
			ret |= ( sarea_priv->warp_pipe > MGA_MAX_G400_PIPES );
	} else {
		if ( dirty & MGA_UPLOAD_PIPE )
			ret |= ( sarea_priv->warp_pipe > MGA_MAX_G200_PIPES );
	}

	return ( ret == 0 );
}

static int mga_verify_tex_blit( drm_mga_private_t *dev_priv,
				unsigned long bus_address,
				unsigned int dstorg, int length )
{
	if ( dstorg < dev_priv->texture_offset ||
	     dstorg + length > (dev_priv->texture_offset +
				dev_priv->texture_size) ) {
		return -EINVAL;
	}

	if ( length & MGA_ILOAD_MASK ) {
		return -EINVAL;
	}

	return 0;
}


/* ================================================================
 *
 */

static void mga_dma_dispatch_clear( drm_device_t *dev,
				    drm_mga_clear_t *clear )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	int i;
	DMA_LOCALS;
	DRM_INFO( "%s\n", __FUNCTION__ );

	spin_lock_bh( &dev_priv->prim.lock );
	DRM_INFO( "spin_lock_bh() in %s\n", __FUNCTION__ );

	for ( i = 0 ; i < nbox ; i++ ) {
		unsigned int height = pbox[i].y2 - pbox[i].y1;

		BEGIN_DMA( 6 );

		if ( clear->flags & MGA_FRONT ) {
			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_PLNWT,	clear->color_mask,
				   MGA_YDSTLEN, (pbox[i].y1 << 16) | height,
				   MGA_FXBNDRY,	(pbox[i].x2 << 16) | pbox[i].x1 );

			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_FCOL,	clear->clear_color,
				   MGA_DSTORG,	dev_priv->front_offset,
				   MGA_DWGCTL + MGA_EXEC,
						dev_priv->clear_cmd );
		}

		if ( clear->flags & MGA_BACK ) {
			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_PLNWT,	clear->color_mask,
				   MGA_YDSTLEN, (pbox[i].y1 << 16) | height,
				   MGA_FXBNDRY,	(pbox[i].x2 << 16) | pbox[i].x1 );

			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_FCOL,	clear->clear_color,
				   MGA_DSTORG,	dev_priv->back_offset,
				   MGA_DWGCTL + MGA_EXEC,
						dev_priv->clear_cmd );
		}

		if ( clear->flags & MGA_DEPTH ) {
			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_PLNWT,	clear->depth_mask,
				   MGA_YDSTLEN, (pbox[i].y1 << 16) | height,
				   MGA_FXBNDRY,	(pbox[i].x2 << 16) | pbox[i].x1 );

			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_FCOL,	clear->clear_depth,
				   MGA_DSTORG,	dev_priv->depth_offset,
				   MGA_DWGCTL + MGA_EXEC,
						dev_priv->clear_cmd );
		}

		ADVANCE_DMA();
	}

	BEGIN_DMA( 1 );

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_PLNWT,	ctx->plnwt,
		   MGA_DWGCTL,	ctx->dwgctl );

	ADVANCE_DMA();

	DRM_INFO( "spin_unlock_bh() in %s\n", __FUNCTION__ );
	spin_unlock_bh( &dev_priv->prim.lock );
}

static void mga_dma_dispatch_swap( drm_device_t *dev )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_context_regs_t *ctx = &sarea_priv->context_state;
	drm_clip_rect_t *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	u32 pitch = dev_priv->front_pitch / dev_priv->fb_cpp;
	int i;
	DMA_LOCALS;
	DRM_INFO( "%s\n", __FUNCTION__ );

	spin_lock_bh( &dev_priv->prim.lock );
	DRM_INFO( "spin_lock_bh() in %s\n", __FUNCTION__ );

	BEGIN_DMA( 4 + nbox );

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DWGSYNC,	0x00007100,
		   MGA_DWGSYNC,	0x00007000 );

	DMA_BLOCK( MGA_DSTORG,	dev_priv->front_offset,
		   MGA_MACCESS,	dev_priv->maccess,
		   MGA_SRCORG,	dev_priv->back_offset,
		   MGA_AR5,	pitch );

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DMAPAD,	0x00000000,
		   MGA_DWGCTL,	MGA_DWGCTL_COPY );

	for ( i = 0 ; i < nbox ; i++ ) {
		u32 h = pbox[i].y2 - pbox[i].y1;
		u32 start = pbox[i].y1 * pitch;

		DMA_BLOCK( MGA_AR0,	start + pbox[i].x2 - 1,
			   MGA_AR3,	start + pbox[i].x1,
			   MGA_FXBNDRY,	((pbox[i].x2 - 1) << 16) | pbox[i].x1,
			   MGA_YDSTLEN + MGA_EXEC,
					(pbox[i].y1 << 16) | h );
	}

	DMA_BLOCK( MGA_DMAPAD,	0x00000000,
		   MGA_PLNWT,	ctx->plnwt,
		   MGA_SRCORG,	dev_priv->front_offset,
		   MGA_DWGCTL,	ctx->dwgctl );

	ADVANCE_DMA();

	DRM_INFO( "spin_unlock_bh() in %s\n", __FUNCTION__ );
	spin_unlock_bh( &dev_priv->prim.lock );
}


static void mga_dma_dispatch_vertex( drm_device_t *dev, drm_buf_t *buf )
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	u32 address = (u32) buf->bus_address;
	u32 length = (u32) buf->used;
	int i = 0;
	DMA_LOCALS;

	DRM_INFO( "%s: buf=%d used=%d\n",
		  __FUNCTION__, buf->idx, buf->used );

	spin_lock_bh( &dev_priv->prim.lock );
	DRM_INFO( "spin_lock_bh() in %s\n", __FUNCTION__ );

	if ( buf->used ) {
		buf_priv->dispatched = 1;

		MGA_EMIT_STATE( dev_priv, sarea_priv->dirty );

		do {
			if ( i < sarea_priv->nbox ) {
				mga_emit_clip_rect( dev_priv,
						    &sarea_priv->boxes[i] );
			}

			BEGIN_DMA( 1 );

			DMA_BLOCK( MGA_DMAPAD,		0x00000000,
				   MGA_DMAPAD,		0x00000000,
				   MGA_SECADDRESS,	(address |
							 MGA_DMA_VERTEX),
				   MGA_SECEND,		((address + length) |
							 MGA_PAGPXFER) );

			ADVANCE_DMA();
		} while ( ++i < sarea_priv->nbox );
	}

	if ( buf_priv->discard ) {
#if 0
		if ( buf_priv->dispatched == 1 ) {
			buf_priv->list_entry->age = sarea_priv->last_dispatch;

			BEGIN_DMA( 1 );

			DMA_BLOCK( MGA_DMAPAD,	0x00000000,
				   MGA_DMAPAD,	0x00000000,
				   MGA_DMAPAD,	0x00000000,
				   MGA_DWGSYNC,	sarea_priv->last_dispatch );

			ADVANCE_DMA();

			sarea_priv->last_dispatch += 4;
		}
#endif

		buf->pending = 0;
		buf->used = 0;
		buf_priv->dispatched = 0;

		mga_freelist_put( dev, buf );
	}

	DRM_INFO( "spin_unlock_bh() in %s\n", __FUNCTION__ );
	spin_unlock_bh( &dev_priv->prim.lock );
}



#if 0


/* This copies a 64 byte aligned agp region to the frambuffer
 * with a standard blit, the ioctl needs to do checking */

static void mga_dma_dispatch_tex_blit(drm_device_t * dev,
				      unsigned long bus_address,
				      int length, unsigned int destOrg)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	int use_agp = PDEA_pagpxfer_enable | 0x00000001;
	u16 y2;
	PRIMLOCALS;

	y2 = length / 64;

	PRIM_OVERFLOW(dev, dev_priv, 30);

	PRIMOUTREG(MGAREG_DSTORG, destOrg);
	PRIMOUTREG(MGAREG_MACCESS, 0x00000000);
	PRIMOUTREG(MGAREG_SRCORG, (u32) bus_address | use_agp);
	PRIMOUTREG(MGAREG_AR5, 64);

	PRIMOUTREG(MGAREG_PITCH, 64);
	PRIMOUTREG(MGAREG_DMAPAD, 0);
	PRIMOUTREG(MGAREG_DMAPAD, 0);
	PRIMOUTREG(MGAREG_DWGCTL, MGA_COPY_CMD);

	PRIMOUTREG(MGAREG_AR0, 63);
	PRIMOUTREG(MGAREG_AR3, 0);
	PRIMOUTREG(MGAREG_FXBNDRY, (63 << 16));
	PRIMOUTREG(MGAREG_YDSTLEN + MGAREG_MGA_EXEC, y2);

	PRIMOUTREG(MGAREG_DMAPAD, 0);
	PRIMOUTREG(MGAREG_SRCORG, 0);
	PRIMOUTREG(MGAREG_PITCH, dev_priv->stride / dev_priv->cpp);
	PRIMOUTREG(MGAREG_DWGSYNC, 0x7000);
	PRIMADVANCE(dev_priv);
}


static void mga_dma_dispatch_indices(drm_device_t * dev,
				     drm_buf_t * buf,
				     unsigned int start, unsigned int end)
{
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int address = (unsigned int) buf->bus_address;
	int use_agp = PDEA_pagpxfer_enable;
	int i = 0;
	PRIMLOCALS;

	if (start != end) {
		/* WARNING: if you change any of the state functions verify
		 * these numbers (Overestimating this doesn't hurt).
		 */
		buf_priv->dispatched = 1;
		PRIM_OVERFLOW(dev, dev_priv,
			      (MAX_STATE_SIZE + (5 * MGA_NR_SAREA_CLIPRECTS)));
		mgaEmitState(dev_priv);

		do {
			if (i < sarea_priv->nbox) {
				mgaEmitClipRect(dev_priv,
						&sarea_priv->boxes[i]);
			}

			PRIMGETPTR(dev_priv);
			PRIMOUTREG(MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_SETUPADDRESS,
				   ((address + start) |
				    SETADD_mode_vertlist));
			PRIMOUTREG(MGAREG_SETUPEND,
				   ((address + end) | use_agp));
/*				   ((address + start + 12) | use_agp)); */
			PRIMADVANCE(dev_priv);
		} while (++i < sarea_priv->nbox);
	}
	if (buf_priv->discard) {
		if (buf_priv->dispatched == 1)
			AGEBUF(dev_priv, buf_priv);
		buf_priv->dispatched = 0;
		mga_freelist_put(dev, buf);
	}
}



#endif




/* ================================================================
 *
 */

int mga_dma_clear( struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_mga_clear_t clear;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &clear, (drm_mga_clear_t *) arg, sizeof(clear) ) )
		return -EFAULT;

	if ( sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	mga_dma_dispatch_clear( dev, &clear );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CONTEXT;

	return 0;
}

int mga_dma_swap( struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;

	LOCK_TEST_WITH_RETURN( dev );

	if ( sarea_priv->nbox > MGA_NR_SAREA_CLIPRECTS )
		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	mga_dma_dispatch_swap( dev );

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CONTEXT;

	/* If we're idle, flush the primary DMA stream...
	 */
	spin_lock_bh( &dev_priv->prim.lock );
	DRM_INFO( "spin_lock_bh() in %s\n", __FUNCTION__ );

	FLUSH_DMA();

	DRM_INFO( "spin_unlock_bh() in %s\n", __FUNCTION__ );
	spin_unlock_bh( &dev_priv->prim.lock );

	return 0;
}

int mga_dma_vertex( struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long arg )
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_vertex_t vertex;

	LOCK_TEST_WITH_RETURN( dev );

	if ( copy_from_user( &vertex,
			     (drm_mga_vertex_t *)arg,
			     sizeof(vertex) ) )
		return -EFAULT;

	/* HACK: Force this for now...
	 */
	if ( mga_do_wait_for_idle( dev_priv ) < 0 )
		return -EBUSY;

	buf = dma->buflist[vertex.idx];
	buf_priv = buf->dev_private;

	buf->used = vertex.used;
	buf_priv->discard = vertex.discard;

	if ( !mga_verify_state( dev_priv ) ) {
		if ( vertex.discard ) {
#if 0
			if ( buf_priv->dispatched == 1 )
				AGEBUF(dev_priv, buf_priv);
#endif
			buf_priv->dispatched = 0;
			mga_freelist_put( dev, buf );
		}
		return -EINVAL;
	}

	mga_dma_dispatch_vertex( dev, buf );

	return 0;
}



#if 0


int mga_iload(struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_device_dma_t *dma = dev->dma;
	drm_mga_private_t *dev_priv =
	    (drm_mga_private_t *) dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_iload_t iload;
	unsigned long bus_address;

	if (copy_from_user(&iload, (drm_mga_iload_t *) arg, sizeof(iload)))
		return -EFAULT;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("mga_iload called without lock held\n");
		return -EINVAL;
	}

	buf = dma->buflist[iload.idx];
	buf_priv = buf->dev_private;
	bus_address = buf->bus_address;

	if (mgaVerifyIload(dev_priv,
			   bus_address, iload.destOrg, iload.length)) {
		mga_freelist_put(dev, buf);
		return -EINVAL;
	}

	sarea_priv->dirty |= MGA_UPLOAD_CTX;

	mga_dma_dispatch_tex_blit(dev, bus_address, iload.length,
				  iload.destOrg);
	AGEBUF(dev_priv, buf_priv);
	buf_priv->discard = 1;
	mga_freelist_put(dev, buf);
	mga_flush_write_combine();
	mga_dma_schedule(dev, 1);
	return 0;
}


int mga_indices(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
	drm_mga_private_t *dev_priv =
	    (drm_mga_private_t *) dev->dev_private;
	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
	drm_mga_buf_priv_t *buf_priv;
	drm_mga_indices_t indices;

	if (copy_from_user(&indices,
			   (drm_mga_indices_t *)arg, sizeof(indices)))
		return -EFAULT;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("mga_indices called without lock held\n");
		return -EINVAL;
	}

	buf = dma->buflist[indices.idx];
	buf_priv = buf->dev_private;

	buf_priv->discard = indices.discard;

	if (!mgaVerifyState(dev_priv)) {
		if (indices.discard) {
			if (buf_priv->dispatched == 1)
				AGEBUF(dev_priv, buf_priv);
			buf_priv->dispatched = 0;
			mga_freelist_put(dev, buf);
		}
		return -EINVAL;
	}

	mga_dma_dispatch_indices(dev, buf, indices.start, indices.end);

	PRIMUPDATE(dev_priv);
	mga_flush_write_combine();
	mga_dma_schedule(dev, 1);
	return 0;
}


#endif
