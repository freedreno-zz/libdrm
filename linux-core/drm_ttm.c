/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Steamboat Springs, CO.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE 
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * 
 **************************************************************************/

#include "drmP.h"
#include <linux/rmap.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

/*
 * DAVE: The below code needs to go to the linux mm subsystem. Most of it is already there.
 * Basically stolen from mprotect.c and rmap.c 
 * 8<----------------------------------------------------------------------------------
 */

#ifdef CONFIG_X86_PAE
#error Cannot compile with CONFIG_X86_PAE. __supported_pte_mask undefined.
#endif

void pgd_clear_bad(pgd_t * pgd)
{
	pgd_ERROR(*pgd);
	pgd_clear(pgd);
}

void pud_clear_bad(pud_t * pud)
{
	pud_ERROR(*pud);
	pud_clear(pud);
}

void pmd_clear_bad(pmd_t * pmd)
{
	pmd_ERROR(*pmd);
	pmd_clear(pmd);
}

/*
 * Invalidate or update all PTEs associated with a vma.
 */

static void change_pte_range(struct mm_struct *mm, pmd_t * pmd,
			     unsigned long addr, unsigned long end,
			     pgprot_t newprot, int unmap)
{
	pte_t *pte;
	int count;
	struct page *page;

	pte = pte_offset_map(pmd, addr);
	do {
		if (unmap && pte_present(*pte)) {
			count = get_mm_counter(mm, rss);
			if (count) {
				page = pte_page(*pte);
				ptep_get_and_clear(mm, addr, pte);
				dec_mm_counter(mm, rss);
				atomic_add_negative(-1, &page->_mapcount);
				put_page(page);
				lazy_mmu_prot_update(*pte);
			}
		}
		if (pte_present(*pte)) {
			pte_t ptent;
			ptent =
			    pte_modify(ptep_get_and_clear(mm, addr, pte),
				       newprot);
			set_pte_at(mm, addr, pte, ptent);
			lazy_mmu_prot_update(ptent);
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap(pte - 1);
}

static inline void change_pmd_range(struct mm_struct *mm, pud_t * pud,
				    unsigned long addr, unsigned long end,
				    pgprot_t newprot, int unmap)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		change_pte_range(mm, pmd, addr, next, newprot, unmap);
	} while (pmd++, addr = next, addr != end);
}

static inline void change_pud_range(struct mm_struct *mm, pgd_t * pgd,
				    unsigned long addr, unsigned long end,
				    pgprot_t newprot, int unmap)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		change_pmd_range(mm, pud, addr, next, newprot, unmap);
	} while (pud++, addr = next, addr != end);
}

static void drm_change_protection(struct vm_area_struct *vma,
				  unsigned long addr, unsigned long end,
				  pgprot_t newprot, int unmap)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		change_pud_range(mm, pgd, addr, next, newprot, unmap);
	} while (pgd++, addr = next, addr != end);
	global_flush_tlb();
}

/*
 * 8<----------------------------------------------------------------------------------
 * End linux mm subsystem code.
 */

/*
 * Unmap all vma pages from vmas mapping this ttm.
 */

static int unmap_vma_pages(drm_ttm_t * ttm, unsigned long page_offset,
			   unsigned long num_pages)
{
	struct list_head *list;
	struct page **first_page = ttm->pages + page_offset;
	struct page **last_page = ttm->pages + (page_offset + num_pages);
	struct page **cur_page;

	list_for_each(list, &ttm->vma_list->head) {
		drm_ttm_vma_list_t *entry =
		    list_entry(list, drm_ttm_vma_list_t, head);
		drm_change_protection(entry->vma,
				      entry->vma->vm_start +
				      (page_offset << PAGE_SHIFT),
				      entry->vma->vm_start +
				      ((page_offset + num_pages) << PAGE_SHIFT),
				      entry->vma->vm_page_prot, TRUE);

	}

	for (cur_page = first_page; cur_page != last_page; ++cur_page) {
		if (page_mapcount(*cur_page) != 0) {
			DRM_ERROR("Mapped page detected. Map count is %d\n",
				  page_mapcount(*cur_page));
			return -1;
		}
	}
	return 0;
}

/*
 * Free all resources associated with a ttm.
 */

int drm_destroy_ttm(drm_ttm_t * ttm)
{

	int i;
	struct list_head *list, *next;
	struct page **cur_page;
	unsigned hash;

	if (!ttm)
		return 0;

	if (atomic_read(&ttm->vma_count) > 0) {
		DRM_DEBUG("VMAs are still alive. Skipping destruction.\n");
		return -1;
	} else {
		DRM_DEBUG("About to really destroy ttm.\n");
	}

	if (ttm->be_list) {
		list_for_each_safe(list, next, &ttm->be_list->head) {
			drm_ttm_backend_list_t *entry =
			    list_entry(list, drm_ttm_backend_list_t, head);
			if (!drm_find_ht_item(&ttm->dev->ttmreghash,
					      entry, &hash))
				drm_remove_ht_val(&ttm->dev->ttmreghash, hash);
			drm_destroy_ttm_region(entry);
		}

		drm_free(ttm->be_list, sizeof(*ttm->be_list), DRM_MEM_MAPS);
	}

	if (ttm->pages) {
		for (i = 0; i < ttm->num_pages; ++i) {
			cur_page = ttm->pages + i;
			if (ttm->page_flags &&
			    (ttm->page_flags[i] & DRM_TTM_PAGE_UNCACHED) &&
			    *cur_page && !PageHighMem(*cur_page)) {
				change_page_attr(*cur_page, 1, PAGE_KERNEL);
			}
			if (*cur_page) {
				unlock_page(*cur_page);
				__free_page(*cur_page);
			}
		}
		global_flush_tlb();
		vfree(ttm->pages);
	}
	if (ttm->page_flags) {
		vfree(ttm->page_flags);
	}

	if (ttm->vma_list) {
		list_for_each_safe(list, next, &ttm->vma_list->head) {
			drm_ttm_vma_list_t *entry =
			    list_entry(list, drm_ttm_vma_list_t, head);
			list_del(list);
			entry->vma->vm_private_data = NULL;
			drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
		}
		drm_free(ttm->vma_list, sizeof(*ttm->vma_list), DRM_MEM_MAPS);
	}
	drm_free(ttm, sizeof(*ttm), DRM_MEM_MAPS);

	return 0;
}

