/* mga_state.c -- State support for mga g200/g400 -*- linux-c -*-
 * Created: Thu Jan 27 02:53:43 2000 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 * Authors: Jeff Hartmann <jhartmann@precisioninsight.com>
 * 	    Keith Whitwell <keithw@precisioninsight.com>
 *
 * $XFree86$
 *
 */
 
#define __NO_VERSION__
#include "drmP.h"
#include "mga_drv.h"
#include "mgareg_flags.h"
#include "mga_dma.h"
#include "drm.h"

static void mgaEmitClipRect( drm_mga_private_t *dev_priv, xf86drmClipRectRec *box )
{
	PRIMLOCALS;

   	/* This takes a max of 10 dwords */
	PRIMGETPTR( dev_priv );

	/* The G400 seems to have an issue with the second WARP not
	 * stalling clipper register writes.  This bothers me, but the only
	 * way I could get it to never clip the last triangle under any
	 * circumstances is by inserting TWO dwgsync commands.
	 */
 	if (dev_priv->chipset == MGA_CARD_TYPE_G400) { 
		PRIMOUTREG( MGAREG_DMAPAD, 0 );
		PRIMOUTREG( MGAREG_DMAPAD, 0 );
		PRIMOUTREG( MGAREG_DWGSYNC, dev_priv->last_sync_tag - 1 );
		PRIMOUTREG( MGAREG_DWGSYNC, dev_priv->last_sync_tag - 1 );
	}

	PRIMOUTREG( MGAREG_CXBNDRY, ((box->x2)<<16)|(box->x1) );
	PRIMOUTREG( MGAREG_YTOP, box->y1 * dev_priv->stride/2 );
	PRIMOUTREG( MGAREG_YBOT, box->y2 * dev_priv->stride/2 );
	PRIMOUTREG( MGAREG_DMAPAD, 0 );

	PRIMADVANCE( dev_priv );
}

static void mgaEmitContext(drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->ContextState;
	PRIMLOCALS;
	
   	/* This takes a max of 15 dwords */
	PRIMGETPTR( dev_priv );

	PRIMOUTREG( MGAREG_DSTORG, regs[MGA_CTXREG_DSTORG] );
	PRIMOUTREG( MGAREG_MACCESS, regs[MGA_CTXREG_MACCESS] );
	PRIMOUTREG( MGAREG_PLNWT, regs[MGA_CTXREG_PLNWT] );
	PRIMOUTREG( MGAREG_DWGCTL, regs[MGA_CTXREG_DWGCTL] );

	PRIMOUTREG( MGAREG_ALPHACTRL, regs[MGA_CTXREG_ALPHACTRL] );
	PRIMOUTREG( MGAREG_FOGCOL, regs[MGA_CTXREG_FOGCOLOR] );
	PRIMOUTREG( MGAREG_WFLAG, regs[MGA_CTXREG_WFLAG] );
	PRIMOUTREG( MGAREG_ZORG, dev_priv->depthOffset ); /* invarient */

 	if (dev_priv->chipset == MGA_CARD_TYPE_G400) { 
		PRIMOUTREG( MGAREG_WFLAG1, regs[MGA_CTXREG_WFLAG] );
		PRIMOUTREG( MGAREG_TDUALSTAGE0, regs[MGA_CTXREG_TDUAL0] );
		PRIMOUTREG( MGAREG_TDUALSTAGE1, regs[MGA_CTXREG_TDUAL1] );      
		PRIMOUTREG( MGAREG_DMAPAD, 0 );
	}   
   
	PRIMADVANCE( dev_priv );
}

static void mgaG200EmitTex( drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->TexState[0];
	PRIMLOCALS;

	PRIMGETPTR( dev_priv );
   
   	/* This takes 20 dwords */

	PRIMOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] );
	PRIMOUTREG(MGAREG_TEXCTL, regs[MGA_TEXREG_CTL] );
	PRIMOUTREG(MGAREG_TEXFILTER, regs[MGA_TEXREG_FILTER] );
	PRIMOUTREG(MGAREG_TEXBORDERCOL, regs[MGA_TEXREG_BORDERCOL] );

	PRIMOUTREG(MGAREG_TEXORG, regs[MGA_TEXREG_ORG] );
	PRIMOUTREG(MGAREG_TEXORG1, regs[MGA_TEXREG_ORG1] );
	PRIMOUTREG(MGAREG_TEXORG2, regs[MGA_TEXREG_ORG2] );
	PRIMOUTREG(MGAREG_TEXORG3, regs[MGA_TEXREG_ORG3] );

	PRIMOUTREG(MGAREG_TEXORG4, regs[MGA_TEXREG_ORG4] );		
	PRIMOUTREG(MGAREG_TEXWIDTH, regs[MGA_TEXREG_WIDTH] );
	PRIMOUTREG(MGAREG_TEXHEIGHT, regs[MGA_TEXREG_HEIGHT] );   
	PRIMOUTREG(0x2d00 + 24*4, regs[MGA_TEXREG_WIDTH] );

	PRIMOUTREG(0x2d00 + 34*4, regs[MGA_TEXREG_HEIGHT] );
	PRIMOUTREG( MGAREG_TEXTRANS, 0xffff );
	PRIMOUTREG( MGAREG_TEXTRANSHIGH, 0xffff );
	PRIMOUTREG( MGAREG_DMAPAD, 0 );

	PRIMADVANCE( dev_priv );  
}

