/* i915_irq.c -- IRQ support for the I915 -*- linux-c -*-
 */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#define USER_INT_FLAG (1<<1)
#define VSYNC_PIPEB_FLAG (1<<5)
#define VSYNC_PIPEA_FLAG (1<<7)

#define MAX_NOPID ((u32)~0)

irqreturn_t i915_driver_irq_handler(DRM_IRQ_ARGS)
{
	drm_device_t *dev = (drm_device_t *) arg;
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	u16 temp;

	temp = I915_READ16(I915REG_INT_IDENTITY_R);

	temp &= (USER_INT_FLAG | VSYNC_PIPEA_FLAG);

	DRM_DEBUG("%s flag=%08x\n", __FUNCTION__, temp);

	if (temp == 0)
		return IRQ_NONE;

	I915_WRITE16(I915REG_INT_IDENTITY_R, temp);

	if (temp & USER_INT_FLAG)
		DRM_WAKEUP(&dev_priv->irq_queue);

	if (temp & VSYNC_PIPEA_FLAG) {
		atomic_inc(&dev->vbl_received);
		DRM_WAKEUP(&dev->vbl_queue);
		drm_vbl_send_signals(dev);
	}

	return IRQ_HANDLED;
}

static int i915_emit_irq(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	u32 ret;
	RING_LOCALS;

	i915_kernel_lost_context(dev);

	DRM_DEBUG("%s\n", __FUNCTION__);

	ret = dev_priv->counter;

	BEGIN_LP_RING(2);
	OUT_RING(0);
	OUT_RING(GFX_OP_USER_INTERRUPT);
	ADVANCE_LP_RING();

	return ret;
}



static void i915_user_irq_on(drm_i915_private_t *dev_priv)
{

	spin_lock(&dev_priv->user_irq_lock);
	if (++dev_priv->user_irq_refcount > 0){
		dev_priv->irq_enable_reg |= USER_INT_FLAG;
		I915_WRITE16(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);
	}
	spin_unlock(&dev_priv->user_irq_lock);

}
		
static void i915_user_irq_off(drm_i915_private_t *dev_priv)
{
	spin_lock(&dev_priv->user_irq_lock);
	if (--dev_priv->user_irq_refcount == 0) {
		dev_priv->irq_enable_reg &= ~USER_INT_FLAG;
		I915_WRITE16(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);
	}
	spin_unlock(&dev_priv->user_irq_lock);
}
		

static int i915_wait_irq(drm_device_t * dev, int irq_nr)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret = 0;

	DRM_DEBUG("%s irq_nr=%d breadcrumb=%d\n", __FUNCTION__, irq_nr,
		  READ_BREADCRUMB(dev_priv));

	dev_priv->sarea_priv->last_dispatch = READ_BREADCRUMB(dev_priv);
	if (((uint32_t)(READ_BREADCRUMB(dev_priv) - irq_nr)) <= (1 << 23))
		return 0;

	dev_priv->sarea_priv->perf_boxes |= I915_BOX_WAIT;

	i915_user_irq_on(dev_priv);
	DRM_WAIT_ON(ret, dev_priv->irq_queue, 3 * DRM_HZ,
		    (((uint32_t)(READ_BREADCRUMB(dev_priv) - irq_nr)) <= (1 << 23)));
	i915_user_irq_off(dev_priv);

	if (ret == DRM_ERR(EBUSY)) {
		DRM_ERROR("%s: EBUSY -- rec: %d emitted: %d\n",
			  __FUNCTION__,
			  READ_BREADCRUMB(dev_priv), (int)dev_priv->counter);
	}

	return ret;
}

int i915_driver_vblank_wait(drm_device_t *dev, unsigned int *sequence)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	unsigned int cur_vblank;
	int ret = 0;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_WAIT_ON(ret, dev->vbl_queue, 3 * DRM_HZ,
		    (((cur_vblank = atomic_read(&dev->vbl_received))
			- *sequence) <= (1<<23)));
	
	*sequence = cur_vblank;

	return ret;
}

/* Needs the lock as it touches the ring.
 */
int i915_irq_emit(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_emit_t emit;
	int result;

	LOCK_TEST_WITH_RETURN(dev, filp);

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(emit, (drm_i915_irq_emit_t __user *) data,
				 sizeof(emit));

	result = i915_emit_irq(dev);

	if (DRM_COPY_TO_USER(emit.irq_seq, &result, sizeof(int))) {
		DRM_ERROR("copy_to_user\n");
		return DRM_ERR(EFAULT);
	}

	return 0;
}