/*
 * Initialize a ttm.
 * FIXME: Avoid using vmalloc for the page- and page_flags tables?
 */

drm_ttm_t *drm_init_ttm(struct drm_device * dev, unsigned long size)
{

	drm_ttm_t *ttm;

	if (!dev->mm_driver)
		return NULL;

	ttm = drm_calloc(1, sizeof(*ttm), DRM_MEM_MAPS);
	if (!ttm)
		return NULL;

	ttm->lhandle = 0;
	atomic_set(&ttm->vma_count, 0);
	ttm->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	ttm->page_flags = vmalloc(ttm->num_pages * sizeof(*ttm->page_flags));
	if (!ttm->page_flags) {
		drm_destroy_ttm(ttm);
		DRM_ERROR("Failed allocating page_flags table\n");
		return NULL;
	}
	memset(ttm->page_flags, 0, ttm->num_pages * sizeof(*ttm->page_flags));

	ttm->pages = vmalloc(ttm->num_pages * sizeof(*ttm->pages));
	if (!ttm->pages) {
		drm_destroy_ttm(ttm);
		DRM_ERROR("Failed allocating page table\n");
		return NULL;
	}
	memset(ttm->pages, 0, ttm->num_pages * sizeof(*ttm->pages));

	ttm->be_list = drm_calloc(1, sizeof(*ttm->be_list), DRM_MEM_MAPS);
	if (!ttm->be_list) {
		DRM_ERROR("Alloc be regions failed\n");
		drm_destroy_ttm(ttm);
		return NULL;
	}

	INIT_LIST_HEAD(&ttm->be_list->head);

	ttm->vma_list = drm_calloc(1, sizeof(*ttm->vma_list), DRM_MEM_MAPS);
	if (!ttm->vma_list) {
		DRM_ERROR("Alloc vma list failed\n");
		drm_destroy_ttm(ttm);
		return NULL;
	}

	INIT_LIST_HEAD(&ttm->vma_list->head);

	ttm->lhandle = (unsigned long)ttm;
	ttm->dev = dev;
	return ttm;
}

/*
 * Change caching policy for range of pages in a ttm.
 */

static int drm_set_caching(drm_ttm_t * ttm, unsigned long page_offset,
			   unsigned long num_pages, int noncached)
{
	int i, cur;
	struct page **cur_page;
	pgprot_t attr = (noncached) ? PAGE_KERNEL_NOCACHE : PAGE_KERNEL;
	int do_spinlock = atomic_read(&ttm->vma_count) > 0;

	if (do_spinlock) {
		down_write(&current->mm->mmap_sem);
		spin_lock(&current->mm->page_table_lock);
		unmap_vma_pages(ttm, page_offset, num_pages);
	}
	for (i = 0; i < num_pages; ++i) {
		cur = page_offset + i;
		cur_page = ttm->pages + cur;
		if (*cur_page) {
			if (PageHighMem(*cur_page)) {
				if (noncached
				    && page_address(*cur_page) != NULL) {
					DRM_ERROR
					    ("Illegal mapped HighMem Page\n");
					up_write(&current->mm->mmap_sem);
					if (do_spinlock) {
						spin_unlock(&current->mm->
							    page_table_lock);
						up_write(&current->mm->
							 mmap_sem);
					}
					return -EINVAL;
				}
			} else if ((ttm->page_flags[cur] &
				    DRM_TTM_PAGE_UNCACHED) != noncached) {
				DRM_MASK_VAL(ttm->page_flags[cur],
					     DRM_TTM_PAGE_UNCACHED, noncached);
				change_page_attr(*cur_page, 1, attr);
			}
		}
	}
	global_flush_tlb();
	if (do_spinlock) {
		spin_unlock(&current->mm->page_table_lock);
		up_write(&current->mm->mmap_sem);
	}
	return 0;
}

/*
 * Take a ttm region out of the aperture manager.
 */

static void remove_ttm_region(drm_ttm_backend_list_t * entry)
{

	drm_mm_node_t *mm_node = entry->mm_node;
	drm_ttm_mm_priv_t *mm_priv;
	drm_ttm_mm_t *mm = entry->mm;
	drm_device_t *dev = mm->dev;

	if (!mm_node)
		return;

	entry->mm_node = NULL;
	mm_priv = (drm_ttm_mm_priv_t *) mm_node->private;
	if (!mm_priv)
		return;

	if (mm_priv->fence_valid)
		dev->mm_driver->wait_fence(mm->dev, entry->fence_type,
					   mm_priv->fence);
	mm_node->private = NULL;
	spin_lock(&mm->mm.mm_lock);
	list_del(&mm_priv->lru);
	drm_mm_put_block_locked(&mm->mm, mm_node);
	spin_unlock(&mm->mm.mm_lock);
	drm_free(mm_priv, sizeof(*mm_priv), DRM_MEM_MM);
}

/*
 * Unbind a ttm region from the aperture and take it out of the
 * aperture manager.
 */

