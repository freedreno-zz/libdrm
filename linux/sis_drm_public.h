/* sis_drm_public.h -- Private header for Direct Rendering Manager -*- linux-c -*-
 * Created: Mon Jan  4 10:05:05 1999 by sclin@sis.com.tw
 *
 * Copyright 2000 Silicon Integrated Systems Corp, Inc., HsinChu, Taiwan.
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
 * Authors:
 *    Sung-Ching Lin <sclin@sis.com.tw>
 * 
 */
/* $XFree86: xc/programs/Xserver/hw/xfree86/os-support/linux/drm/kernel/sis_drm_public.h,v 1.2 2000/08/04 03:51:47 tsi Exp $ */

#ifndef _sis_drm_public_h_
#define _sis_drm_public_h_

typedef struct { 
  int context;
  unsigned int offset;
  unsigned int size;
  unsigned int free;
} drm_sis_mem_t; 

typedef struct { 
  unsigned int offset, size;
} drm_sis_agp_t; 

typedef struct { 
  unsigned int left, right;
} drm_sis_flip_t; 

#define SIS_IOCTL_FB_ALLOC     DRM_IOWR( 0x44, drm_sis_mem_t)
#define SIS_IOCTL_FB_FREE      DRM_IOW( 0x45, drm_sis_mem_t)

#define SIS_IOCTL_AGP_INIT     DRM_IOWR( 0x53, drm_sis_agp_t)
#define SIS_IOCTL_AGP_ALLOC    DRM_IOWR( 0x54, drm_sis_mem_t)
#define SIS_IOCTL_AGP_FREE     DRM_IOW( 0x55, drm_sis_mem_t)

#define SIS_IOCTL_FLIP         DRM_IOW( 0x48, drm_sis_flip_t)
#define SIS_IOCTL_FLIP_INIT    DRM_IO( 0x49)
#define SIS_IOCTL_FLIP_FINAL   DRM_IO( 0x50)

#endif
