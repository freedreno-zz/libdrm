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

#if 0
/* How does this work???
 */
int mgaEmitClipRect(drm_device_t *dev, xf86drmClipRectRec *box)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->ContextState;

	/* The G400 seems to have an issue with the second WARP not
	 * stalling clipper register writes.  This bothers me, but the only
	 * way I could get it to never clip the last triangle under any
	 * circumstances is by inserting TWO dwgsync commands.
	 */
 	if (dev_priv->chipset == MGA_CARD_TYPE_G400) { 
		DMAOUTREG( MGAREG_DWGSYNC, 0 );
		DMAOUTREG( MGAREG_DWGSYNC, 0 );
	}

	DMAOUTREG( MGAREG_CXBNDRY, ((box->x2)<<16)|(box->x1) );
	DMAOUTREG( MGAREG_YTOP, box->y1 * dev_priv->pitch );
	DMAOUTREG( MGAREG_YBOT, box->y2 * dev_priv->pitch );
	DMAADVANCE();
	return 0;
}
#endif


static int mgaEmitContext(drm_device_t *dev, drm_buf_t *buf)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->ContextState;
	DMALOCALS;
	
	/* Disallow all write destinations except the front and backbuffer.
	 */
	if (regs[MGA_CTXREG_DSTORG] != dev_priv->frontOrg &&
	    regs[MGA_CTXREG_DSTORG] != dev_priv->backOrg)
		return -1;
	
	DMAGETPTR( buf );
	DMAOUTREG( MGAREG_DSTORG, regs[MGA_CTXREG_DSTORG] );
	DMAOUTREG( MGAREG_MACCESS, regs[MGA_CTXREG_MACCESS] );
	DMAOUTREG( MGAREG_PLNWT, regs[MGA_CTXREG_PLNWT] );
	DMAOUTREG( MGAREG_DWGCTL, regs[MGA_CTXREG_DWGCTL] );
	DMAOUTREG( MGAREG_ALPHACTRL, regs[MGA_CTXREG_ALPHACTRL] );
	DMAOUTREG( MGAREG_FOGCOL, regs[MGA_CTXREG_FOGCOLOR] );
	DMAOUTREG( MGAREG_WFLAG, regs[MGA_CTXREG_WFLAG] );

 	if (dev_priv->chipset == MGA_CARD_TYPE_G400) { 
		DMAOUTREG( MGAREG_WFLAG1, regs[MGA_CTXREG_WFLAG] );
		DMAOUTREG( MGAREG_TDUALSTAGE0, regs[MGA_CTXREG_TDUAL0] );
		DMAOUTREG( MGAREG_TDUALSTAGE1, regs[MGA_CTXREG_TDUAL1] );      
	}   
   
	DMAADVANCE( buf );
	return 0;
}

/* The texture state routines are quite similar, but are a real mess
 * when integrated into a single function. 
 */
static int mgaG200EmitTex(drm_device_t *dev, drm_buf_t *buf)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->Tex0State;

	DMALOCALS;

	/* Disallow texture reads from PCI space.
	 */
	if ((regs[MGA_TEXREG_ORG] & 0x3) == 0x1)
		return -1;

	DMAGETPTR( buf );
	DMAOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] );
	DMAOUTREG(MGAREG_TEXCTL, regs[MGA_TEXREG_CTL] );
	DMAOUTREG(MGAREG_TEXFILTER, regs[MGA_TEXREG_FILTER] );
	DMAOUTREG(MGAREG_TEXBORDERCOL, regs[MGA_TEXREG_BORDERCOL] );
	DMAOUTREG(MGAREG_TEXORG, regs[MGA_TEXREG_ORG] );
	DMAOUTREG(MGAREG_TEXORG1, regs[MGA_TEXREG_ORG1] );
	DMAOUTREG(MGAREG_TEXORG2, regs[MGA_TEXREG_ORG2] );
	DMAOUTREG(MGAREG_TEXORG3, regs[MGA_TEXREG_ORG3] );
	DMAOUTREG(MGAREG_TEXORG4, regs[MGA_TEXREG_ORG4] );		
	DMAOUTREG(MGAREG_TEXWIDTH, regs[MGA_TEXREG_WIDTH] );
	DMAOUTREG(MGAREG_TEXHEIGHT, regs[MGA_TEXREG_HEIGHT] );
   
	DMAOUTREG(0x2d00 + 24*4, regs[MGA_TEXREG_WIDTH] );
	DMAOUTREG(0x2d00 + 34*4, regs[MGA_TEXREG_HEIGHT] );

	DMAADVANCE( buf );  

	return 0;
}