int drm_evict_ttm_region(drm_ttm_backend_list_t * entry)
{
	drm_ttm_backend_t *be = entry->be;
	drm_ttm_t *ttm = entry->owner;

	if (be) {
		switch (entry->state) {
		case ttm_bound:
			if (ttm && be->needs_cache_adjust(be)) {
				unmap_vma_pages(ttm, entry->page_offset,
						entry->num_pages);
			}
			be->unbind(entry->be);
			if (ttm && be->needs_cache_adjust(be)) {
				drm_set_caching(ttm, entry->page_offset,
						entry->num_pages, 0);
			}
			break;
		default:
			break;
		}
	}
	entry->state = ttm_evicted;
	return 0;
}

void drm_unbind_ttm_region(drm_ttm_backend_list_t * entry)
{
	remove_ttm_region(entry);
	drm_evict_ttm_region(entry);
	entry->state = ttm_unbound;
}

/*
 * Destroy and clean up all resources associated with a ttm region.
 * FIXME: release pages to OS when doing this operation.
 */

void drm_destroy_ttm_region(drm_ttm_backend_list_t * entry)
{
	drm_ttm_backend_t *be = entry->be;
	drm_ttm_t *ttm = entry->owner;
	uint32_t *cur_page_flags;
	int i;

	list_del(&entry->head);
	remove_ttm_region(entry);
	drm_unbind_ttm_region(entry);
	if (be) {
		be->clear(entry->be);
		if (be->needs_cache_adjust(be)) {
			drm_set_caching(ttm, entry->page_offset,
					entry->num_pages, 0);
		}
		be->destroy(be);
	}

	cur_page_flags = ttm->page_flags;
	for (i = 0; i < entry->num_pages; ++i) {
		DRM_MASK_VAL(*cur_page_flags, DRM_TTM_PAGE_USED, 0);
		cur_page_flags++;
	}

	drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
}

/*
 * Create a ttm region from a range of ttm pages.
 */

int drm_create_ttm_region(drm_ttm_t * ttm, unsigned long page_offset,
			  unsigned long n_pages, int cached,
			  drm_ttm_backend_list_t ** region)
{
	struct page **cur_page;
	uint32_t *cur_page_flags;
	drm_ttm_backend_list_t *entry;
	drm_ttm_backend_t *be;
	int ret, i;

	if ((page_offset + n_pages) > ttm->num_pages || n_pages == 0) {
		DRM_ERROR("Region Doesn't fit ttm\n");
		return -EINVAL;
	}

	cur_page_flags = ttm->page_flags;
	for (i = 0; i < n_pages; ++i) {
		if (*cur_page_flags++ & DRM_TTM_PAGE_USED) {
			DRM_ERROR("TTM region overlap\n");
			return -EINVAL;
		}
	}

	cur_page_flags = ttm->page_flags;
	for (i = 0; i < n_pages; ++i) {
		DRM_MASK_VAL(*cur_page_flags, DRM_TTM_PAGE_USED,
			     DRM_TTM_PAGE_USED);
		cur_page_flags++;
	}

	entry = drm_calloc(1, sizeof(*entry), DRM_MEM_MAPS);
	if (!entry)
		return -ENOMEM;

	be = ttm->dev->mm_driver->create_ttm_backend_entry(ttm->dev, cached);
	if (!be) {
		drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
		DRM_ERROR("Couldn't create backend.\n");
		return -EINVAL;
	}
	entry->state = ttm_unbound;
	entry->page_offset = page_offset;
	entry->num_pages = n_pages;
	entry->be = be;
	entry->owner = ttm;

	INIT_LIST_HEAD(&entry->head);
	list_add_tail(&entry->head, &ttm->be_list->head);

	for (i = 0; i < entry->num_pages; ++i) {
		cur_page = ttm->pages + (page_offset + i);
		if (!*cur_page) {
			*cur_page = alloc_page(GFP_USER);
			if (!*cur_page) {
				DRM_ERROR("Page allocation failed\n");
				drm_destroy_ttm_region(entry);
				return -ENOMEM;
			}
			SetPageLocked(*cur_page);
		}
	}

	if ((ret = be->populate(be, n_pages, ttm->pages + page_offset))) {
		drm_destroy_ttm_region(entry);
		DRM_ERROR("Couldn't populate backend.\n");
		return ret;
	}
	entry->mm_node = NULL;
	entry->mm = &ttm->dev->mm_driver->ttm_mm;
	ttm->aperture_base = be->aperture_base;
	*region = entry;
	return 0;
}

/*
 * Bind a ttm region. Set correct caching policy.
 */

int drm_bind_ttm_region(drm_ttm_backend_list_t * region,
			unsigned long aper_offset)
{

	int i;
	uint32_t *cur_page_flag;
	int ret;
	drm_ttm_backend_t *be;
	drm_ttm_t *ttm;

	if (!region || region->state == ttm_bound)
		return -EINVAL;

	be = region->be;
	ttm = region->owner;

	if (ttm && be->needs_cache_adjust(be)) {
		drm_set_caching(ttm, region->page_offset, region->num_pages,
				DRM_TTM_PAGE_UNCACHED);
	}

	if ((ret = be->bind(be, aper_offset))) {
		drm_unbind_ttm_region(region);
		DRM_ERROR("Couldn't bind backend.\n");
		return ret;
	}

	cur_page_flag = ttm->page_flags;
	for (i = 0; i < region->num_pages; ++i) {
		DRM_MASK_VAL(*cur_page_flag, DRM_TTM_MASK_PFN,
			     (i + aper_offset) << PAGE_SHIFT);
		cur_page_flag++;
	}

	region->state = ttm_bound;
	return 0;
}

int drm_rebind_ttm_region(drm_ttm_backend_list_t * entry,
			  unsigned long aper_offset)
{
	return drm_bind_ttm_region(entry, aper_offset);
}