static void mgaG400EmitTex0( drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->TexState[0];
	int multitex = sarea_priv->WarpPipe & MGA_T2;
	PRIMLOCALS;
   
	PRIMGETPTR( dev_priv );
   
   	/* This takes a max of 30 dwords */

	PRIMOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] );
	PRIMOUTREG(MGAREG_TEXCTL, regs[MGA_TEXREG_CTL] );
	PRIMOUTREG(MGAREG_TEXFILTER, regs[MGA_TEXREG_FILTER] );
	PRIMOUTREG(MGAREG_TEXBORDERCOL, regs[MGA_TEXREG_BORDERCOL] );

	PRIMOUTREG(MGAREG_TEXORG, regs[MGA_TEXREG_ORG] );
	PRIMOUTREG(MGAREG_TEXORG1, regs[MGA_TEXREG_ORG1] );
	PRIMOUTREG(MGAREG_TEXORG2, regs[MGA_TEXREG_ORG2] );
	PRIMOUTREG(MGAREG_TEXORG3, regs[MGA_TEXREG_ORG3] );

	PRIMOUTREG(MGAREG_TEXORG4, regs[MGA_TEXREG_ORG4] );		
	PRIMOUTREG(MGAREG_TEXWIDTH, regs[MGA_TEXREG_WIDTH] );
	PRIMOUTREG(MGAREG_TEXHEIGHT, regs[MGA_TEXREG_HEIGHT] );   
	PRIMOUTREG(0x2d00 + 49*4, 0);

	PRIMOUTREG(0x2d00 + 57*4, 0);
	PRIMOUTREG(0x2d00 + 53*4, 0);
	PRIMOUTREG(0x2d00 + 61*4, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0 );

	if (!multitex) {
		PRIMOUTREG(0x2d00 + 52*4, 0x40 ); 
		PRIMOUTREG(0x2d00 + 60*4, 0x40 );
		PRIMOUTREG( MGAREG_DMAPAD, 0 );
		PRIMOUTREG( MGAREG_DMAPAD, 0 );
	} 

	PRIMOUTREG( 0x2d00 + 54*4, regs[MGA_TEXREG_WIDTH] | 0x40 );
	PRIMOUTREG( 0x2d00 + 62*4, regs[MGA_TEXREG_HEIGHT] | 0x40 );
	PRIMOUTREG( MGAREG_TEXTRANS, 0xffff );
	PRIMOUTREG( MGAREG_TEXTRANSHIGH, 0xffff );

	PRIMADVANCE( dev_priv );  
}

#define TMC_map1_enable 		0x80000000 	

static void mgaG400EmitTex1( drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->TexState[1];
	PRIMLOCALS;
   
	PRIMGETPTR(dev_priv);

   	/* This takes 25 dwords */
	PRIMOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] | TMC_map1_enable);
	PRIMOUTREG(MGAREG_TEXCTL, regs[MGA_TEXREG_CTL] );
	PRIMOUTREG(MGAREG_TEXFILTER, regs[MGA_TEXREG_FILTER] );
	PRIMOUTREG(MGAREG_TEXBORDERCOL, regs[MGA_TEXREG_BORDERCOL] );

	PRIMOUTREG(MGAREG_TEXORG, regs[MGA_TEXREG_ORG] );
	PRIMOUTREG(MGAREG_TEXORG1, regs[MGA_TEXREG_ORG1] );
	PRIMOUTREG(MGAREG_TEXORG2, regs[MGA_TEXREG_ORG2] );
	PRIMOUTREG(MGAREG_TEXORG3, regs[MGA_TEXREG_ORG3] );

	PRIMOUTREG(MGAREG_TEXORG4, regs[MGA_TEXREG_ORG4] );		
	PRIMOUTREG(MGAREG_TEXWIDTH, regs[MGA_TEXREG_WIDTH] );
	PRIMOUTREG(MGAREG_TEXHEIGHT, regs[MGA_TEXREG_HEIGHT] );   
	PRIMOUTREG(0x2d00 + 49*4, 0);

	PRIMOUTREG(0x2d00 + 57*4, 0);
	PRIMOUTREG(0x2d00 + 53*4, 0);
	PRIMOUTREG(0x2d00 + 61*4, 0);
	PRIMOUTREG(0x2d00 + 52*4, regs[MGA_TEXREG_WIDTH] | 0x40 ); 

	PRIMOUTREG(0x2d00 + 60*4, regs[MGA_TEXREG_HEIGHT] | 0x40 );
	PRIMOUTREG( MGAREG_TEXTRANS, 0xffff );
	PRIMOUTREG( MGAREG_TEXTRANSHIGH, 0xffff );
	PRIMOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] );

	PRIMADVANCE( dev_priv );
}

