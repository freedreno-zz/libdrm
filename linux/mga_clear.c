/* mga_state.c -- State support for mga g200/g400 -*- linux-c -*-
 *
 * Created: February 2000 by keithw@precisioninsight.com
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
 * Authors: 
 *           Keith Whitwell <keithw@precisioninsight.com>
 *
 */
 
#define __NO_VERSION__
#include "drmP.h"
#include "mga_drv.h"
#include "mgareg_flags.h"
#include "mga_dma.h"

#define MGA_CLEAR_CMD (DC_opcod_trap | DC_arzero_enable | 		\
		       DC_sgnzero_enable | DC_shftzero_enable | 	\
		       (0xC << DC_bop_SHIFT) | DC_clipdis_enable | 	\
		       DC_solid_enable | DC_transc_enable)
	  

#define MGA_COPY_CMD (DC_opcod_bitblt | DC_atype_rpl | DC_linear_xy |	\
		      DC_solid_disable | DC_arzero_disable | 		\
		      DC_sgnzero_enable | DC_shftzero_enable | 		\
		      (0xC << DC_bop_SHIFT) | DC_bltmod_bfcol | 	\
		      DC_pattern_disable | DC_transc_disable | 		\
		      DC_clipdis_enable)				\


void mgaClearBuffers( drm_device_t *dev,
		      int clear_color,
		      int clear_depth,
		      int flags )
{
	int cmd, i;	
	drm_device_dma_t *dma = dev->dma;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;   
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	xf86drmClipRectRec *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	drm_buf_t *buf;
	drm_dma_t d;
	int order = 10;		/* ??? */
	DMALOCALS;


	if (!nbox) 
		return;

	if ( dev_priv->sgram ) 
		cmd = MGA_CLEAR_CMD | DC_atype_blk;
	else
		cmd = MGA_CLEAR_CMD | DC_atype_rstr;
	    
	buf = drm_freelist_get(&dma->bufs[order].freelist, _DRM_DMA_WAIT);


	DMAGETPTR( buf );

	for (i = 0 ; i < nbox ; i++) {
		unsigned int height = pbox[i].y2 - pbox[i].y1;
		
		/* Is it necessary to be this paranoid?  I don't think so.
		if (pbox[i].x1 > dev_priv->width) continue;
		if (pbox[i].y1 > dev_priv->height) continue;
		if (pbox[i].x2 > dev_priv->width) continue;
		if (pbox[i].y2 > dev_priv->height) continue;
		if (pbox[i].x2 <= pbox[i].x1) continue;
		if (pbox[i].y2 <= pbox[i].x1) continue;
		 */

		DMAOUTREG(MGAREG_YDSTLEN, (pbox[i].y1<<16)|height);
		DMAOUTREG(MGAREG_FXBNDRY, (pbox[i].x2<<16)|pbox[i].x1);

		if ( flags & MGA_CLEAR_FRONTBUFFER ) {	    
			DMAOUTREG(MGAREG_FCOL, clear_color);
			DMAOUTREG(MGAREG_DSTORG, dev_priv->frontOrg);
			DMAOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, cmd );
		}

		if ( flags & MGA_CLEAR_BACKBUFFER ) {
			DMAOUTREG(MGAREG_FCOL, clear_color);
			DMAOUTREG(MGAREG_DSTORG, dev_priv->backOrg);
			DMAOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, cmd );
		}

		if ( flags & MGA_CLEAR_DEPTHBUFFER ) 
		{
			DMAOUTREG(MGAREG_FCOL, clear_depth);
			DMAOUTREG(MGAREG_DSTORG, dev_priv->depthOrg);
			DMAOUTREG(MGAREG_DWGCTL+MGAREG_MGA_EXEC, cmd );
		}
	}

	DMAADVANCE( buf );

	/* Make sure we restore the 3D state next time.
	 */
	sarea_priv->dirty |= MGASAREA_NEW_CONTEXT;

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
}


void mgaSwapBuffers( drm_device_t *dev ) 
{
	drm_device_dma_t *dma = dev->dma;
	drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
	drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
	xf86drmClipRectRec *pbox = sarea_priv->boxes;
	int nbox = sarea_priv->nbox;
	drm_buf_t *buf;
	drm_dma_t d;
	int order = 10;		/* ??? */
	int i;
	DMALOCALS;	

	if (!nbox) 
		return;

	buf = drm_freelist_get(&dma->bufs[order].freelist, _DRM_DMA_WAIT);

	DMAGETPTR( buf );

	DMAOUTREG(MGAREG_DSTORG, dev_priv->frontOrg);
	DMAOUTREG(MGAREG_MACCESS, dev_priv->mAccess);
	DMAOUTREG(MGAREG_SRCORG, dev_priv->backOrg);
	DMAOUTREG(MGAREG_AR5, dev_priv->stride); /* unnecessary? */
	DMAOUTREG(MGAREG_DWGCTL, MGA_COPY_CMD); 
	     
	for (i = 0 ; i < nbox; i++) {
		unsigned int h = pbox[i].y2 - pbox[i].y1;
		unsigned int start = pbox[i].y1 * dev_priv->stride;

		/*
		if (pbox[i].x1 > dev_priv->width) continue;
		if (pbox[i].y1 > dev_priv->height) continue;
		if (pbox[i].x2 > dev_priv->width) continue;
		if (pbox[i].y2 > dev_priv->height) continue;
		if (pbox[i].x2 <= pbox[i].x1) continue;
		if (pbox[i].y2 <= pbox[i].x1) continue;		
		*/

		DMAOUTREG(MGAREG_AR0, start + pbox[i].x2 - 1);
		DMAOUTREG(MGAREG_AR3, start + pbox[i].x1);		
		DMAOUTREG(MGAREG_FXBNDRY, pbox[i].x1|((pbox[i].x2 - 1)<<16));
		DMAOUTREG(MGAREG_YDSTLEN+MGAREG_MGA_EXEC, (pbox[i].y1<<16)|h);
	}
  
	DMAOUTREG(MGAREG_SRCORG, 0);
	DMAADVANCE( buf );

	/* Make sure we restore the 3D state next time.
	 */
	sarea_priv->dirty |= MGASAREA_NEW_CONTEXT;

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
}