/*
 * Destroy an anonymous ttm region.
 */

void drm_user_destroy_region(drm_ttm_backend_list_t * entry)
{
	drm_ttm_backend_t *be;
	struct page **cur_page;
	int i;

	if (!entry || entry->owner)
		return;

	remove_ttm_region(entry);

	be = entry->be;
	if (!be) {
		drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
		return;
	}

	be->unbind(be);

	if (entry->anon_pages) {
		cur_page = entry->anon_pages;
		for (i = 0; i < entry->anon_locked; ++i) {
			if (!PageReserved(*cur_page))
				SetPageDirty(*cur_page);
			page_cache_release(*cur_page);
			cur_page++;
		}
		vfree(entry->anon_pages);
	}

	be->destroy(be);
	drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
}

/*
 * Create a ttm region from an arbitrary region of user pages.
 * Since this region has no backing ttm, it's owner is set to
 * null, and it is registered with the file of the caller.
 * Gets destroyed when the file is closed. We call this an
 * anonymous ttm region.
 */

int drm_user_create_region(drm_device_t * dev, unsigned long start, int len,
			   drm_ttm_backend_list_t ** entry)
{
	drm_ttm_backend_list_t *tmp;
	drm_ttm_backend_t *be;
	int ret;

	if (len <= 0)
		return -EINVAL;
	if (!dev->mm_driver->create_ttm_backend_entry)
		return -EFAULT;

	tmp = drm_calloc(1, sizeof(*tmp), DRM_MEM_MAPS);

	if (!tmp)
		return -ENOMEM;

	be = dev->mm_driver->create_ttm_backend_entry(dev, 1);
	tmp->be = be;

	if (!be) {
		drm_user_destroy_region(tmp);
		return -ENOMEM;
	}
	if (be->needs_cache_adjust(be)) {
		drm_user_destroy_region(tmp);
		return -EFAULT;
	}

	tmp->anon_pages = vmalloc(sizeof(*(tmp->anon_pages)) * len);

	if (!tmp->anon_pages) {
		drm_user_destroy_region(tmp);
		return -ENOMEM;
	}

	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(current, current->mm, start, len, 1, 0,
			     tmp->anon_pages, NULL);
	up_read(&current->mm->mmap_sem);

	if (ret != len) {
		drm_user_destroy_region(tmp);
		DRM_ERROR("Could not lock %d pages. Return code was %d\n",
			  len, ret);
		return -EPERM;
	}
	tmp->anon_locked = len;

	ret = be->populate(be, len, tmp->anon_pages);

	if (ret) {
		drm_user_destroy_region(tmp);
		return ret;
	}

	tmp->state = ttm_unbound;
	tmp->mm_node = NULL;
	tmp->mm = &dev->mm_driver->ttm_mm;
	*entry = tmp;

	return 0;
}

/*
 * Create a ttm and add it to the drm book-keeping.
 */

int drm_add_ttm(drm_device_t * dev, unsigned size, drm_map_list_t ** maplist)
{
	drm_map_list_t *list;
	drm_map_t *map;
	drm_ttm_t *ttm;

	map = drm_alloc(sizeof(*map), DRM_MEM_MAPS);
	if (!map)
		return -ENOMEM;

	ttm = drm_init_ttm(dev, size);

	if (!ttm) {
		DRM_ERROR("Could not create ttm\n");
		drm_free(map, sizeof(*map), DRM_MEM_MAPS);
		return -ENOMEM;
	}

	map->offset = ttm->lhandle;
	map->type = _DRM_TTM;
	map->flags = _DRM_REMOVABLE;
	map->size = size;

	list = drm_calloc(1, sizeof(*list), DRM_MEM_MAPS);
	if (!list) {
		drm_destroy_ttm(ttm);
		drm_free(map, sizeof(*map), DRM_MEM_MAPS);
		return -ENOMEM;
	}
	map->handle = (void *)list;

	if (drm_insert_ht_val(&dev->maphash, map->handle, &list->user_token)) {
		drm_destroy_ttm(ttm);
		drm_free(map, sizeof(*map), DRM_MEM_MAPS);
		drm_free(list, sizeof(*list), DRM_MEM_MAPS);
		up(&dev->struct_sem);
		return -ENOMEM;
	}

	list->user_token =
	    (list->user_token << PAGE_SHIFT) + DRM_MAP_HASH_OFFSET;
	list->map = map;

	*maplist = list;

	return 0;
}

/*
 * Fence all unfenced regions in the global lru list. 
 */

static void drm_ttm_fence_regions(drm_device_t * dev, drm_ttm_mm_t * mm)
{
	int emitted[DRM_FENCE_TYPES];
	uint32_t fence_seqs[DRM_FENCE_TYPES];
	struct list_head *list;
	uint32_t fence_type;
	uint32_t fence;

	memset(emitted, 0, sizeof(int) * DRM_FENCE_TYPES);
	spin_lock(&mm->mm.mm_lock);

	list_for_each_prev(list, &mm->lru_head) {
		drm_ttm_mm_priv_t *entry =
		    list_entry(list, drm_ttm_mm_priv_t, lru);
		if (entry->fence_valid)
			break;

		fence_type = entry->region->fence_type;

		if (!emitted[fence_type]) {
			fence = dev->mm_driver->emit_fence(dev, fence_type);
			fence_seqs[fence_type] = fence;
			emitted[fence_type] = TRUE;
		} else {
			fence = fence_seqs[fence_type];
		}

		entry->fence = fence;
		entry->fence_valid = TRUE;
	}

	spin_unlock(&mm->mm.mm_lock);
}