/* Doesn't need the hardware lock.
 */
int i915_irq_wait(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	drm_i915_private_t *dev_priv = dev->dev_private;
	drm_i915_irq_wait_t irqwait;

	if (!dev_priv) {
		DRM_ERROR("%s called with no initialization\n", __FUNCTION__);
		return DRM_ERR(EINVAL);
	}

	DRM_COPY_FROM_USER_IOCTL(irqwait, (drm_i915_irq_wait_t __user *) data,
				 sizeof(irqwait));

	return i915_wait_irq(dev, irqwait.irq_seq);
}

/* drm_dma.h hooks
*/
void i915_driver_irq_preinstall(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	I915_WRITE16(I915REG_HWSTAM, 0xeffe);
	I915_WRITE16(I915REG_INT_MASK_R, 0x0);
	I915_WRITE16(I915REG_INT_ENABLE_R, 0x0);
}

void i915_driver_irq_postinstall(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	dev_priv->irq_enable_reg = VSYNC_PIPEA_FLAG;
	dev_priv->user_irq_lock = SPIN_LOCK_UNLOCKED;
	dev_priv->user_irq_refcount = 0;
	I915_WRITE16(I915REG_INT_ENABLE_R, dev_priv->irq_enable_reg);
	DRM_INIT_WAITQUEUE(&dev_priv->irq_queue);
}

void i915_driver_irq_uninstall(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	if (!dev_priv)
		return;

	I915_WRITE16(I915REG_HWSTAM, 0xffff);
	I915_WRITE16(I915REG_INT_MASK_R, 0xffff);
	I915_WRITE16(I915REG_INT_ENABLE_R, 0x0);
}

uint32_t i915_emit_fence(drm_device_t * dev, uint32_t type) 
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	if (!dev_priv)
		return 0;

	i915_emit_irq(dev);
	return 	dev_priv->counter;
}

static int i915_do_test_fence(drm_device_t * dev, uint32_t fence)
{

	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	uint32_t test;

	if (!dev_priv)
		return TRUE;

	test = (((uint32_t)(READ_BREADCRUMB(dev_priv))) - fence);
	return (test < (1 << 23));
}

static int i915_sync_flush(drm_device_t *dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	uint32_t saved_status, i_status;
	int i;
	
	saved_status = READ_HWSP(dev_priv, 0);
	I915_WRITE(I915REG_INSTPM, (1 << 5) | (1 << 21));
	for (i=0; i<10000000; ++i) {
		i_status = READ_HWSP(dev_priv, 0);
		if ((i_status & ( 1 << 12)) != (saved_status & (1 << 12)))
			return 0;
	}	
	DRM_ERROR("Sync Flush timeout: HWSP: 0x%x, 0x%x %d\n", 
			saved_status, i_status, i);
	return 1;
}


int i915_test_fence(drm_device_t *dev, uint32_t type, uint32_t fence)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int tmp = i915_do_test_fence(dev, fence);
	if (!dev_priv) {
		DRM_ERROR("called without initialization\n");
		return TRUE;
	}
	
	fence = READ_BREADCRUMB(dev_priv);
	dev->mm_driver->mm_sarea->retired[0] = fence;

	return tmp;
}

int i915_fence_aged(drm_device_t *dev, uint32_t type, uint32_t fence)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	uint32_t tmp;

	if (!dev_priv) {
		DRM_ERROR("called without initialization\n");
		return TRUE;
	}
	
	tmp = READ_BREADCRUMB(dev_priv);
	return ((tmp - fence) > DRM_MM_CLEAN);
}


int i915_wait_fence(drm_device_t * dev, uint32_t type, uint32_t fence) 
{

	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;
	int ret;

	if (!dev_priv)
		return 0;

	ret = 0;
	if ( !i915_do_test_fence(dev, fence)) {
		ret = 1;
		do {
			ret = i915_wait_irq(dev, fence);
			if (i915_do_test_fence(dev, fence)) {
				ret = 0;
				break;
			}
		} while (ret == 0);
	}
	
	if (ret && ret != DRM_ERR(EINTR)) {
		DRM_ERROR("Fence timeout %d %d\n", 
			READ_BREADCRUMB(dev_priv), fence);
	} 

	dev->mm_driver->mm_sarea->retired[0] = READ_BREADCRUMB(dev_priv);

	if (ret == DRM_ERR(EINTR))
		return DRM_ERR(EAGAIN);
	else
		return ret;	
}
