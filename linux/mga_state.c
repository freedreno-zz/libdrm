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

void mga2drefresh(drm_device_t *dev) 
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
   
}

void mgaContextRefresh(drm_device_t *dev)
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
   
}

void mgaTex0Refresh(drm_device_t *dev)
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
   
}

void mgaTex1Refresh(drm_device_t *dev)
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
   
}

/* WIADDR might not work in sec bufs, might need to use
 * the primary buffer
 */
int mgaG400SwitchPipe(drm_device_t *dev, void *code)
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
   drm_device_dma_t *dma = dev->dma;
   drm_dma_t d;
   drm_buf_t *buf;
   float fParam = 12800.0f;
   DMALOCALS();
   
   buf = drm_freelist_get(&dma->bufs[order].freelist,_DRM_DMA_WAIT);
   
   /* This needs to get a buffer to fill */
   
   DMAGETPTR(buf);
   DMAOUTREG(MGAREG_WIADDR2, WIA_wmode_suspend);
   
   if (sarea_priv->WarpPipe >= 8) {
      DMAOUTREG(MGAREG_WVRTXSZ, 0x00001e09);
      DMAOUTREG(MGAREG_WACCEPTSEQ, 0x1e000000);
   } else {
      DMAOUTREG(MGAREG_WVRTXSZ, 0x00001807);
      DMAOUTREG(MGAREG_WACCEPTSEQ, 0x18000000);
   }
   
   DMAOUTREG(MGAREG_WFLAG, 0);
   DMAOUTREG(MGAREG_WFLAG1, 0);
   
   DMAOUTREG(0x2d00 + 56*4, *((mgaUI32 *)(&fParam)));
   DMAOUTREG(MGAREG_DMAPAD, 0);
   DMAOUTREG(MGAREG_DMAPAD, 0);
   
   /* Perhaps only the registers for the Tex Stage 1 should
    * be set on the microcode that does multitex, however it
    * makes no difference :(
    */
   DMAOUTREG(0x2d00 + 49*4, 0);  /* Tex stage 0 */
   DMAOUTREG(0x2d00 + 57*4, 0);  /* Tex stage 0 */
   DMAOUTREG(0x2d00 + 53*4, 0);  /* Tex stage 1 */
   DMAOUTREG(0x2d00 + 61*4, 0);  /* Tex stage 1 */
   
   DMAOUTREG(0x2d00 + 54*4, 0x40); /* Tex stage 0 : w */
   DMAOUTREG(0x2d00 + 62*4, 0x40); /* Tex stage 0 : h */
   DMAOUTREG(0x2d00 + 52*4, 0x40); /* Tex stage 1 : w */
   DMAOUTREG(0x2d00 + 60*4, 0x40); /* Tex stage 1 : h */
   
   /* Attention! dma pading required due to hw bug (see specs) */
   DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
   DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
   DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
   DMAOUTREG(MGAREG_WIADDR2,(unsigned long)(code) | WIA_wmode_start |
	     WIA_wagp_agp);
   DMAADVANCE(buf);
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
   return 0;
}

int mgaG200SwitchPipe(drm_device_t *dev, void *code)
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_device_dma_t *dma = dev->dma;
   drm_dma_t d;
   drm_buf_t *buf;
   DMALOCALS;
   
   /* This needs to get a buffer to fill */
   buf = drm_freelist_get(&dma->bufs[order].freelist,_DRM_DMA_WAIT);
   
   DMAGETPTR(buf);
   DMAOUTREG(MGAREG_WIADDR, WIA_wmode_suspend);
   DMAOUTREG(MGAREG_WVRTXSZ, 7);
   DMAOUTREG(MGAREG_WFLAG, 0);
   DMAOUTREG(0x2d00 + 24*4, 0); // tex w/h
   
   DMAOUTREG(0x2d00 + 25*4, 0x100);
   DMAOUTREG(0x2d00 + 34*4, 0); // tex w/h
   DMAOUTREG(0x2d00 + 42*4, 0xFFFF);
   DMAOUTREG(0x2d00 + 60*4, 0xFFFF);
   
   /* Attention! dma pading required due to hw bug (see specs) */
   DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
   DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
   DMAOUTREG(MGAREG_DMAPAD, 0xffffffff);
   DMAOUTREG(MGAREG_WIADDR,(unsigned long)(code) | WIA_wmode_start |
	     WIA_wagp_agp);
   DMAADVANCE(buf);
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
   return 0;
}

void mgaWarpPipeRefresh(drm_device_t *dev)
{
   drm_mga_private_t *dev_priv = (drm_mga_private_t *)dev->dev_private;
   drm_mga_sarea_t *sarea_priv = dev_priv->sarea_priv;
   void *code;
   int failure;
   
   code = (void *)dev_priv->WarpIndex[sarea_priv->WarpPipe].phys_addr;
   
   if(!code) {
      printk("Invalid warp code selected : %d\n", sarea_priv->WarpPipe);
      code = (void *)dev_priv->WarpIndex[0].phys_addr;
      
      if(!code) {
	 DRM_ERROR("No Warp Pipes Loaded\n");
	 return;
      }
   }
   switch(dev_priv->type) {
    case MGA_CARD_TYPE_G400:
      failure = mgaG400SwitchPipe(dev, code);
      break;
    case MGA_CARD_TYPE_G200:
      failure = mgaG200SwitchPipe(dev, code);
      break;
   }
   if(failure) {
      DRM_ERROR("Failed to switch warp pipes to : %d\n", 
		sarea_priv->WarpPipe);
   }
}