/*
 * Evict the first (oldest) region on the lru list, after its fence
 * is fulfilled. Will fail if the lru list is empty (nothing to evict),
 * or the first node doesn't have a fence which means it is a newly
 * validated region which the user intends not to be evicted yet.
 * May sleep while waiting for a fence.
 */

static int drm_ttm_evict_lru_sl(drm_ttm_backend_list_t * entry)
{
	struct list_head *list;
	drm_ttm_mm_t *mm = entry->mm;
	spinlock_t *mm_lock = &mm->mm.mm_lock;
	drm_ttm_mm_priv_t *evict_priv;
	uint32_t evict_fence;
	drm_device_t *dev = mm->dev;
	drm_mm_node_t *evict_node;

	do {
		list = entry->mm->lru_head.next;

		if (list == &entry->mm->lru_head) {
			DRM_ERROR("Out of aperture space\n");
			return -ENOMEM;
		}
		evict_priv = list_entry(list, drm_ttm_mm_priv_t, lru);

		if (!evict_priv->fence_valid) {
			DRM_ERROR("Out of aperture space\n");
			return -ENOMEM;
		}

		evict_fence = evict_priv->fence;

		spin_unlock(mm_lock);
		up(&dev->struct_sem);
		dev->mm_driver->wait_fence(dev, evict_priv->region->fence_type,
					   evict_fence);
		down(&dev->struct_sem);
		spin_lock(mm_lock);

	} while (TRUE);

	DRM_ERROR("Evicting 0x%lx\n", (unsigned long)evict_priv->region);
	dev->mm_driver->evicted_tt = TRUE;
	evict_node = evict_priv->region->mm_node;
	drm_evict_ttm_region(evict_priv->region);
	list_del_init(list);
	evict_node->private = NULL;
	drm_mm_put_block_locked(&mm->mm, evict_node);
	evict_priv->region->mm_node = NULL;
	drm_free(evict_priv, sizeof(*evict_priv), DRM_MEM_MM);

	return 0;
}

/*
 * Make sure a backend entry is present in the TT. If it is not, try to allocate
 * TT space and put it in there. If we're out of space, start evicting old entries
 * from the head of the global lru list, which is sorted in fence order.
 * Finally move the entry to the tail of the lru list. Pinned regions don't go into
 * the lru list.
 */

static int drm_validate_ttm_region(drm_ttm_backend_list_t * entry,
				   uint32_t fence_type, unsigned *aper_offset)
{
	drm_mm_node_t *mm_node = entry->mm_node;
	drm_ttm_mm_t *mm = entry->mm;
	spinlock_t *mm_lock = &mm->mm.mm_lock;
	drm_ttm_mm_priv_t *mm_priv;
	unsigned num_pages;
	int ret;

	if (!entry->mm_node) {
		mm_priv = drm_alloc(sizeof(*mm_priv), DRM_MEM_MM);
		if (!mm_priv)
			return -ENOMEM;
		INIT_LIST_HEAD(&mm_priv->lru);
	} else {
		mm_priv = mm_node->private;
	}

	num_pages = (entry->owner) ? entry->num_pages : entry->anon_locked;
	spin_lock(mm_lock);
	while (!mm_node) {
		mm_node =
		    drm_mm_search_free_locked(&entry->mm->mm, num_pages, 0, 0);
		if (!mm_node) {
			ret = drm_ttm_evict_lru_sl(entry);
			if (ret) {
				spin_unlock(mm_lock);
				return ret;
			}
		}
	}

	if (!entry->mm_node) {
		mm_node = drm_mm_get_block_locked(mm_node, num_pages, 0);
		mm_node->private = mm_priv;
		mm_priv->region = entry;
		entry->mm_node = mm_node;
	} else {
		list_del_init(&mm_priv->lru);
	}

	mm_priv->fence_valid = FALSE;
	entry->fence_type = fence_type;

	if (!entry->pinned)
		list_add_tail(&mm_priv->lru, &mm->lru_head);

	switch (entry->state) {
	case ttm_bound:
		break;
	case ttm_evicted:
		drm_rebind_ttm_region(entry, mm_node->start);
		break;
	case ttm_unbound:
	default:
		drm_bind_ttm_region(entry, mm_node->start);
		break;
	}

	spin_unlock(mm_lock);
	*aper_offset = mm_node->start;
	return 0;
}

static void drm_ttm_mm_init(drm_device_t * dev, drm_ttm_mm_t * mm,
			    unsigned long start, unsigned long size)
{
	drm_mm_init(&mm->mm, start, size);
	INIT_LIST_HEAD(&mm->lru_head);
	mm->dev = dev;
}

static void drm_ttm_mm_takedown(drm_ttm_mm_t * mm)
{
	drm_mm_takedown(&mm->mm);
}

static int drm_ttm_from_handle(drm_handle_t handle, drm_file_t * priv,
			       drm_ttm_t ** used_ttm,
			       drm_map_list_t ** used_list)
{
	drm_device_t *dev = priv->head->dev;
	drm_ttm_t *ttm;
	drm_map_list_t *map_list;
	drm_map_t *map = NULL;
	void *hash_val;

	if (drm_get_ht_val(&dev->maphash,
			   (handle -
			    DRM_MAP_HASH_OFFSET) >> PAGE_SHIFT, &hash_val)) {
		DRM_ERROR("Could not find TTM map.\n");
		return -EINVAL;
	}

	map_list = (drm_map_list_t *) hash_val;

	map = map_list->map;

	if (!map || map->type != _DRM_TTM) {
		DRM_ERROR("Could not find TTM map.\n");
		return -EINVAL;
	}
	ttm = (drm_ttm_t *) map->offset;
	if (ttm->owner != priv) {
		DRM_ERROR("Caller is not TTM owner.\n");
		return -EINVAL;
	}
	*used_ttm = ttm;
	if (used_list)
		*used_list = map_list;
	return 0;
}

