/* mga_drm_public.h -- Public header for the Matrox g200/g400 driver -*- linux-c -*-
 * Created: Tue Jan 25 01:50:01 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
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
 * Author: Jeff Hartmann <jhartmann@precisioninsight.com>
 *
 * $XFree86$
 */

#ifndef _MGA_DRM_PUBLIC_H_
#define _MGA_DRM_PUBLIC_H_

/* These defines must match the Xserver code */
#define MGA_WARP_TGZ            0
#define MGA_WARP_TGZA           1
#define MGA_WARP_TGZAF          2
#define MGA_WARP_TGZF           3
#define MGA_WARP_TGZS           4
#define MGA_WARP_TGZSA          5
#define MGA_WARP_TGZSAF         6
#define MGA_WARP_TGZSF          7
#define MGA_WARP_T2GZ           8
#define MGA_WARP_T2GZA          9
#define MGA_WARP_T2GZAF         10
#define MGA_WARP_T2GZF          11
#define MGA_WARP_T2GZS          12
#define MGA_WARP_T2GZSA         13
#define MGA_WARP_T2GZSAF        14
#define MGA_WARP_T2GZSF         15

#define MGA_MAX_WARP_PIPES 16

#define MGA_CARD_TYPE_G200 1
#define MGA_CARD_TYPE_G400 2

/* Bogus values */

#define MGA_X_SETUP_SIZE   16
#define MGA_SETUP_SIZE     16
#define MGA_TEX_SETUP_SIZE 16

typedef struct _drm_mga_warp_index {
   	int installed;
   	unsigned long phys_addr;
   	int size;
} mgaWarpIndex;

typedef struct drm_mga_init {
   	enum { 
	   	MGA_INIT_DMA = 0x01,
	       	MGA_CLEANUP_DMA = 0x02
	} func;
   	int reserved_map_agpstart;
   	int reserved_map_idx;
   	int buffer_map_idx;
   	int sarea_priv_offset;
   	int primary_size;
   	int warp_mc_size;
   	int type;
   	int fbOffset;
   	int backOffset;
   	int depthOffset;
   	int textureOffset;
   	int textureSize;
   	int cpp;
   	int stride;
   	int sgram;
   	mgaWarpIndex WarpIndex[MGA_MAX_WARP_PIPES];
} drm_mga_init_t;

typedef struct _xf86drmClipRectRec {
   	unsigned short x1;
   	unsigned short y1;
   	unsigned short x2;
   	unsigned short y2;
} xf86drmClipRectRec;

#define _MGA_2D_DIRTY 0
/* These aren't finals, its just what the Xserver uses */
#define _MGA_SETUP_PITCH 0
#define _MGA_SETUP_CXBNDRY 1
#define _MGA_SETUP_YTOP 2
#define _MGA_SETUP_YBOT 3
#define _MGA_SETUP_DSTORG 4
#define _MGA_SETUP_MACCESS 5
#define _MGA_SETUP_PLNWT 6
#define _MGA_SETUP_ZORG 7
#define _MGA_SETUP_YDSTLEN 8
#define _MGA_SETUP_YDSTORG 9
#define _MGA_SETUP_FXBNDRY 10
#define _MGA_SETUP_SRCORG 11
#define _MGA_SETUP_DSTORG 12
#define _MGA_SETUP_SGN 13
#define _MGA_SETUP_AR0 14
#define _MGA_SETUP_AR1 15
#define _MGA_SETUP_AR2 16
#define _MGA_SETUP_AR3 17
#define _MGA_SETUP_AR4 18
#define _MGA_SETUP_AR5 19
#define _MGA_SETUP_AR6 20
#define _MGA_SETUP_CXRIGHT 21
#define _MGA_SETUP_DWGCTL 22
#define _MGA_SETUP_XYEND 23
#define _MGA_SETUP_XYSTRT 24
#define _MGA_SETUP_FCOL 25
#define _MGA_SETUP_BCOL 26
#define _MGA_SETUP_PAT0 27
#define _MGA_SETUP_PAT1 28
#define _MGA_SETUP_SHIFT 29
#define _MGA_SETUP_SRC0 30
#define _MGA_SETUP_SRC1 31
#define _MGA_SETUP_SRC2 32
#define _MGA_SETUP_SRC3 33
#define _MGA_SETUP_OPMODE 34
#define _MGA_SETUP_WIADDR2 35
#define _MGA_SETUP_WGETMSB 36
#define _MGA_SETUP_WVRTXSZ 37
#define _MGA_SETUP_WACCEPTSEQ 38
#define _MGA_SETUP_WIADDR 39
#define _MGA_SETUP_WMISC 40



typedef struct _drm_mga_sarea {
   	int CtxOwner;
   	int TexOwner;
   	unsigned long ServerState[MGA_X_SETUP_SIZE];
   	unsigned long ContextState[MGA_SETUP_SIZE];
   	unsigned long Tex0State[MGA_TEX_SETUP_SIZE];
   	unsigned long Tex1State[MGA_TEX_SETUP_SIZE];
   	int WarpPipe;
   	unsigned long NewState;
   	int nbox;
   	xf86drmClipRectRec boxes[256];
} drm_mga_sarea_t;

#define DRM_IOCTL_MGA_INIT    DRM_IOW( 0x40, drm_mga_init_t)

#endif