static int mgaG400EmitTex0( drm_device_t *dev, drm_buf_t *buf )
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->Tex0State;
	int multitex = sarea_priv->WarpPipe & MGA_T2;

	DMALOCALS;
	DMAGETPTR( buf );

	/* Disallow texture reads from PCI space.
	 */
	if ((regs[MGA_TEXREG_ORG] & 0x3) == 0x1)
		return -1;

	DMAOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] );
	DMAOUTREG(MGAREG_TEXCTL, regs[MGA_TEXREG_CTL] );
	DMAOUTREG(MGAREG_TEXFILTER, regs[MGA_TEXREG_FILTER] );
	DMAOUTREG(MGAREG_TEXBORDERCOL, regs[MGA_TEXREG_BORDERCOL] );
	DMAOUTREG(MGAREG_TEXORG, regs[MGA_TEXREG_ORG] );
	DMAOUTREG(MGAREG_TEXORG1, regs[MGA_TEXREG_ORG1] );
	DMAOUTREG(MGAREG_TEXORG2, regs[MGA_TEXREG_ORG2] );
	DMAOUTREG(MGAREG_TEXORG3, regs[MGA_TEXREG_ORG3] );
	DMAOUTREG(MGAREG_TEXORG4, regs[MGA_TEXREG_ORG4] );		
	DMAOUTREG(MGAREG_TEXWIDTH, regs[MGA_TEXREG_WIDTH] );
	DMAOUTREG(MGAREG_TEXHEIGHT, regs[MGA_TEXREG_HEIGHT] );
   
	DMAOUTREG(0x2d00 + 49*4, 0);
	DMAOUTREG(0x2d00 + 57*4, 0);
	DMAOUTREG(0x2d00 + 53*4, 0);
	DMAOUTREG(0x2d00 + 61*4, 0);

	if (!multitex) {
		DMAOUTREG(0x2d00 + 52*4, 0x40 ); 
		DMAOUTREG(0x2d00 + 60*4, 0x40 );
	} 

	DMAOUTREG(0x2d00 + 54*4, regs[MGA_TEXREG_WIDTH] | 0x40 );
	DMAOUTREG(0x2d00 + 62*4, regs[MGA_TEXREG_HEIGHT] | 0x40 );

	DMAADVANCE( buf );  
	return 0;
}

#define TMC_map1_enable 		0x80000000 	


static int mgaG400EmitTex1( drm_device_t *dev, drm_buf_t *buf )
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int *regs = sarea_priv->Tex1State;

	DMALOCALS;
	DMAGETPTR(buf);

	/* Disallow texture reads from PCI space.
	 */
	if ((regs[MGA_TEXREG_ORG] & 0x3) == 0x1)
		return -1;

	DMAOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] | TMC_map1_enable);
	DMAOUTREG(MGAREG_TEXCTL, regs[MGA_TEXREG_CTL] );
	DMAOUTREG(MGAREG_TEXFILTER, regs[MGA_TEXREG_FILTER] );
	DMAOUTREG(MGAREG_TEXBORDERCOL, regs[MGA_TEXREG_BORDERCOL] );
	DMAOUTREG(MGAREG_TEXORG, regs[MGA_TEXREG_ORG] );
	DMAOUTREG(MGAREG_TEXORG1, regs[MGA_TEXREG_ORG1] );
	DMAOUTREG(MGAREG_TEXORG2, regs[MGA_TEXREG_ORG2] );
	DMAOUTREG(MGAREG_TEXORG3, regs[MGA_TEXREG_ORG3] );
	DMAOUTREG(MGAREG_TEXORG4, regs[MGA_TEXREG_ORG4] );		
	DMAOUTREG(MGAREG_TEXWIDTH, regs[MGA_TEXREG_WIDTH] );
	DMAOUTREG(MGAREG_TEXHEIGHT, regs[MGA_TEXREG_HEIGHT] );
   
	DMAOUTREG(0x2d00 + 49*4, 0);
	DMAOUTREG(0x2d00 + 57*4, 0);
	DMAOUTREG(0x2d00 + 53*4, 0);
	DMAOUTREG(0x2d00 + 61*4, 0);

	DMAOUTREG(0x2d00 + 52*4, regs[MGA_TEXREG_WIDTH] | 0x40 ); 
	DMAOUTREG(0x2d00 + 60*4, regs[MGA_TEXREG_HEIGHT] | 0x40 );

	DMAOUTREG(MGAREG_TEXCTL2, regs[MGA_TEXREG_CTL2] );

	DMAADVANCE( buf );     
	return 0;
}



/* WIADDR might not work in sec bufs, might need to use
 * the primary buffer
 */