static int drm_ttm_create_buffer(drm_device_t * dev, drm_ttm_t * ttm,
				 drm_ttm_buf_arg_t * buf,
				 drm_ttm_backend_list_t ** created)
{
	unsigned region;
	drm_ttm_backend_list_t *entry;
	int ret;
	int cached;

	cached = buf->flags & DRM_TTM_FLAG_CACHED;

	if ((ret = drm_create_ttm_region(ttm, buf->ttm_page_offset,
					 buf->num_pages, cached, &entry)))
		return ret;

	if ((ret = drm_insert_ht_val(&dev->ttmreghash, entry, &region))) {
		drm_destroy_ttm_region(entry);
		return ret;
	}
	buf->region_handle = (drm_handle_t) region;

	entry->pinned = buf->flags & DRM_TTM_FLAG_PINNED;
	*created = entry;
	return 0;
}

static int drm_ttm_region_from_handle(drm_handle_t handle, drm_file_t * priv,
				      drm_ttm_backend_list_t ** region)
{
	drm_device_t *dev = priv->head->dev;
	drm_ttm_backend_list_t *entry;
	void *hash_val;

	if (drm_get_ht_val(&dev->ttmreghash, handle, &hash_val)) {
		DRM_ERROR("Could not find TTM region.\n");
		return -EINVAL;
	}
	entry = (drm_ttm_backend_list_t *) hash_val;
	if (entry->owner && entry->owner->owner != priv) {
		DRM_ERROR("Caller is not TTM region owner.\n");
		return -EPERM;
	}
	if (!entry->owner && entry->anon_owner != priv) {
		DRM_ERROR("Caller is not TTM region owner.\n");
		return -EPERM;
	}
	*region = entry;
	return 0;
}

static int drm_ttm_create_user_buf(drm_ttm_buf_arg_t * buf_p,
				   char __user * addr, unsigned long size,
				   drm_file_t * priv,
				   drm_ttm_backend_list_t ** created)
{
	unsigned long start, end;
	int ret, len;
	unsigned region;
	drm_ttm_backend_list_t *entry;
	drm_device_t *dev = priv->head->dev;

	if (!(buf_p->flags & DRM_TTM_FLAG_NEW))
		return -EINVAL;

	end = (unsigned long)addr + size;
	start = ((unsigned long)addr) & ~(PAGE_SIZE - 1);
	end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	len = ((end - start) >> PAGE_SHIFT);
	if (len <= 0) {
		DRM_ERROR("Invalid user buffer length.\n");
		return -EINVAL;
	}
	ret = drm_user_create_region(dev, start, len, &entry);
	if (ret)
		return ret;

	entry->anon_owner = priv;
	ret = drm_insert_ht_val(&dev->ttmreghash, entry, &region);
	if (ret) {
		drm_user_destroy_region(entry);
		return ret;
	}
	list_add(&entry->head, &priv->anon_ttm_regs);
	buf_p->region_handle = (drm_handle_t) region;
	entry->pinned = buf_p->flags & DRM_TTM_FLAG_PINNED;
	*created = entry;
	return 0;
}

static void drm_ttm_handle_buf(drm_file_t * priv, drm_ttm_buf_arg_t * buf_p)
{
	drm_device_t *dev = priv->head->dev;
	drm_ttm_t *ttm;
	drm_ttm_backend_list_t *entry;
	drm_ttm_mm_t *ttm_mm;

	switch (buf_p->op) {
	case ttm_validate_user:
		buf_p->ret =
		    drm_ttm_create_user_buf(buf_p, buf_p->user_addr,
					    buf_p->user_size, priv, &entry);
		if (buf_p->ret)
			break;
		ttm_mm = entry->mm;
		if (buf_p->fence_type >= dev->mm_driver->fence_types) {
			buf_p->ret = -EINVAL;
			break;
		}
		buf_p->ret =
		    drm_validate_ttm_region(entry, buf_p->fence_type,
					    &buf_p->aper_offset);
		break;
	case ttm_validate:
		buf_p->ret =
		    drm_ttm_from_handle(buf_p->ttm_handle, priv, &ttm, NULL);
		if (buf_p->ret)
			break;
		if (buf_p->flags & DRM_TTM_FLAG_NEW) {
			buf_p->ret =
			    drm_ttm_create_buffer(dev, ttm, buf_p, &entry);
			if (buf_p->ret)
				break;
			buf_p->flags &= ~DRM_TTM_FLAG_NEW;
		} else {
			buf_p->ret =
			    drm_ttm_region_from_handle(buf_p->region_handle,
						       priv, &entry);
			if (buf_p->ret)
				break;
		}
		ttm_mm = entry->mm;
		if (buf_p->fence_type >= dev->mm_driver->fence_types) {
			buf_p->ret = -EINVAL;
			break;
		}
		buf_p->ret =
		    drm_validate_ttm_region(entry, buf_p->fence_type,
					    &buf_p->aper_offset);
		break;
	case ttm_unbind:
		buf_p->ret =
		    drm_ttm_region_from_handle(buf_p->region_handle, priv,
					       &entry);
		if (buf_p->ret)
			break;
		drm_unbind_ttm_region(entry);
		break;
	case ttm_evict:
		buf_p->ret =
		    drm_ttm_region_from_handle(buf_p->region_handle, priv,
					       &entry);
		if (buf_p->ret)
			break;
		remove_ttm_region(entry);
		buf_p->ret = drm_evict_ttm_region(entry);
		break;
	case ttm_destroy:
		buf_p->ret =
		    drm_ttm_region_from_handle(buf_p->region_handle, priv,
					       &entry);

		if (buf_p->ret)
			break;
		if (entry->owner) {
			drm_destroy_ttm_region(entry);
		} else {
			list_del(&entry->head);
			drm_user_destroy_region(entry);
		}
		drm_remove_ht_val(&dev->ttmreghash, buf_p->region_handle);
		break;
	default:
		DRM_ERROR("Invalid TTM buffer operation\n");
		buf_p->ret = -EINVAL;
		break;
	}

}

