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

static void i915_mm_takedown(drm_mm_driver_t *mm_driver)
{
	drm_free(mm_driver, sizeof(*mm_driver), DRM_MEM_MM);
}


drm_mm_driver_t *i915_mm_init(drm_device_t * dev)
{
	drm_mm_driver_t *mm_driver = 
		drm_calloc(1, sizeof(*mm_driver), DRM_MEM_MM);
	if (!dev->dev_private)
	  return NULL;
	mm_driver->fence_types = 1;
	mm_driver->kernel_emit = TRUE;
	mm_driver->emit_fence = i915_emit_fence;
	mm_driver->wait_fence = i915_wait_fence;
	mm_driver->test_fence = i915_test_fence;
	mm_driver->create_ttm_backend_entry = i915_create_ttm_backend_entry;
	mm_driver->flush_caches = i915_emit_mi_flush;
	mm_driver->cached_pages = TRUE;
	mm_driver->takedown = i915_mm_takedown;
	return mm_driver;
}