static void mgaG400EmitPipe(drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->WarpPipe;
	float fParam = 12800.0f;
	PRIMLOCALS;
   
	PRIMGETPTR(dev_priv);

   	/* This takes 25 dwords */
   
	/* Establish vertex size.  
	 */
	if (pipe & MGA_T2) {
		PRIMOUTREG(MGAREG_WIADDR2, WIA_wmode_suspend);
		PRIMOUTREG(MGAREG_WVRTXSZ, 0x00001e09);
		PRIMOUTREG(MGAREG_WACCEPTSEQ, 0x1e000000);
		PRIMOUTREG(MGAREG_WFLAG, 0);
	} else {
		PRIMOUTREG(MGAREG_WIADDR2, WIA_wmode_suspend);
		PRIMOUTREG(MGAREG_WVRTXSZ, 0x00001807);
		PRIMOUTREG(MGAREG_WACCEPTSEQ, 0x18000000);
		PRIMOUTREG(MGAREG_WFLAG, 0);
	}   

	PRIMOUTREG(MGAREG_WFLAG1, 0);   
	PRIMOUTREG(0x2d00 + 56*4, *((u32 *)(&fParam)));
	PRIMOUTREG(MGAREG_DMAPAD, 0);
	PRIMOUTREG(MGAREG_DMAPAD, 0);
   
	PRIMOUTREG(0x2d00 + 49*4, 0);  /* Tex stage 0 */
	PRIMOUTREG(0x2d00 + 57*4, 0);  /* Tex stage 0 */
	PRIMOUTREG(0x2d00 + 53*4, 0);  /* Tex stage 1 */
	PRIMOUTREG(0x2d00 + 61*4, 0);  /* Tex stage 1 */
   
	PRIMOUTREG(0x2d00 + 54*4, 0x40); /* Tex stage 0 : w */
	PRIMOUTREG(0x2d00 + 62*4, 0x40); /* Tex stage 0 : h */
	PRIMOUTREG(0x2d00 + 52*4, 0x40); /* Tex stage 1 : w */
	PRIMOUTREG(0x2d00 + 60*4, 0x40); /* Tex stage 1 : h */
   
	/* Dma pading required due to hw bug */
	PRIMOUTREG(MGAREG_DMAPAD, 0xffffffff);
	PRIMOUTREG(MGAREG_DMAPAD, 0xffffffff);
	PRIMOUTREG(MGAREG_DMAPAD, 0xffffffff);
	PRIMOUTREG(MGAREG_WIADDR2, (__u32)(dev_priv->WarpIndex[pipe].phys_addr |
					   WIA_wmode_start | WIA_wagp_agp));
	PRIMADVANCE(dev_priv);
}

static void mgaG200EmitPipe( drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->WarpPipe;
	PRIMLOCALS;

	PRIMGETPTR(dev_priv);
   
   	/* This takes 15 dwords */

	PRIMOUTREG(MGAREG_WIADDR, WIA_wmode_suspend);
	PRIMOUTREG(MGAREG_WVRTXSZ, 7);
	PRIMOUTREG(MGAREG_WFLAG, 0);
	PRIMOUTREG(0x2d00 + 24*4, 0); /* tex w/h */
   
	PRIMOUTREG(0x2d00 + 25*4, 0x100);
	PRIMOUTREG(0x2d00 + 34*4, 0); /* tex w/h */
	PRIMOUTREG(0x2d00 + 42*4, 0xFFFF);
	PRIMOUTREG(0x2d00 + 60*4, 0xFFFF);
   
	/* Dma pading required due to hw bug */
	PRIMOUTREG(MGAREG_DMAPAD, 0xffffffff);
	PRIMOUTREG(MGAREG_DMAPAD, 0xffffffff);
	PRIMOUTREG(MGAREG_DMAPAD, 0xffffffff);
	PRIMOUTREG(MGAREG_WIADDR, (__u32)(dev_priv->WarpIndex[pipe].phys_addr | 
					  WIA_wmode_start | WIA_wagp_agp));

	PRIMADVANCE(dev_priv);
}

