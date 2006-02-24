
#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

static drm_ttm_backend_t *i915_create_ttm_backend_entry(drm_device_t * dev,
							int cached)
{
	if (cached) {
		return drm_agp_init_ttm_cached(dev);
	} else {
		return drm_agp_init_ttm_uncached(dev);
	}
}

static drm_ttm_mm_t *i915_ttm_mm(drm_device_t * dev)
{
	drm_i915_private_t *dev_priv = (drm_i915_private_t *) dev->dev_private;

	return &dev_priv->ttm_mm;
}

void i915_init_ttm(drm_device_t * dev, drm_i915_private_t * dev_priv)
{
	drm_ttm_driver_t *ttm_driver = &dev_priv->ttm_driver;
	drm_ttm_mm_init(dev, &dev_priv->ttm_mm, 32768, 20480);
	ttm_driver->emit_fence = i915_emit_fence;
	ttm_driver->wait_fence = i915_wait_fence;
	ttm_driver->test_fence = i915_test_fence;
	ttm_driver->create_ttm_backend_entry = i915_create_ttm_backend_entry;
	ttm_driver->ttm_mm = i915_ttm_mm;
	ttm_driver->flush_caches = i915_emit_mi_flush;
	ttm_driver->cached_pages = TRUE;
	dev->driver->ttm_driver = ttm_driver;
}