static int mgaG400EmitPipe(drm_device_t *dev, drm_buf_t *buf)
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->WarpPipe;
	float fParam = 12800.0f;
	DMALOCALS;
   

	if (pipe > MGA_MAX_G400_PIPES) 
		return -1;

	DMAGETPTR(buf);
	DMAOUTREG(MGAREG_WIADDR2, WIA_wmode_suspend);
   
	/* Establish vertex size.  
	 */
	if (pipe & MGA_T2) {
		DMAOUTREG(MGAREG_WVRTXSZ, 0x00001e09);
		DMAOUTREG(MGAREG_WACCEPTSEQ, 0x1e000000);
	} else {
		DMAOUTREG(MGAREG_WVRTXSZ, 0x00001807);
		DMAOUTREG(MGAREG_WACCEPTSEQ, 0x18000000);
	}
   
	DMAOUTREG(MGAREG_WFLAG, 0);
	DMAOUTREG(MGAREG_WFLAG1, 0);
   
	DMAOUTREG(0x2d00 + 56*4, *((u32 *)(&fParam)));
	DMAOUTREG(MGAREG_DMAPAD, 0);
	DMAOUTREG(MGAREG_DMAPAD, 0);
   
	DMAOUTREG(0x2d00 + 49*4, 0);  /* Tex stage 0 */
	DMAOUTREG(0x2d00 + 57*4, 0);  /* Tex stage 0 */
	DMAOUTREG(0x2d00 + 53*4, 0);  /* Tex stage 1 */
	DMAOUTREG(0x2d00 + 61*4, 0);  /* Tex stage 1 */
   
	DMAOUTREG(0x2d00 + 54*4, 0x40); /* Tex stage 0 : w */
	DMAOUTREG(0x2d00 + 62*4, 0x40); /* Tex stage 0 : h */
	DMAOUTREG(0x2d00 + 52*4, 0x40); /* Tex stage 1 : w */
	DMAOUTREG(0x2d00 + 60*4, 0x40); /* Tex stage 1 : h */
   
	/* Dma pading required due to hw bug */
	DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
	DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
	DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
	DMAOUTREG(MGAREG_WIADDR2, (dev_priv->WarpIndex[pipe].phys_addr | 
				   WIA_wmode_start | WIA_wagp_agp));
	DMAADVANCE(buf);
	return 0;
}

static int mgaG200EmitPipe( drm_device_t *dev, drm_buf_t *buf )
{
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int pipe = sarea_priv->WarpPipe;
	DMALOCALS;
   
	if (pipe > MGA_MAX_G200_PIPES) 
		return -1;

	DMAGETPTR(buf);
	DMAOUTREG(MGAREG_WIADDR, WIA_wmode_suspend);
	DMAOUTREG(MGAREG_WVRTXSZ, 7);
	DMAOUTREG(MGAREG_WFLAG, 0);
	DMAOUTREG(0x2d00 + 24*4, 0); /* tex w/h */
   
	DMAOUTREG(0x2d00 + 25*4, 0x100);
	DMAOUTREG(0x2d00 + 34*4, 0); /* tex w/h */
	DMAOUTREG(0x2d00 + 42*4, 0xFFFF);
	DMAOUTREG(0x2d00 + 60*4, 0xFFFF);
   
	/* Dma pading required due to hw bug */
	DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
	DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
	DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
	DMAOUTREG(MGAREG_WIADDR, (dev_priv->WarpIndex[pipe].phys_addr | 
				  WIA_wmode_start | WIA_wagp_agp));

	DMAADVANCE(buf);
	return 0;
}



void mgaEmitState( drm_device_t *dev )
{
	drm_buf_t *buf;
	drm_device_dma_t *dma = dev->dma;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	unsigned int dirty = sarea_priv->dirty ;
	int rv = 0;

	int order = 1;		/*  ???? */

	/* Put all state onto a single buffer.
	 */
	buf = drm_freelist_get(&dma->bufs[order].freelist,_DRM_DMA_WAIT);


	if (dev_priv->chipset == MGA_CARD_TYPE_G400) {	   

		if (dirty & MGASAREA_NEW_CONTEXT)
			rv |= mgaEmitContext( dev, buf );

		if (dirty & MGASAREA_NEW_TEX1)
			rv |= mgaG400EmitTex1( dev, buf );
	   
		if (dirty & MGASAREA_NEW_TEX0)
			rv |= mgaG400EmitTex0( dev, buf );

		if (dirty & MGASAREA_NEW_PIPE) 
			rv |= mgaG400EmitPipe( dev, buf );

	} else {

		if (dirty & MGASAREA_NEW_CONTEXT)
			rv |= mgaEmitContext( dev, buf );

		if (dirty & MGASAREA_NEW_TEX0)
			rv |= mgaG200EmitTex( dev, buf );

		if (dirty & MGASAREA_NEW_PIPE) 
			rv |= mgaG200EmitPipe( dev, buf );
	}	  
	   
	  
	if (rv == 0) {
		drm_dma_t d;

		sarea_priv->dirty = 0;

		d.context = DRM_KERNEL_CONTEXT;
		d.send_count = 1;
		d.send_indices = &buf->idx;
		d.send_sizes = &buf->used;
		d.flags = _DRM_DMA_GENERAL;
		d.request_count = 0;
		d.request_size = 0;
		d.request_indices = NULL;
		d.request_sizes = NULL;
		d.granted_count = 0;	   
		drm_dma_enqueue(dev, &d);
	} else {
		/* Won't render anything till we get a good state from a
		 * client.
		 */
		sarea_priv->dirty = ~0; 
		drm_freelist_put( dev, &dma->bufs[order].freelist, buf );
	}
}