static void mgaEmitState( drm_mga_private_t *dev_priv )
{
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;

	if (dev_priv->chipset == MGA_CARD_TYPE_G400) {	   
		int multitex = sarea_priv->WarpPipe & MGA_T2;

/*  		DRM_DEBUG("BUF PIPE: %x LOADED PIPE: %x\n", */
/*  		       sarea_priv->WarpPipe, dev_priv->WarpPipe); */

		if (sarea_priv->WarpPipe != dev_priv->WarpPipe) { 
			mgaG400EmitPipe( dev_priv );
			dev_priv->WarpPipe = sarea_priv->WarpPipe;
		}

		if (dirty & MGA_UPLOAD_CTX)
			mgaEmitContext( dev_priv );

		if (dirty & MGA_UPLOAD_TEX0)
			mgaG400EmitTex0( dev_priv );

		if ((dirty & MGA_UPLOAD_TEX1) && multitex)
			mgaG400EmitTex1( dev_priv );
	} else {
		if (sarea_priv->WarpPipe != dev_priv->WarpPipe) { 
			mgaG200EmitPipe( dev_priv );
			dev_priv->WarpPipe = sarea_priv->WarpPipe;
		}

		if (dirty & MGA_UPLOAD_CTX)
			mgaEmitContext( dev_priv );

		if (dirty & MGA_UPLOAD_TEX0)
			mgaG200EmitTex( dev_priv );
	}
   
   	sarea_priv->dirty = 0;
}

/* WARNING if you change any of the state functions 
 * verify these numbers */

static int mgaCalcState( drm_mga_private_t *dev_priv )
{
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;
	int size = 0;
   
   	if (dev_priv->chipset == MGA_CARD_TYPE_G400) {	   
		int multitex = sarea_priv->WarpPipe & MGA_T2;

		if (sarea_priv->WarpPipe != dev_priv->WarpPipe) {
			size += 25;
		}
		if (dirty & MGA_UPLOAD_CTX) {
		   	size += 15;
		}
		if (dirty & MGA_UPLOAD_TEX0) {
		   	size += 30;
		}
		if ((dirty & MGA_UPLOAD_TEX1) && multitex) {
		   	size += 25;
		}
	} else {
		if (sarea_priv->WarpPipe != dev_priv->WarpPipe) {
		   	size += 15;
		}
		if (dirty & MGA_UPLOAD_CTX) {
		   	size += 15;
		}
		if (dirty & MGA_UPLOAD_TEX0) {
		   	size += 20;
		}
	}	  
	return size;
}

/* Disallow all write destinations except the front and backbuffer.
 */
static int mgaVerifyContext(drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->ContextState;

	if (regs[MGA_CTXREG_DSTORG] != dev_priv->frontOffset &&
	    regs[MGA_CTXREG_DSTORG] != dev_priv->backOffset) {
		DRM_DEBUG("BAD DSTORG: %x (front %x, back %x)\n\n", 
		       regs[MGA_CTXREG_DSTORG], dev_priv->frontOffset,
		       dev_priv->backOffset);
  	        regs[MGA_CTXREG_DSTORG] = 0;
		return -1;
	}

	return 0;
}

/* Disallow texture reads from PCI space.
 */
static int mgaVerifyTex(drm_mga_private_t *dev_priv, 
		      int unit)
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;

	if ((sarea_priv->TexState[unit][MGA_TEXREG_ORG] & 0x3) == 0x1) {
		DRM_DEBUG("BAD TEXREG_ORG: %x, unit %d\n", 
		       sarea_priv->TexState[unit][MGA_TEXREG_ORG],
		       unit);
		sarea_priv->TexState[unit][MGA_TEXREG_ORG] = 0;
		return -1;
	} 

	return 0;
}

static int mgaVerifyState( drm_mga_private_t *dev_priv )
{
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty;
	int rv = 0;

   	if (sarea_priv->nbox >= MGA_NR_SAREA_CLIPRECTS)
     		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	if (dirty & MGA_UPLOAD_CTX)
		rv |= mgaVerifyContext( dev_priv );

	if (dirty & MGA_UPLOAD_TEX0)
		rv |= mgaVerifyTex( dev_priv, 0 );

	if (dev_priv->chipset == MGA_CARD_TYPE_G400) 
	{	   
		if (dirty & MGA_UPLOAD_TEX1)
			rv |= mgaVerifyTex( dev_priv, 1 );
	   
		if (dirty & MGA_UPLOAD_PIPE) 
			rv |= (sarea_priv->WarpPipe > MGA_MAX_G400_PIPES); 
	} 
	else 
	{
		if (dirty & MGA_UPLOAD_PIPE) 
			rv |= (sarea_priv->WarpPipe > MGA_MAX_G200_PIPES); 
	}	  

	return rv == 0;
}

