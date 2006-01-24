#ifndef _DRM_TTM_H
#define _DRM_TTM_H

/*
 * The backend GART interface. (In our case AGP). Any similar type of device (PCIE?)
 * needs only to implement these functions to be usable with the "TTM" interface.
 * The AGP backend implementation lives in drm_agpsupport.c 
 * basically maps these calls to available functions in agpgart. Each drm device driver gets an
 * additional function pointer that creates these types, 
 * so that the device can choose the correct aperture.
 * (Multiple AGP apertures, etc.) 
 * Most device drivers will let this point to the standard AGP implementation.
 */

typedef struct drm_ttm_backend {
	void *private;
	int (*needs_cache_adjust) (struct drm_ttm_backend * backend);
	int (*populate) (struct drm_ttm_backend * backend,
			 unsigned long num_pages, struct page ** pages);
	void (*clear) (struct drm_ttm_backend * backend);
	int (*bind) (struct drm_ttm_backend * backend, unsigned long offset);
	int (*unbind) (struct drm_ttm_backend * backend);
	void (*destroy) (struct drm_ttm_backend * backend);
} drm_ttm_backend_t;

struct drm_ttm;

typedef struct drm_ttm_backend_list {
	struct list_head head;
	drm_ttm_backend_t *be;
	unsigned page_offset;
	unsigned num_pages;
	int bound;
	struct drm_ttm *owner;
} drm_ttm_backend_list_t;

typedef struct drm_ttm_vma_list {
	struct list_head head;
	pgprot_t orig_protection;
	struct vm_area_struct *vma;
	drm_map_t *map;
} drm_ttm_vma_list_t;

typedef struct drm_ttm {
	struct page **pages;
	int8_t *nocached;
	unsigned long lhandle;
	unsigned long num_pages;
	drm_ttm_vma_list_t *vma_list;
	struct drm_device *dev;
	drm_ttm_backend_list_t *be_list;
	atomic_t vma_count;
	drm_file_t *owner;
        int in_dtor;
} drm_ttm_t;

/*
 * Initialize a ttm. Currently the size is fixed. Currently drmAddMap calls this function
 * and creates a DRM map of type _DRM_TTM, and returns a reference to that map to the caller.
 * However, since drmAddMap is root only, It would be better to create a separate IOCTL for the
 * TTM user interface.
 */

drm_ttm_t *drm_init_ttm(struct drm_device *dev, unsigned long size);

/*
 * Bind a part of the ttm starting at page_offset size n_pages into the GTT, at
 * aperture offset aper_offset. The region handle will be used to reference this
 * bound region in the future. Note that the region may be the whole ttm. Regions should not overlap.
 * This function sets all affected pages as noncacheable and flushes cashes and TLB.
 */

int drm_bind_ttm_region(drm_ttm_t * ttm, unsigned long page_offset,
			unsigned long n_pages,
			unsigned long aper_offset,
			drm_ttm_backend_list_t ** region);

/*
 * Unbind a ttm region. Restores caching policy. Flushes caches and TLB.
 */

void drm_unbind_ttm_region(drm_ttm_backend_list_t * entry);

/*
 * Evict a ttm region. Keeps Aperture caching policy.
 */

int drm_evict_ttm_region(drm_ttm_backend_list_t * entry);

/*
 * Rebind an already evicted region into a possibly new location in the aperture.
 */

int drm_rebind_ttm_region(drm_ttm_backend_list_t * entry,
			  unsigned long aper_offset);

/*
 * Destroy a ttm. The user normally calls drmRmMap or a similar IOCTL to do this, 
 * which calls this function iff there are no vmas referencing it anymore. Otherwise it is called
 * when the last vma exits.
 */

void drm_destroy_ttm(drm_ttm_t * ttm);



int drm_ttm_ioctl(DRM_IOCTL_ARGS);

#endif
