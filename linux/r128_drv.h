/* r128_drv.h -- Private header for r128 driver -*- linux-c -*-
 * Created: Mon Dec 13 09:51:11 1999 by faith@precisioninsight.com
 * Revised: Mon Dec 13 09:51:39 1999 by faith@precisioninsight.com
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
 * $PI$
 * $XFree86$
 * 
 */

#ifndef _R128_DRV_H_
#define _R128_DRV_H_

				/* r128_drv.c */
extern int  r128_init(void);
extern void r128_cleanup(void);
extern int  r128_version(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg);
extern int  r128_open(struct inode *inode, struct file *filp);
extern int  r128_release(struct inode *inode, struct file *filp);
extern int  r128_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  r128_lock(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);
extern int  r128_unlock(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg);

				/* r128_context.c */

extern int  r128_resctx(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  r128_addctx(struct inode *inode, struct file *filp,
		        unsigned int cmd, unsigned long arg);
extern int  r128_modctx(struct inode *inode, struct file *filp,
		        unsigned int cmd, unsigned long arg);
extern int  r128_getctx(struct inode *inode, struct file *filp,
		        unsigned int cmd, unsigned long arg);
extern int  r128_switchctx(struct inode *inode, struct file *filp,
			   unsigned int cmd, unsigned long arg);
extern int  r128_newctx(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg);
extern int  r128_rmctx(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg);

extern int  r128_context_switch(drm_device_t *dev, int old, int new);
extern int  r128_context_switch_complete(drm_device_t *dev, int new);
#endif