#if 0
/* This is very broken */

static void mga_dma_dispatch_tex_blit(drm_device_t *dev, drm_buf_t *buf, u16 x1,
				      u16 x2, u16 y1, u16 y2, unsigned int destOrg,
				      unsigned int mAccess, unsigned int pitch)
{
	int use_agp = PDEA_pagpxfer_enable;
      	drm_mga_private_t *dev_priv = dev->dev_private;
   	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned long address = (unsigned long)buf->bus_address;
   	int length;
   	int width, height;
   	int texperdword = 0;
   	PRIMLOCALS;
   
   	switch((maccess & 0x00000003)) {
	case 0:
	   	texperdword = 4;
      		break;
	case 1:
      		texperdword = 2;
	   	break;
	case 2:
      		texperdword = 1;
      		break;
	}
   
   	length = (y2 - y1) * (x2 - x1) / texperdword;
   
   	x2 = (x2 + (texperdword - 1)) & ~(texperdword - 1);
   	x1 = (x1 + (texperdword - 1)) & ~(texperdword - 1);
   	width = x2 - x1;
   	height = y2 - y1;
   
	PRIMRESET(dev_priv);		
   	PRIMGETPTR(dev_priv);
   	PRIMOUTREG(MGAREG_DSTORG, dstorg);
   	PRIMOUTREG(MGAREG_MACCESS, maccess);
   	PRIMOUTREG(MGAREG_PITCH, pitch);
   	PRIMOUTREG(MGAREG_YDSTLEN, (y1 << 16) | height);
   
   	PRIMOUTREG(MGAREG_FXBNDRY, ((x1+width-1) << 16) | x1);
   	PRIMOUTREG(MGAREG_AR0, width * height - 1);
      	PRIMOUTREG(MGAREG_AR3, 0 );
   	PRIMOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, MGA_ILOAD_CMD);
	   	   
   	PRIMOUTREG(MGAREG_DMAPAD, 0);
   	PRIMOUTREG(MGAREG_DMAPAD, 0);
   	PRIMOUTREG(MGAREG_SRCORG, ((__u32)address) | TT_BLIT);
   	PRIMOUTREG(MGAREG_SECEND, ((__u32)(address + length)) | use_agp);
	   
   	PRIMOUTREG(MGAREG_DSTORG, dev_priv->frontOffset);
   	PRIMOUTREG(MGAREG_MACCESS, dev_priv->mAccess);
   	PRIMOUTREG(MGAREG_PITCH, dev_priv->stride);
   	PRIMOUTREG(MGAREG_AR0, 0 );

      	PRIMOUTREG(MGAREG_AR3, 0 );
	PRIMOUTREG(MGAREG_DMAPAD, 0);
	PRIMOUTREG(MGAREG_DMAPAD, 0);
   	PRIMOUTREG(MGAREG_SOFTRAP, 0);
   	PRIMADVANCE(dev_priv);
}
#endif