int drm_ttm_handle_bufs(drm_file_t * priv, drm_ttm_arg_t * ttm_arg)
{
	drm_device_t *dev = priv->head->dev;
	drm_mm_driver_t *mm_driver = dev->mm_driver;
	drm_ttm_buf_arg_t *bufs = NULL, *next, *buf_p;
	int i;
	volatile drm_mm_sarea_t *sa;

	static void *old_priv;

	if (ttm_arg->num_bufs > DRM_TTM_MAX_BUF_BATCH) {
		DRM_ERROR("Invalid number of TTM buffers.\n");
		return -EINVAL;
	}

	mm_driver->evicted_vram = FALSE;
	mm_driver->evicted_tt = FALSE;
	mm_driver->validated = FALSE;

	if (ttm_arg->num_bufs) {

		bufs =
		    drm_calloc(ttm_arg->num_bufs, sizeof(*bufs), DRM_MEM_TTM);

		if (!bufs) {
			DRM_ERROR("Out of kernel memory for buffers.\n");
			return -ENOMEM;
		}

		next = ttm_arg->first;
		buf_p = bufs;

		for (i = 0; i < ttm_arg->num_bufs; ++i) {
			if (DRM_COPY_FROM_USER
			    (buf_p, (void __user *)next, sizeof(*bufs)))
				break;
			next = buf_p->next;
			buf_p++;
		}
		if (i != ttm_arg->num_bufs) {
			drm_free(bufs, ttm_arg->num_bufs * sizeof(*bufs),
				 DRM_MEM_TTM);
			DRM_ERROR("Error copying buffer data\n");
			return -EFAULT;
		}
	}
	buf_p = bufs;

	down(&dev->struct_sem);
	if (ttm_arg->do_fence) {
		if (old_priv != priv)
			DRM_ERROR("Fence was from wrong client\n");
		drm_ttm_fence_regions(dev, &mm_driver->ttm_mm);
	}

	for (i = 0; i < ttm_arg->num_bufs; ++i) {
		drm_ttm_handle_buf(priv, buf_p);
		buf_p++;
	}
	up(&dev->struct_sem);

	if (ttm_arg->num_bufs) {
		old_priv = priv;
		next = ttm_arg->first;
		buf_p = bufs;
		for (i = 0; i < ttm_arg->num_bufs; ++i) {
			if (DRM_COPY_TO_USER
			    ((void __user *)next, buf_p, sizeof(*bufs)))
				break;
			next = buf_p->next;
			buf_p++;
		}

		drm_free(bufs, ttm_arg->num_bufs * sizeof(*bufs), DRM_MEM_TTM);
	}

	sa = mm_driver->mm_sarea;
	if (mm_driver->validated)
		sa->validation_seq++;
	if (mm_driver->evicted_vram)
		sa->evict_vram_seq++;
	if (mm_driver->evicted_tt)
		sa->evict_vram_seq++;

	return 0;
}

static int drm_ttm_handle_add(drm_file_t * priv, drm_ttm_arg_t * ttm_arg)
{
	drm_device_t *dev = priv->head->dev;
	drm_map_list_t *map_list;
	drm_ttm_t *ttm;
	int ret;

	down(&dev->struct_sem);
	ret = drm_add_ttm(dev, ttm_arg->size, &map_list);
	if (ret) {
		up(&dev->struct_sem);
		return ret;
	}
	list_add(&map_list->head, &priv->ttms);
	ttm = (drm_ttm_t *) map_list->map->offset;
	ttm->owner = priv;
	up(&dev->struct_sem);
	ttm_arg->handle = (uint32_t) map_list->user_token;
	return 0;
}

static int drm_ttm_handle_remove(drm_file_t * priv, drm_handle_t handle)
{
	drm_device_t *dev = priv->head->dev;
	drm_map_list_t *map_list;
	drm_ttm_t *ttm;
	int ret;

	down(&dev->struct_sem);
	ret = drm_ttm_from_handle(handle, priv, &ttm, &map_list);
	if (ret) {
		up(&dev->struct_sem);
		return ret;
	}
	ret = drm_destroy_ttm(ttm);
	list_del(&map_list->head);
	drm_remove_ht_val(&dev->maphash,
			  (handle - DRM_MAP_HASH_OFFSET) >> PAGE_SHIFT);
	up(&dev->struct_sem);
	if (!ret) {

		/*
		 * No active VMAs. So OK to free map here.
		 */

		drm_free(map_list->map, sizeof(*(map_list->map)), DRM_MEM_MAPS);
	}
	drm_free(map_list, sizeof(*map_list), DRM_MEM_MAPS);
	return 0;
}

/*
 * FIXME: Require lock only for validate, but not for evict, unbind or destroy.
 */

int drm_ttm_ioctl(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;
	int ret;
	drm_ttm_arg_t ttm_arg;

	DRM_COPY_FROM_USER_IOCTL(ttm_arg, (void __user *)data, sizeof(ttm_arg));
	switch (ttm_arg.op) {
	case ttm_add:
		if (ttm_arg.num_bufs) {
			LOCK_TEST_WITH_RETURN(dev, filp);
		}
		ret = drm_ttm_handle_add(priv, &ttm_arg);
		if (ret)
			return ret;
		if (!ttm_arg.num_bufs)
			break;
		ret = drm_ttm_handle_bufs(priv, &ttm_arg);
		if (ret)
			return ret;
		break;
	case ttm_bufs:
		LOCK_TEST_WITH_RETURN(dev, filp);
		ret = drm_ttm_handle_bufs(priv, &ttm_arg);
		if (ret)
			return ret;
		break;
	case ttm_remove:
		ret = drm_ttm_handle_remove(priv, ttm_arg.handle);
		if (ret)
			return ret;
		return 0;
	}

	DRM_COPY_TO_USER_IOCTL((void __user *)data, ttm_arg, sizeof(ttm_arg));
	return 0;
}

