/* mach64.h -- ATI Mach 64 DRM template customization -*- linux-c -*-
 * Created: Wed Feb 14 16:07:10 2001 by gareth@valinux.com
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
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Gareth Hughes <gareth@valinux.com>
 *    Leif Delgass <ldelgass@retinalburn.net>
 */

#ifndef __MACH64_H__
#define __MACH64_H__

/* This remains constant for all DRM template files.
 */
#define DRM(x) mach64_##x

/* General customization:
 */
#define __HAVE_AGP		1
#define __MUST_HAVE_AGP		0
#define __HAVE_MTRR		1
#define __HAVE_CTX_BITMAP	1
#define __HAVE_PCI_DMA		1

/* DMA customization:
 */
#define __HAVE_DMA		1
#define __HAVE_DMA_FREELIST     0

#define MACH64_INTERRUPTS       0

#if MACH64_INTERRUPTS
#define __HAVE_DMA_IRQ          1
#define __HAVE_DMA_IRQ_BH       1
#define __HAVE_SHARED_IRQ       1

/* called before installing service routine in _irq_install */
#define DRIVER_PREINSTALL()						\
do {									\
	u32 tmp;							\
	drm_mach64_private_t *dev_priv = dev->dev_private;		\
									\
	tmp = MACH64_READ(MACH64_CRTC_INT_CNTL);			\
        DRM_DEBUG("Before PREINSTALL: CRTC_INT_CNTL = 0x%08x\n", tmp);	\
	/* clear active interrupts */					\
	if ( tmp & (MACH64_CRTC_VBLANK_INT				\
		    | MACH64_CRTC_BUSMASTER_EOL_INT) ) {		\
		/* ack bits are the same as active interrupt bits, */	\
		/* so write back tmp to clear active interrupts */	\
		MACH64_WRITE( MACH64_CRTC_INT_CNTL, tmp );		\
	}								\
									\
	/* disable interrupts */					\
	tmp &= ~(MACH64_CRTC_VBLANK_INT_EN 				\
		 | MACH64_CRTC_BUSMASTER_EOL_INT_EN);			\
	MACH64_WRITE( MACH64_CRTC_INT_CNTL, tmp );			\
        DRM_DEBUG("After PREINSTALL: CRTC_INT_CNTL = 0x%08x\n", tmp);	\
									\
} while(0)

/* called after installing service routine in _irq_install */
#define DRIVER_POSTINSTALL()						\
do {									\
	/* clear and enable interrupts */				\
	u32 tmp;							\
	drm_mach64_private_t *dev_priv = dev->dev_private;		\
									\
	tmp = MACH64_READ(MACH64_CRTC_INT_CNTL);			\
        DRM_DEBUG("Before POSTINSTALL: CRTC_INT_CNTL = 0x%08x\n", tmp);	\
	/* clear active interrupts */					\
	if ( tmp & (MACH64_CRTC_VBLANK_INT 				\
		    | MACH64_CRTC_BUSMASTER_EOL_INT) ) {		\
		/* ack bits are the same as active interrupt bits, */	\
		/* so write back tmp to clear active interrupts */	\
		MACH64_WRITE( MACH64_CRTC_INT_CNTL, tmp );		\
	}								\
									\
	/* enable interrupts */						\
	tmp |= (MACH64_CRTC_VBLANK_INT_EN 				\
		| MACH64_CRTC_BUSMASTER_EOL_INT_EN);			\
	MACH64_WRITE( MACH64_CRTC_INT_CNTL, tmp );			\
        DRM_DEBUG("After POSTINSTALL: CRTC_INT_CNTL = 0x%08x\n", tmp);	\
									\
} while(0)

/* called before freeing irq in _irq_uninstall */
#define DRIVER_UNINSTALL()							\
do {										\
	u32 tmp;								\
	drm_mach64_private_t *dev_priv = dev->dev_private;			\
	if (dev_priv) {								\
		tmp = MACH64_READ(MACH64_CRTC_INT_CNTL);			\
        	DRM_DEBUG("Before UNINSTALL: CRTC_INT_CNTL = 0x%08x\n", tmp);	\
		/* clear active interrupts */					\
		if ( tmp & (MACH64_CRTC_VBLANK_INT 				\
			    | MACH64_CRTC_BUSMASTER_EOL_INT) ) {		\
			/* ack bits are the same as active interrupt bits, */	\
			/* so write back tmp to clear active interrupts */	\
			MACH64_WRITE( MACH64_CRTC_INT_CNTL, tmp );		\
		}								\
										\
		/* disable interrupts */					\
		tmp &= ~(MACH64_CRTC_VBLANK_INT_EN 				\
			 | MACH64_CRTC_BUSMASTER_EOL_INT_EN);			\
		MACH64_WRITE( MACH64_CRTC_INT_CNTL, tmp );			\
        	DRM_DEBUG("After UNINSTALL: CRTC_INT_CNTL = 0x%08x\n", tmp);	\
	}									\
} while(0)

#endif /* MACH64_INTERRUPTS */

/* Buffer customization:
 */

#define DRIVER_AGP_BUFFERS_MAP( dev )					\
	((drm_mach64_private_t *)((dev)->dev_private))->buffers

#endif