static inline void mga_dma_dispatch_vertex(drm_device_t *dev, 
					   drm_buf_t *buf, int real_idx,
					   int idx)
{
   	drm_mga_private_t *dev_priv = dev->dev_private;
	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned long address = (unsigned long)buf->bus_address;
	int length = buf->used;	
	int use_agp = PDEA_pagpxfer_enable;
	int i = 0;
   	PRIMLOCALS;
   	int primary_needed;

   DRM_DEBUG("dispatch vertex %d addr 0x%lx, length 0x%x nbox %d dirty %x\n", 
		  buf->idx, address, length, sarea_priv->nbox, sarea_priv->dirty);

   	primary_needed = mgaCalcState( dev_priv );
   	/* Primary needed is in dwords */
   	if (sarea_priv->nbox == 0) {
	   primary_needed += 5;
	} else {
	   primary_needed += (5 * sarea_priv->nbox);
	   primary_needed += (10 * sarea_priv->nbox);
	}
   	primary_needed += 5; /* For the dwgsync */
   	PRIM_OVERFLOW(dev, dev_priv, primary_needed);
   	dev_priv->last_sync_tag = mga_create_sync_tag(dev_priv);
   	if(real_idx == idx) {
	   buf_priv->age = dev_priv->last_sync_tag;
	}
   	mgaEmitState( dev_priv );
   	do {
	   	if (i < sarea_priv->nbox) {
		   	DRM_DEBUG("idx %d Emit box %d/%d:"
				  "%d,%d - %d,%d\n", 
				  buf->idx,
				  i, sarea_priv->nbox,
				  sarea_priv->boxes[i].x1, 
				  sarea_priv->boxes[i].y1,
				  sarea_priv->boxes[i].x2, 
				  sarea_priv->boxes[i].y2);
		   
		   	mgaEmitClipRect( dev_priv, 
					&sarea_priv->boxes[i] );
		}

	   	PRIMGETPTR(dev_priv);
	   	PRIMOUTREG( MGAREG_DMAPAD, 0);
	   	PRIMOUTREG( MGAREG_DMAPAD, 0);
	   	PRIMOUTREG( MGAREG_SECADDRESS, 
			   ((__u32)address) | TT_VERTEX);
	   	PRIMOUTREG( MGAREG_SECEND, 
			   (((__u32)(address + length)) | 
			    use_agp));
	   	PRIMADVANCE( dev_priv );	       
	}  while (++i < sarea_priv->nbox);

	PRIMGETPTR( dev_priv );
   	PRIMOUTREG(MGAREG_DMAPAD, 0);
   	PRIMOUTREG(MGAREG_DMAPAD, 0);
   	PRIMOUTREG(MGAREG_DMAPAD, 0);
   	PRIMOUTREG(MGAREG_DWGSYNC, dev_priv->last_sync_tag);   
	PRIMADVANCE( dev_priv );
}

/* Not currently used
 */
static inline void mga_dma_dispatch_general(drm_device_t *dev, drm_buf_t *buf)
{
   	drm_mga_private_t *dev_priv = dev->dev_private;
   	drm_mga_buf_priv_t *buf_priv = buf->dev_private;
	unsigned long address = (unsigned long)buf->bus_address;
	int length = buf->used;
	int use_agp = PDEA_pagpxfer_enable;
   	PRIMLOCALS;

   	PRIM_OVERFLOW(dev, dev_priv, 10);
   	PRIMGETPTR(dev_priv);

      	dev_priv->last_sync_tag = mga_create_sync_tag(dev_priv);
   	buf_priv->age = dev_priv->last_sync_tag;

	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
   	PRIMOUTREG( MGAREG_SECADDRESS, ((__u32)address) | TT_GENERAL);
	PRIMOUTREG( MGAREG_SECEND, (((__u32)(address + length)) | use_agp));
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
      	PRIMOUTREG( MGAREG_DWGSYNC, dev_priv->last_sync_tag);
   	PRIMADVANCE(dev_priv);   
}

static inline void mga_dma_dispatch_clear( drm_device_t *dev, int flags, 
					  unsigned int clear_color,
					  unsigned int clear_zval )
{
   	drm_mga_private_t *dev_priv = dev->dev_private;
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	xf86drmClipRectRec *pbox = sarea_priv->boxes;
	unsigned int cmd;
	int i;
   	int primary_needed;
   	PRIMLOCALS;

	if ( dev_priv->sgram ) 
		cmd = MGA_CLEAR_CMD | DC_atype_blk;
	else
		cmd = MGA_CLEAR_CMD | DC_atype_rstr;

   	primary_needed = nbox * 35;
   	if(primary_needed == 0) primary_needed = 35;
	PRIM_OVERFLOW(dev, dev_priv, primary_needed);
	PRIMGETPTR( dev_priv );
      	dev_priv->last_sync_tag = mga_create_sync_tag(dev_priv);

	for (i = 0 ; i < nbox ; i++) {
		unsigned int height = pbox[i].y2 - pbox[i].y1;

	   	DRM_DEBUG("dispatch clear %d,%d-%d,%d flags %x!\n",
			  pbox[i].x1, pbox[i].y1, pbox[i].x2, 
			  pbox[i].y2, flags);

	   	if ( flags & MGA_CLEAR_FRONT ) {	    
		   	DRM_DEBUG("clear front\n");
			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_YDSTLEN, (pbox[i].y1<<16)|height);
			PRIMOUTREG(MGAREG_FXBNDRY, (pbox[i].x2<<16)|pbox[i].x1);

			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_FCOL, clear_color);
			PRIMOUTREG(MGAREG_DSTORG, dev_priv->frontOffset);
			PRIMOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, cmd );
		}

		if ( flags & MGA_CLEAR_BACK ) {
			DRM_DEBUG("clear back\n");
			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_YDSTLEN, (pbox[i].y1<<16)|height);
			PRIMOUTREG(MGAREG_FXBNDRY, (pbox[i].x2<<16)|pbox[i].x1);

			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_FCOL, clear_color);
			PRIMOUTREG(MGAREG_DSTORG, dev_priv->backOffset);
			PRIMOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, cmd );
		}

		if ( flags & MGA_CLEAR_DEPTH ) {
			DRM_DEBUG("clear depth\n");
			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_YDSTLEN, (pbox[i].y1<<16)|height);
			PRIMOUTREG(MGAREG_FXBNDRY, (pbox[i].x2<<16)|pbox[i].x1);

			PRIMOUTREG( MGAREG_DMAPAD, 0);
			PRIMOUTREG(MGAREG_FCOL, clear_zval);
			PRIMOUTREG(MGAREG_DSTORG, dev_priv->depthOffset);
			PRIMOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, cmd );
		}
	}

	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
      	PRIMOUTREG( MGAREG_DMAPAD, 0);
   	PRIMOUTREG( MGAREG_DWGSYNC, dev_priv->last_sync_tag);
	PRIMADVANCE(dev_priv);
}