/*
 * FIXME: Temporarily non-static to allow for intel initialization hack.
 */

int drm_mm_do_takedown(drm_device_t * dev)
{
	if (!dev->mm_driver) {
		DRM_ERROR("Memory manager not initialized.\n");
		return -EINVAL;
	}

	drm_mm_takedown(&dev->mm_driver->vr_mm);
	drm_ttm_mm_takedown(&dev->mm_driver->ttm_mm);
	drm_rmmap_locked(dev, dev->mm_driver->mm_sarea_map->map);
	dev->mm_driver->takedown(dev->mm_driver);
	dev->mm_driver = NULL;
	return 0;
}

EXPORT_SYMBOL(drm_mm_do_takedown);

/*
 * FIXME: Temporarily non-static to allow for intel initialization hack.
 */

int drm_mm_do_init(drm_device_t * dev, drm_mm_init_arg_t * arg)
{
	int ret;
	unsigned long vr_size;
	unsigned long vr_offset;
	unsigned long tt_p_size;
	unsigned long tt_p_offset;
	drm_map_list_t *mm_sarea;

	if (dev->mm_driver) {
		DRM_ERROR("Trying to reinitialize memory manager.\n");
		return -EINVAL;
	}

	vr_size = arg->vr_size_lo;
	vr_offset = arg->vr_offset_lo;
	tt_p_size = arg->tt_p_size_lo;
	tt_p_offset = arg->tt_p_offset_lo;

	if (sizeof(vr_size) == 8) {
		int shift = 32;
		vr_size |= (arg->vr_size_lo << shift);
		vr_offset |= (arg->vr_offset_hi << shift);
		tt_p_size |= (arg->tt_p_size_hi << shift);
		tt_p_offset |= (arg->tt_p_offset_hi << shift);
	}

	dev->mm_driver = dev->driver->init_mm(dev);

	drm_ttm_mm_init(dev, &dev->mm_driver->ttm_mm, tt_p_offset, tt_p_size);
	drm_mm_init(&dev->mm_driver->vr_mm, vr_offset >> MM_VR_GRANULARITY,
		    vr_size >> MM_VR_GRANULARITY);

	if (!dev->mm_driver) {
		DRM_ERROR("Memory manager initialization failed.\n");
		return -EINVAL;
	}

	ret = drm_addmap_core(dev, 0, DRM_MM_SAREA_SIZE,
			      _DRM_SHM, 0, &mm_sarea);
	if (ret) {
		dev->mm_driver->takedown(dev->mm_driver);
		DRM_ERROR("Failed to add a memory manager SAREA.\n");
		return -ENOMEM;
	}

	dev->mm_driver->mm_sarea_map = mm_sarea;
	dev->mm_driver->mm_sarea = (drm_mm_sarea_t *) mm_sarea->map->handle;
	memset((void *)dev->mm_driver->mm_sarea, 0, DRM_MM_SAREA_SIZE);

	arg->mm_sarea = mm_sarea->user_token;

	return 0;
}

EXPORT_SYMBOL(drm_mm_do_init);

int drm_mm_init_ioctl(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	int ret;
	drm_mm_init_arg_t arg;

	if (!dev->driver->init_mm) {
		DRM_ERROR
		    ("Memory management not supported with this driver.\n");
		return -EINVAL;
	}

	DRM_COPY_FROM_USER_IOCTL(arg, (void __user *)data, sizeof(arg));

	down(&dev->struct_sem);
	switch (arg.op) {
	case mm_init:
		ret = drm_mm_do_init(dev, &arg);
		break;
	case mm_takedown:
		ret = drm_mm_do_takedown(dev);
		break;
	default:
		DRM_ERROR("Unsupported memory manager operation.\n");
		ret = -EINVAL;
	}
	up(&dev->struct_sem);

	if (ret)
		return ret;

	DRM_COPY_TO_USER_IOCTL((void __user *)data, arg, sizeof(arg));

	return 0;
}

EXPORT_SYMBOL(drm_mm_init_ioctl);

int drm_mm_fence_ioctl(DRM_IOCTL_ARGS)
{
	DRM_DEVICE;

	int ret;
	drm_fence_arg_t arg;
	drm_mm_driver_t *mm_driver = dev->mm_driver;

	LOCK_TEST_WITH_RETURN(dev, filp);
	if (!mm_driver) {
		DRM_ERROR("Memory manager is not initialized.\n");
		return -EINVAL;
	}

	DRM_COPY_FROM_USER_IOCTL(arg, (void __user *)data, sizeof(arg));

	ret = 0;
	switch (arg.op) {
	case emit_fence:
		arg.fence_seq = mm_driver->emit_fence(dev, arg.fence_type);
		break;
	case wait_fence:
		arg.ret = mm_driver->wait_fence(dev, arg.fence_type,
						arg.fence_seq);
		break;
	case test_fence:
		arg.ret = mm_driver->test_fence(dev, arg.fence_type,
						arg.fence_seq);
		break;
	default:
		DRM_ERROR("Unsupported memory manager operation.\n");
		ret = -EINVAL;
	}

	if (ret)
		return ret;

	DRM_COPY_TO_USER_IOCTL((void __user *)data, arg, sizeof(arg));

	return 0;
}