static inline void mga_dma_dispatch_swap( drm_device_t *dev )
{
   	drm_mga_private_t *dev_priv = dev->dev_private;
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	int nbox = sarea_priv->nbox;
	xf86drmClipRectRec *pbox = sarea_priv->boxes;
	int i;
   	int primary_needed;
   	PRIMLOCALS;

   	primary_needed = nbox * 5;
   	primary_needed += 15;
	PRIM_OVERFLOW(dev, dev_priv, primary_needed);
	PRIMGETPTR( dev_priv );
   
      	dev_priv->last_sync_tag = mga_create_sync_tag(dev_priv);

	PRIMOUTREG(MGAREG_DSTORG, dev_priv->frontOffset);
	PRIMOUTREG(MGAREG_MACCESS, dev_priv->mAccess);
	PRIMOUTREG(MGAREG_SRCORG, dev_priv->backOffset);
	PRIMOUTREG(MGAREG_AR5, dev_priv->stride/2);  

	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
	PRIMOUTREG(MGAREG_DWGCTL, MGA_COPY_CMD); 
	     
	for (i = 0 ; i < nbox; i++) {
		unsigned int h = pbox[i].y2 - pbox[i].y1;
		unsigned int start = pbox[i].y1 * dev_priv->stride/2;
		
	   	DRM_DEBUG("dispatch swap %d,%d-%d,%d!\n",
		       pbox[i].x1, pbox[i].y1,
		       pbox[i].x2, pbox[i].y2);

		PRIMOUTREG(MGAREG_AR0, start + pbox[i].x2 - 1);
		PRIMOUTREG(MGAREG_AR3, start + pbox[i].x1);		
		PRIMOUTREG(MGAREG_FXBNDRY, pbox[i].x1|((pbox[i].x2 - 1)<<16));
		PRIMOUTREG(MGAREG_YDSTLEN+MGAREG_MGA_EXEC, (pbox[i].y1<<16)|h);
	}
  
	PRIMOUTREG( MGAREG_SRCORG, 0);
	PRIMOUTREG( MGAREG_DMAPAD, 0);
      	PRIMOUTREG( MGAREG_DMAPAD, 0);
      	PRIMOUTREG( MGAREG_DWGSYNC, dev_priv->last_sync_tag);
   	PRIMADVANCE(dev_priv);
}

int mga_clear_bufs(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
   	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
      	__volatile__ unsigned int *status = 
     		(__volatile__ unsigned int *)dev_priv->status_page;

	drm_mga_clear_t clear;

   	copy_from_user_ret(&clear, (drm_mga_clear_t *)arg, sizeof(clear), 
			   -EFAULT);
   
   	if (sarea_priv->nbox >= MGA_NR_SAREA_CLIPRECTS)
     		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CTX;
	mga_dma_dispatch_clear( dev, clear.flags, 
			        clear.clear_color, 
			        clear.clear_depth );
      	PRIMUPDATE(dev_priv);
	mga_dma_schedule(dev, 1);
   	sarea_priv->last_dispatch = status[1];
   	return 0;
}

int mga_swap_bufs(struct inode *inode, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
   	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
      	__volatile__ unsigned int *status = 
     		(__volatile__ unsigned int *)dev_priv->status_page;
   
      	if (sarea_priv->nbox >= MGA_NR_SAREA_CLIPRECTS)
     		sarea_priv->nbox = MGA_NR_SAREA_CLIPRECTS;

	/* Make sure we restore the 3D state next time.
	 */
	dev_priv->sarea_priv->dirty |= MGA_UPLOAD_CTX;
	mga_dma_dispatch_swap( dev );
      	PRIMUPDATE(dev_priv);
   	atomic_set(&dev_priv->current_prim->force_fire, 1);
	mga_dma_schedule(dev, 1);
      	sarea_priv->last_dispatch = status[1];
   	return 0;
}

/* This is very broken */
int mga_iload(struct inode *inode, struct file *filp,
	      unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
      	drm_device_dma_t *dma = dev->dma;
      	drm_buf_t *buf;
      	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
      	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
      	__volatile__ unsigned int *status = 
     		(__volatile__ unsigned int *)dev_priv->status_page;
	drm_mga_iload_t iload;

	copy_from_user_ret(&iload, (drm_mga_iload_t *)arg, sizeof(iload),
			   -EFAULT);
   	buf = dma->buflist[iload.idx];
#if 0
   	sarea_priv->dirty |= (MGA_UPLOAD_CTX | MGA_UPLOAD_2D);
   
   	DRM_DEBUG("buf->used : %d\n", buf->used);
   
   	mga_dma_dispatch_tex_blit(dev, buf, iload.x1, iload.x2, iload.y1, iload.y2,
				  iload.destOrg, iload.mAccess, iload.pitch);

	mga_dma_schedule(dev, 1);
#endif
   	mga_freelist_put(dev, buf);
   	sarea_priv->last_dispatch = status[1];
	return 0; 
}

int mga_vertex(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->dev;
      	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
      	__volatile__ unsigned int *status = 
     		(__volatile__ unsigned int *)dev_priv->status_page;
   	drm_device_dma_t *dma = dev->dma;
	drm_buf_t *buf;
      	drm_mga_buf_priv_t *buf_priv;
	drm_mga_vertex_t vertex;

	copy_from_user_ret(&vertex, (drm_mga_vertex_t *)arg, sizeof(vertex),
			   -EFAULT);
   
	buf = dma->buflist[ vertex.real_idx ];
   	buf_priv = buf->dev_private;
   
	if (!mgaVerifyState(dev_priv)) {
	   if(vertex.real_idx == vertex.idx) 
	     	buf_priv->age = dev_priv->last_sync_tag;
	   return -EINVAL;
	}

	buf->used = vertex.real_used;
   	if(vertex.discard) {
	      	buf_priv->age = dev_priv->last_sync_tag;
	} else {
	   	mga_dma_dispatch_vertex(dev, buf, vertex.real_idx, 
					vertex.idx);
	}
   	PRIMUPDATE(dev_priv);
	mga_dma_schedule(dev, 1);
      	sarea_priv->last_dispatch = status[1];
   	return 0;
}

static int mga_dma_get_buffers(drm_device_t *dev, drm_dma_t *d)
{
	int		  i;
	drm_buf_t	  *buf;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = mga_freelist_get(dev);
		if (!buf) break;
		buf->pid     = current->pid;
		copy_to_user_ret(&d->request_indices[i],
				 &buf->idx,
				 sizeof(buf->idx),
				 -EFAULT);
		copy_to_user_ret(&d->request_sizes[i],
				 &buf->total,
				 sizeof(buf->total),
				 -EFAULT);
		++d->granted_count;
	}
	return 0;
}

int mga_dma(struct inode *inode, struct file *filp, unsigned int cmd,
	    unsigned long arg)
{
	drm_file_t	  *priv	    = filp->private_data;
	drm_device_t	  *dev	    = priv->dev;
	drm_device_dma_t  *dma	    = dev->dma;
   	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
      	__volatile__ unsigned int *status = 
     		(__volatile__ unsigned int *)dev_priv->status_page;
	int		  retcode   = 0;
	drm_dma_t	  d;

   	copy_from_user_ret(&d, (drm_dma_t *)arg, sizeof(d), -EFAULT);
	DRM_DEBUG("%d %d: %d send, %d req\n",
		  current->pid, d.context, d.send_count, d.request_count);

	/* Please don't send us buffers.
	 */
	if (d.send_count != 0) {
		DRM_ERROR("Process %d trying to send %d buffers via drmDMA\n",
			  current->pid, d.send_count);
		return -EINVAL;
	}
	
	/* We'll send you buffers.
	 */
	if (d.request_count < 0 || d.request_count > dma->buf_count) {
		DRM_ERROR("Process %d trying to get %d buffers (of %d max)\n",
			  current->pid, d.request_count, dma->buf_count);
		return -EINVAL;
	}
	
	d.granted_count = 0;

	if (d.request_count) {
		retcode = mga_dma_get_buffers(dev, &d);
	}

	DRM_DEBUG("%d returning, granted = %d\n",
		  current->pid, d.granted_count);
	copy_to_user_ret((drm_dma_t *)arg, &d, sizeof(d), -EFAULT);
      	sarea_priv->last_dispatch = status[1];
	return retcode;
}
