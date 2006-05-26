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

#define DRM_TTM_UNMAP 0x01
#define DRM_TTM_REWRITE 0x02

static void change_pte_range(struct mm_struct *mm, pmd_t * pmd,
			     unsigned long addr, unsigned long end,
			     pgprot_t newprot, unsigned long pfn, int flags)
{
	pte_t *pte;

	pte = pte_offset_map(pmd, addr);
	do {
		if ((flags & DRM_TTM_UNMAP) && pte_present(*pte)) {
			pte_t ptent;
			ptent = *pte;
			ptep_get_and_clear(mm, addr, pte);
			lazy_mmu_prot_update(ptent);
		}
		if (flags & DRM_TTM_REWRITE) {
			unsigned long new_pfn = (pfn + addr) >> PAGE_SHIFT;
			pte_t ptent;
			ptep_get_and_clear(mm, addr, pte);
			ptent = pfn_pte(new_pfn, newprot);
			set_pte_at(mm, addr, pte, ptent);
			lazy_mmu_prot_update(ptent);
		}
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap(pte - 1);
}

static inline void change_pmd_range(struct mm_struct *mm, pud_t * pud,
				    unsigned long addr, unsigned long end,
				    pgprot_t newprot, unsigned long pfn,
				    int flags)
{
	pmd_t *pmd;
	unsigned long next;

	pmd = pmd_offset(pud, addr);
	do {
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_clear_bad(pmd))
			continue;
		change_pte_range(mm, pmd, addr, next, newprot, pfn, flags);
	} while (pmd++, addr = next, addr != end);
}

static inline void change_pud_range(struct mm_struct *mm, pgd_t * pgd,
				    unsigned long addr, unsigned long end,
				    pgprot_t newprot, unsigned long pfn,
				    int flags)
{
	pud_t *pud;
	unsigned long next;

	pud = pud_offset(pgd, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		change_pmd_range(mm, pud, addr, next, newprot, pfn, flags);
	} while (pud++, addr = next, addr != end);
}

static void drm_change_protection(struct vm_area_struct *vma,
				  unsigned long addr, unsigned long end,
				  pgprot_t newprot, unsigned long pfn,
				  int flags)
{
	struct mm_struct *mm = vma->vm_mm;
	pgd_t *pgd;
	unsigned long next;
	pfn = (pfn << PAGE_SHIFT) - addr;

	BUG_ON(addr >= end);
	pgd = pgd_offset(mm, addr);
	flush_cache_range(vma, addr, end);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		change_pud_range(mm, pgd, addr, next, newprot, pfn, flags);
	} while (pgd++, addr = next, addr != end);
}

/*
 * 8<----------------------------------------------------------------------------------
 * End linux mm subsystem code.
 */

typedef struct p_mm_entry {
	struct list_head head;
	struct mm_struct *mm;
	atomic_t refcount;
} p_mm_entry_t;

typedef struct drm_val_action {
	int needs_rx_flush;
	int evicted_tt;
	int evicted_vram;
	int validated;
} drm_val_action_t;

/*
 * We may be manipulating other processes page tables, so for each TTM, keep track of 
 * which mm_structs are currently mapping the ttm so that we can take the appropriate
 * locks when we modify their page tables. A typical application is when we evict another
 * process' buffers.
 */

int drm_ttm_add_mm_to_list(drm_ttm_t * ttm, struct mm_struct *mm)
{
	p_mm_entry_t *entry, *n_entry;

	list_for_each_entry(entry, &ttm->p_mm_list, head) {
		if (mm == entry->mm) {
			atomic_inc(&entry->refcount);
			return 0;
		} else if ((unsigned long)mm < (unsigned long)entry->mm) ;
	}

	n_entry = drm_alloc(sizeof(*n_entry), DRM_MEM_MM);
	if (!entry) {
		DRM_ERROR("Allocation of process mm pointer entry failed\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&n_entry->head);
	n_entry->mm = mm;
	atomic_set(&n_entry->refcount, 0);
	atomic_inc(&ttm->shared_count);
	ttm->mm_list_seq++;

	list_add_tail(&n_entry->head, &entry->head);

	return 0;
}

void drm_ttm_delete_mm(drm_ttm_t * ttm, struct mm_struct *mm)
{
	p_mm_entry_t *entry, *n;
	list_for_each_entry_safe(entry, n, &ttm->p_mm_list, head) {
		if (mm == entry->mm) {
			if (atomic_add_negative(-1, &entry->refcount)) {
				list_del(&entry->head);
				drm_free(entry, sizeof(*entry), DRM_MEM_MM);
				atomic_dec(&ttm->shared_count);
				ttm->mm_list_seq++;
			}
			return;
		}
	}
	BUG_ON(TRUE);
}

static void drm_ttm_lock_mm(drm_ttm_t * ttm, int mm_sem, int page_table)
{
	p_mm_entry_t *entry;

	list_for_each_entry(entry, &ttm->p_mm_list, head) {
		if (mm_sem) {
			down_write(&entry->mm->mmap_sem);
		}
		if (page_table) {
			spin_lock(&entry->mm->page_table_lock);
		}
	}
}

static void drm_ttm_unlock_mm(drm_ttm_t * ttm, int mm_sem, int page_table)
{
	p_mm_entry_t *entry;

	list_for_each_entry(entry, &ttm->p_mm_list, head) {
		if (page_table) {
			spin_unlock(&entry->mm->page_table_lock);
		}
		if (mm_sem) {
			up_write(&entry->mm->mmap_sem);
		}
	}
}

static int ioremap_vmas(drm_ttm_t * ttm, unsigned long page_offset,
			unsigned long num_pages, unsigned long aper_offset)
{
	struct list_head *list;
	int ret = 0;

	list_for_each(list, &ttm->vma_list->head) {
		drm_ttm_vma_list_t *entry =
		    list_entry(list, drm_ttm_vma_list_t, head);

		ret = io_remap_pfn_range(entry->vma,
					 entry->vma->vm_start +
					 (page_offset << PAGE_SHIFT),
					 (ttm->aperture_base >> PAGE_SHIFT) +
					 aper_offset, num_pages << PAGE_SHIFT,
					 drm_io_prot(_DRM_AGP, entry->vma));
		if (ret)
			break;
	}
	global_flush_tlb();
	return ret;
}

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
				      entry->vma->vm_page_prot, 0,
				      DRM_TTM_UNMAP);
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
		return -EBUSY;
	} else {
		DRM_DEBUG("Checking for busy regions.\n");
	}

	if (ttm->be_list) {
		list_for_each_safe(list, next, &ttm->be_list->head) {
			drm_ttm_backend_list_t *entry =
			    list_entry(list, drm_ttm_backend_list_t, head);
			if (!drm_find_ht_item(&ttm->dev->ttmreghash,
					      entry, &hash)) {
				drm_remove_ht_val(&ttm->dev->ttmreghash, hash);
			}
			drm_destroy_ttm_region(entry);
		}

		drm_free(ttm->be_list, sizeof(*ttm->be_list), DRM_MEM_MAPS);
		ttm->be_list = NULL;
	}

	if (atomic_read(&ttm->unfinished_regions) > 0) {
		DRM_DEBUG("Regions are still busy. Skipping destruction.\n");
		ttm->destroy = TRUE;
		return -EAGAIN;
	} else {
		DRM_DEBUG("About to really destroy ttm.\n");
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
				ClearPageReserved(*cur_page);
				__free_page(*cur_page);
			}
		}
		global_flush_tlb();
		vfree(ttm->pages);
		ttm->pages = NULL;
	}
	if (ttm->page_flags) {
		vfree(ttm->page_flags);
		ttm->page_flags = NULL;
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
		ttm->vma_list = NULL;
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
	atomic_set(&ttm->unfinished_regions, 0);
	ttm->destroy = FALSE;
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
	INIT_LIST_HEAD(&ttm->p_mm_list);
	atomic_set(&ttm->shared_count, 0);
	ttm->mm_list_seq = 0;

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
 * Lock the mmap_sems for processes that are mapping this ttm. 
 * This looks a bit clumsy, since we need to maintain the correct
 * locking order
 * mm->mmap_sem
 * dev->struct_sem;
 * and while we release dev->struct_sem to lock the mmap_sems, 
 * the mmap_sem list may have been updated. We need to revalidate
 * it after relocking dev->struc_sem.
 */

static int drm_ttm_lock_mmap_sem(drm_ttm_t * ttm)
{
	struct mm_struct **mm_list = NULL, **mm_list_p;
	uint32_t list_seq;
	uint32_t cur_count, shared_count;
	p_mm_entry_t *entry;
	unsigned i;

	cur_count = 0;
	list_seq = ttm->mm_list_seq;
	shared_count = atomic_read(&ttm->shared_count);

	do {
		if (shared_count > cur_count) {
			if (mm_list)
				drm_free(mm_list, sizeof(*mm_list) * cur_count,
					 DRM_MEM_MM);
			cur_count = shared_count + 10;
			mm_list =
			    drm_alloc(sizeof(*mm_list) * cur_count, DRM_MEM_MM);
			if (!mm_list)
				return -ENOMEM;
		}

		mm_list_p = mm_list;
		list_for_each_entry(entry, &ttm->p_mm_list, head) {
			*mm_list_p++ = entry->mm;
		}

		up(&ttm->dev->struct_sem);
		mm_list_p = mm_list;
		for (i = 0; i < shared_count; ++i, ++mm_list_p) {
			down_write(&((*mm_list_p)->mmap_sem));
		}

		down(&ttm->dev->struct_sem);

		if (list_seq != ttm->mm_list_seq) {
			mm_list_p = mm_list;
			for (i = 0; i < shared_count; ++i, ++mm_list_p) {
				up_write(&((*mm_list_p)->mmap_sem));
			}

		}
		shared_count = atomic_read(&ttm->shared_count);

	} while (list_seq != ttm->mm_list_seq);

	if (mm_list)
		drm_free(mm_list, sizeof(*mm_list) * cur_count, DRM_MEM_MM);

	ttm->mmap_sem_locked = TRUE;
	return 0;
}

/*
 * Change caching policy for range of pages in a ttm.
 */

static int drm_set_caching(drm_ttm_t * ttm, unsigned long page_offset,
			   unsigned long num_pages, int noncached,
			   int do_tlbflush)
{
	int i, cur;
	struct page **cur_page;
	pgprot_t attr = (noncached) ? PAGE_KERNEL_NOCACHE : PAGE_KERNEL;

	drm_ttm_lock_mm(ttm, FALSE, TRUE);
	unmap_vma_pages(ttm, page_offset, num_pages);

	for (i = 0; i < num_pages; ++i) {
		cur = page_offset + i;
		cur_page = ttm->pages + cur;
		if (*cur_page) {
			if (PageHighMem(*cur_page)) {
				if (noncached
				    && page_address(*cur_page) != NULL) {
					DRM_ERROR
					    ("Illegal mapped HighMem Page\n");
					drm_ttm_unlock_mm(ttm, FALSE, TRUE);
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
	if (do_tlbflush)
		global_flush_tlb();

	drm_ttm_unlock_mm(ttm, FALSE, TRUE);
	return 0;
}

/*
 * Wait for a maximum of one second for a buffer to become free.
 * Note that we block completely in a buzy wait loop here 
 * if a signal is pending. Could be made to behave more nicely.
 */

static int drm_wait_buf_busy(drm_ttm_backend_list_t * entry)
{
	drm_mm_node_t *mm_node = entry->mm_node;
	drm_device_t *dev = entry->mm->dev;
	drm_ttm_mm_priv_t *mm_priv;
	unsigned long end = jiffies + DRM_HZ;
	int ret = 0;

	if (!mm_node)
		return 0;

	mm_priv = (drm_ttm_mm_priv_t *) mm_node->private;
	if (mm_priv->fence_valid) {
		do {
			ret = dev->mm_driver->wait_fence(dev, entry->fence_type,
							 mm_priv->fence);
			if (time_after_eq(jiffies, end))
				ret = -EBUSY;
		} while (ret == -EAGAIN);
	}
	return ret;
}

/*
 * Take a ttm region out of the aperture manager.
 */

static int remove_ttm_region(drm_ttm_backend_list_t * entry, int ret_if_busy)
{

	drm_mm_node_t *mm_node = entry->mm_node;
	drm_ttm_mm_priv_t *mm_priv;
	drm_ttm_mm_t *mm = entry->mm;
	drm_device_t *dev = mm->dev;
	int ret;

	if (!mm_node)
		return 0;

	mm_priv = (drm_ttm_mm_priv_t *) mm_node->private;

	if (!mm_priv)
		return 0;

	if (mm_priv->fence_valid) {
		if (ret_if_busy
		    && !dev->mm_driver->test_fence(mm->dev, entry->fence_type,
						   mm_priv->fence)) {
			DRM_DEBUG("Fence not fulfilled\n");
			return -EBUSY;
		}
		ret = drm_wait_buf_busy(entry);
		if (ret) {
			DRM_DEBUG("Nope, buf busy.\n");
			return ret;
		}
	}
	entry->mm_node = NULL;
	mm_node->private = NULL;
	list_del(&mm_priv->lru);
	drm_mm_put_block(&mm->mm, mm_node);
	drm_free(mm_priv, sizeof(*mm_priv), DRM_MEM_MM);
	return 0;
}

/*
 * Unbind a ttm region from the aperture and take it out of the
 * aperture manager.
 */

int drm_evict_ttm_region(drm_ttm_backend_list_t * entry)
{
	drm_ttm_backend_t *be = entry->be;
	drm_ttm_t *ttm = entry->owner;
	int ret;

	if (be) {
		switch (entry->state) {
		case ttm_bound:
			if (ttm && be->needs_cache_adjust(be)) {
				BUG_ON(entry->flags & DRM_MM_CACHED);
				ret = drm_ttm_lock_mmap_sem(ttm);
				if (ret)
					return ret;
				drm_ttm_lock_mm(ttm, FALSE, TRUE);
				unmap_vma_pages(ttm, entry->page_offset,
						entry->num_pages);
				global_flush_tlb();
				drm_ttm_unlock_mm(ttm, FALSE, TRUE);
			}
			be->unbind(entry->be);
			if (ttm && be->needs_cache_adjust(be)) {
				drm_set_caching(ttm, entry->page_offset,
						entry->num_pages, 0, 1);
				drm_ttm_unlock_mm(ttm, TRUE, FALSE);
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
	remove_ttm_region(entry, FALSE);
	drm_evict_ttm_region(entry);
	entry->state = ttm_unbound;
}

/*
 * This should be called every once in a while to destroy regions and ttms that are
 * put on hold for destruction because their fence had not retired.
 */

int drm_ttm_destroy_delayed(drm_ttm_mm_t * mm, int ret_if_busy)
{
	struct list_head *list, *next;
	drm_ttm_t *ttm;

	list_for_each_safe(list, next, &mm->delayed) {
		drm_ttm_backend_list_t *entry =
		    list_entry(list, drm_ttm_backend_list_t, head);
		DRM_DEBUG("Trying to remove put-on-hold from aperture\n");
		if (remove_ttm_region(entry, ret_if_busy))
			continue;

		list_del_init(list);
		ttm = entry->owner;
		if (ttm) {
			DRM_DEBUG("Destroying put-on-hold region\n");
			drm_destroy_ttm_region(entry);
			atomic_dec(&ttm->unfinished_regions);
			if (ttm->destroy
			    && atomic_read(&ttm->unfinished_regions) == 0) {
				DRM_DEBUG("Destroying put-on-hold ttm\n");
				drm_destroy_ttm(ttm);

			}
		} else {
			drm_user_destroy_region(entry);
		}
	}
	return (mm->delayed.next == &mm->delayed);
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

	list_del_init(&entry->head);

	if (remove_ttm_region(entry, TRUE)) {
		DRM_DEBUG("Putting destruction on hold\n");
		atomic_inc(&ttm->unfinished_regions);
		list_add_tail(&entry->head, &entry->mm->delayed);
		return;
	}
	drm_unbind_ttm_region(entry);
	if (be) {
		be->clear(entry->be);
		if (be->needs_cache_adjust(be)) {
			int ret = drm_ttm_lock_mmap_sem(ttm);
			drm_set_caching(ttm, entry->page_offset,
					entry->num_pages, 0, 1);
			if (!ret)
				drm_ttm_unlock_mm(ttm, TRUE, FALSE);
		}
		be->destroy(be);
	}
	cur_page_flags = ttm->page_flags + entry->page_offset;
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

	cur_page_flags = ttm->page_flags + page_offset;
	for (i = 0; i < n_pages; ++i, ++cur_page_flags) {
		if (*cur_page_flags & DRM_TTM_PAGE_USED) {
			DRM_ERROR("TTM region overlap\n");
			return -EINVAL;
		} else {
			DRM_MASK_VAL(*cur_page_flags, DRM_TTM_PAGE_USED,
				     DRM_TTM_PAGE_USED);
		}
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
			*cur_page = alloc_page(GFP_KERNEL);
			if (!*cur_page) {
				DRM_ERROR("Page allocation failed\n");
				drm_destroy_ttm_region(entry);
				return -ENOMEM;
			}
			SetPageReserved(*cur_page);
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
	int ret = 0;
	drm_ttm_backend_t *be;
	drm_ttm_t *ttm;

	if (!region || region->state == ttm_bound)
		return -EINVAL;

	be = region->be;
	ttm = region->owner;

	if (ttm && be->needs_cache_adjust(be)) {
		BUG_ON(region->flags & DRM_MM_CACHED);
		ret = drm_ttm_lock_mmap_sem(ttm);
		if (ret)
			return ret;
		drm_set_caching(ttm, region->page_offset, region->num_pages,
				DRM_TTM_PAGE_UNCACHED, TRUE);
	} else {
		DRM_DEBUG("Binding cached\n");
	}

	if ((ret = be->bind(be, aper_offset))) {
		if (ttm && be->needs_cache_adjust(be))
			drm_ttm_unlock_mm(ttm, TRUE, FALSE);
		drm_unbind_ttm_region(region);
		DRM_ERROR("Couldn't bind backend.\n");
		return ret;
	}

	cur_page_flag = ttm->page_flags + region->page_offset;
	for (i = 0; i < region->num_pages; ++i) {
		DRM_MASK_VAL(*cur_page_flag, DRM_TTM_MASK_PFN,
			     (i + aper_offset) << PAGE_SHIFT);
		cur_page_flag++;
	}

	if (ttm && be->needs_cache_adjust(be)) {
		ioremap_vmas(ttm, region->page_offset, region->num_pages,
			     aper_offset);
		drm_ttm_unlock_mm(ttm, TRUE, FALSE);
	}

	region->state = ttm_bound;
	return 0;
}

int drm_rebind_ttm_region(drm_ttm_backend_list_t * entry,
			  unsigned long aper_offset)
{
	return drm_bind_ttm_region(entry, aper_offset);

}

void drm_fence_unfenced_region(drm_ttm_backend_list_t * entry)
{
	drm_mm_node_t *mm_node;
	drm_ttm_mm_priv_t *mm_priv;
	uint32_t fence;
	drm_device_t *dev;

	if (!entry)
		return;

	dev = entry->mm->dev;
	mm_node = entry->mm_node;
	if (!mm_node)
		return;

	mm_priv = (drm_ttm_mm_priv_t *) mm_node->private;
	if (mm_priv->fence_valid)
		return;

	BUG_ON(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock));
	DRM_DEBUG("Fencing unfenced region\n");
	fence = dev->mm_driver->emit_fence(dev, entry->fence_type);
	mm_priv->fence = fence;
	mm_priv->fence_valid = TRUE;
}

void drm_ttm_fence_before_destroy(drm_ttm_t * ttm)
{
	drm_ttm_backend_list_t *entry;

	list_for_each_entry(entry, &ttm->be_list->head, head) {
		drm_fence_unfenced_region(entry);
	}
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

	if (remove_ttm_region(entry, TRUE)) {
		list_add_tail(&entry->head, &entry->mm->delayed);
		return;
	}

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
	return;
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
		return -ENOMEM;
	}

	list->user_token =
	    (list->user_token << PAGE_SHIFT) + DRM_MAP_HASH_OFFSET;
	list->map = map;

	*maplist = list;

	return 0;
}

static int drm_ttm_evict_aged(drm_ttm_mm_t * mm)
{
	struct list_head *list;
	drm_ttm_mm_priv_t *evict_priv;
	uint32_t evict_fence;
	drm_device_t *dev = mm->dev;
	drm_mm_node_t *evict_node;
	int evicted = FALSE;

	do {
		list = mm->lru_head.next;

		if (list == &mm->lru_head)
			break;

		evict_priv = list_entry(list, drm_ttm_mm_priv_t, lru);
		if (!evict_priv->fence_valid)
			break;

		evict_fence = evict_priv->fence;
		if (!dev->mm_driver->
		    test_fence(dev, evict_priv->region->fence_type,
			       evict_fence))
			break;

		if (!dev->mm_driver->
		    fence_aged(dev, evict_priv->region->fence_type,
			       evict_fence))
			break;
		evicted = TRUE;
		evict_node = evict_priv->region->mm_node;
		drm_evict_ttm_region(evict_priv->region);
		list_del_init(list);
		evict_node->private = NULL;
		drm_mm_put_block(&mm->mm, evict_node);
		evict_priv->region->mm_node = NULL;
		drm_free(evict_priv, sizeof(*evict_priv), DRM_MEM_MM);
	} while (TRUE);

	return evicted;
}

/*
 * Fence all unfenced regions in the global lru list. 
 * FIXME: This is exported until we have a scheduler built in.
 * The driver's command dispatcher is responsible for calling
 * this function.
 */

void drm_ttm_fence_regions(drm_device_t * dev)
{
	int emitted[DRM_FENCE_TYPES];
	uint32_t fence_seqs[DRM_FENCE_TYPES];
	struct list_head *list;
	uint32_t fence_type;
	uint32_t fence;
	drm_ttm_mm_t *mm = &dev->mm_driver->ttm_mm;
	static int check_aged = 0;

	memset(emitted, 0, sizeof(int) * DRM_FENCE_TYPES);

	list_for_each_prev(list, &mm->lru_head) {
		drm_ttm_mm_priv_t *entry =
		    list_entry(list, drm_ttm_mm_priv_t, lru);
		if (entry->fence_valid)
			break;

		fence_type = entry->region->fence_type;

		if (!emitted[fence_type]) {
			BUG_ON(!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock));
			fence = dev->mm_driver->emit_fence(dev, fence_type);
			fence_seqs[fence_type] = fence;
			emitted[fence_type] = TRUE;
		} else {
			fence = fence_seqs[fence_type];
		}

		entry->fence = fence;
		entry->fence_valid = TRUE;
	}

	if (!(check_aged++ & 0x0F) && drm_ttm_evict_aged(mm)) {
		dev->mm_driver->mm_sarea->evict_tt_seq =
		    dev->mm_driver->mm_sarea->validation_seq + 1;
	}
}

EXPORT_SYMBOL(drm_ttm_fence_regions);

/*
 * Evict the first (oldest) region on the lru list, after its fence
 * is fulfilled. Will fail if the lru list is empty (nothing to evict),
 * or the first node doesn't have a fence which means it is a newly
 * validated region which the user intends not to be evicted yet.
 * May sleep while waiting for a fence.
 */

static int drm_ttm_evict_lru(drm_ttm_backend_list_t * entry)
{
	struct list_head *list;
	drm_ttm_mm_t *mm = entry->mm;
	drm_ttm_mm_priv_t *evict_priv;
	uint32_t evict_fence;
	drm_device_t *dev = mm->dev;
	drm_mm_node_t *evict_node;
	int ret;

/*
 * We must use a loop here, since the list might be updated while we release
 * the locks to wait for fence.
 */

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
		if (dev->mm_driver->
		    test_fence(dev, evict_priv->region->fence_type,
			       evict_fence))
			break;

		up(&dev->struct_sem);

		ret = drm_wait_buf_busy(evict_priv->region);

		down(&dev->struct_sem);

		if (ret) {
			DRM_ERROR("Evict wait timed out\n");
			return ret;
		}

	} while (TRUE);
	evict_node = evict_priv->region->mm_node;
	drm_evict_ttm_region(evict_priv->region);
	list_del_init(list);
	evict_node->private = NULL;
	drm_mm_put_block(&mm->mm, evict_node);
	evict_priv->region->mm_node = NULL;
	drm_free(evict_priv, sizeof(*evict_priv), DRM_MEM_MM);

	return 0;
}

static int drm_validate_ttm_region(drm_ttm_backend_list_t * entry,
				   unsigned *aper_offset,
				   drm_val_action_t * action,
				   uint32_t validation_seq)
{
	drm_mm_node_t *mm_node = entry->mm_node;
	drm_ttm_mm_t *mm = entry->mm;
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
	drm_ttm_destroy_delayed(entry->mm, TRUE);
	while (!mm_node) {
		mm_node =
		    drm_mm_search_free(&entry->mm->mm, num_pages, 0, 0);
		if (!mm_node) {
			ret = drm_ttm_evict_lru(entry);
			if (ret) {
				return ret;
			}
			action->evicted_tt = TRUE;
		}
	}

	if (!entry->mm_node) {
		mm_node = drm_mm_get_block(mm_node, num_pages, 0);
		mm_node->private = mm_priv;
		mm_priv->region = entry;
		entry->mm_node = mm_node;
	} else {
		list_del_init(&mm_priv->lru);
	}

	mm_priv->fence_valid = FALSE;
	mm_priv->val_seq = validation_seq;

	if (!entry->pinned)
		list_add_tail(&mm_priv->lru, &mm->lru_head);

	switch (entry->state) {
	case ttm_bound:
		break;
	case ttm_evicted:
		drm_rebind_ttm_region(entry, mm_node->start);
		action->needs_rx_flush = TRUE;
		break;
	case ttm_unbound:
	default:
		drm_bind_ttm_region(entry, mm_node->start);
		action->needs_rx_flush = TRUE;
		break;
	}

	*aper_offset = mm_node->start;
	return 0;
}

static void drm_ttm_mm_init(drm_device_t * dev, drm_ttm_mm_t * mm,
			    unsigned long start, unsigned long size)
{
	drm_mm_init(&mm->mm, start, size);
	INIT_LIST_HEAD(&mm->lru_head);
	INIT_LIST_HEAD(&mm->delayed);
	mm->dev = dev;
}

static void drm_ttm_mm_takedown(drm_ttm_mm_t * mm)
{
	if (!drm_ttm_destroy_delayed(mm, FALSE)) {
		DRM_ERROR("DRM_MM: Inconsistency: "
			  "There are still used buffers\n");

		/*
		 * FIXME: Clean this up in a nice way.
		 */

	}
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

	cached = buf->flags & DRM_MM_CACHED;

	if ((ret = drm_create_ttm_region(ttm, buf->ttm_page_offset,
					 buf->num_pages, cached, &entry)))
		return ret;

	if ((ret = drm_insert_ht_val(&dev->ttmreghash, entry, &region))) {
		drm_destroy_ttm_region(entry);
		return ret;
	}
	buf->region_handle = (drm_handle_t) region;

	entry->pinned = 0;
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

	if (!(buf_p->flags & DRM_MM_NEW))
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
	entry->pinned = 0;
	*created = entry;
	return 0;
}

static int drm_ttm_validate_buf(drm_file_t * priv, drm_ttm_buf_arg_t * buf_p,
				drm_val_action_t * action,
				uint32_t validation_seq)
{
	int ret;
	int pin_buffer;
	uint32_t flags = 0;
	drm_ttm_t *ttm;
	drm_device_t *dev = priv->head->dev;
	drm_ttm_backend_list_t *entry;

	if (buf_p->flags & DRM_MM_NEW) {
		flags |= DRM_MM_NEW;
		ret = drm_ttm_from_handle(buf_p->ttm_handle, priv, &ttm, NULL);
		if (ret)
			goto out;
		ret = drm_ttm_create_buffer(dev, ttm, buf_p, &entry);
		if (ret)
			goto out;
		flags &= ~DRM_MM_NEW;
	} else {
		ret = drm_ttm_region_from_handle(buf_p->region_handle,
						 priv, &entry);
		if (ret)
			goto out;
		flags = entry->flags;
	}

	if (buf_p->fence_type > dev->mm_driver->fence_types) {
		DRM_ERROR("Illegal fence type\n");
		ret = -EINVAL;
		goto out;
	}

	if (entry->owner == NULL && !(flags & DRM_MM_CACHED)) {

		DRM_ERROR("Only cached user regions allowed\n");
		ret = -EINVAL;
		goto out;

	} else if (((flags & DRM_MM_CACHED) != (buf_p->flags & DRM_MM_CACHED))
		   && ((buf_p->flags & DRM_MM_TT) != 0)
		   && dev->mm_driver->cached_pages) {

		drm_unbind_ttm_region(entry);
		flags = (flags & ~DRM_MM_MEMTYPE_MASK) | DRM_MM_SYSTEM;

	}

	if ((flags & buf_p->flags & DRM_MM_MEMTYPE_MASK) == 0) {

		if (buf_p->flags & DRM_MM_SYSTEM) {
			drm_unbind_ttm_region(entry);
			flags = (flags & ~DRM_MM_MEMTYPE_MASK) | DRM_MM_SYSTEM;

		} else if (buf_p->flags & DRM_MM_TT) {

			flags = (flags & ~DRM_MM_MEMTYPE_MASK) | DRM_MM_TT;
		} else {
			DRM_ERROR("VRAM is not implemented yet.\n");
			ret = -EINVAL;
			goto out;
		}
	}

	if (flags & (DRM_MM_TT | DRM_MM_VRAM)) {
		ret = drm_wait_buf_busy(entry);
		if (ret)
			goto out;

		pin_buffer = (buf_p->flags & DRM_MM_NO_EVICT) != 0;
		entry->pinned = pin_buffer;
		entry->fence_type = buf_p->fence_type;

		ret = drm_validate_ttm_region(entry, &buf_p->aper_offset,
					      action, validation_seq);
		if (ret)
			goto out;
	}

	if (!entry->be->needs_cache_adjust(entry->be))
		flags |= DRM_MM_CACHED;

	flags &= ~(DRM_MM_NO_EVICT | DRM_MM_READ | DRM_MM_EXE | DRM_MM_WRITE);
	flags |= (buf_p->flags & (DRM_MM_NO_EVICT | DRM_MM_READ | DRM_MM_EXE |
				  DRM_MM_WRITE));
	entry->flags = flags;
	buf_p->flags = flags;

	return 0;
      out:
	buf_p->flags = flags;
	return ret;
}

/*
 * Handle the buffer operations for a single buffer.
 */

static void drm_ttm_handle_buf(drm_file_t * priv, drm_ttm_buf_arg_t * buf_p,
			       drm_val_action_t * action,
			       uint32_t validation_seq)
{
	drm_device_t *dev = priv->head->dev;
	drm_ttm_backend_list_t *entry;
	drm_ttm_mm_t *ttm_mm;

	switch (buf_p->op) {
	case ttm_validate_user:
		if (buf_p->flags & DRM_MM_NEW) {

			buf_p->ret =
			    drm_ttm_create_user_buf(buf_p, buf_p->user_addr,
						    buf_p->user_size, priv,
						    &entry);
			if (buf_p->ret) {
				buf_p->flags = 0;
				break;
			}

			buf_p->flags &= ~DRM_MM_NEW;
		}
		buf_p->ret = drm_ttm_validate_buf(priv, buf_p, action,
						  validation_seq);
		break;
	case ttm_validate:
		buf_p->ret = drm_ttm_validate_buf(priv, buf_p, action,
						  validation_seq);
		break;
	case ttm_destroy:
		buf_p->ret =
		    drm_ttm_region_from_handle(buf_p->region_handle, priv,
					       &entry);

		drm_remove_ht_val(&dev->ttmreghash, buf_p->region_handle);
		ttm_mm = entry->mm;
		if (buf_p->ret)
			break;
		if (entry->owner) {
			drm_destroy_ttm_region(entry);
		} else {
			list_del_init(&entry->head);
			drm_user_destroy_region(entry);
		}
		drm_ttm_destroy_delayed(ttm_mm, TRUE);
		break;
	default:
		DRM_ERROR("Invalid TTM buffer operation\n");
		buf_p->ret = -EINVAL;
		break;
	}

}

/*
 * Download a buffer list from user space and dispatch each buffer to
 * drm_ttm_handle_buf
 */

int drm_ttm_handle_bufs(drm_file_t * priv, drm_ttm_arg_t * ttm_arg)
{
	drm_device_t *dev = priv->head->dev;
	drm_mm_driver_t *mm_driver = dev->mm_driver;
	drm_ttm_buf_arg_t *bufs = NULL, *next, *buf_p;
	int i;
	volatile drm_mm_sarea_t *sa;
	drm_val_action_t action;

	static void *old_priv;

	memset(&action, 0, sizeof(action));

	if (ttm_arg->num_bufs > DRM_TTM_MAX_BUF_BATCH) {
		DRM_ERROR("Invalid number of TTM buffers.\n");
		return -EINVAL;
	}

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
	sa = mm_driver->mm_sarea;

	for (i = 0; i < ttm_arg->num_bufs; ++i) {
		if (!action.validated && (buf_p->op == ttm_validate ||
					  buf_p->op == ttm_validate_user)) {
			sa->validation_seq++;
			action.validated = TRUE;
		}

		drm_ttm_handle_buf(priv, buf_p, &action, sa->validation_seq);
		buf_p++;
	}

	if (action.evicted_vram)
		sa->evict_vram_seq = sa->validation_seq;
	if (action.evicted_tt)
		sa->evict_tt_seq = sa->validation_seq;

	ttm_arg->val_seq = sa->validation_seq;
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

	if (action.needs_rx_flush) {
/*
 * We have inserted new pages and need to make sure that the GPU flushes
 * read- and exe caches before they are used.
 */
		dev->mm_driver->flush_caches(dev,
					     DRM_FLUSH_READ | DRM_FLUSH_EXE);
	}

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
	list_del(&map_list->head);
	drm_remove_ht_val(&dev->maphash,
			  (handle - DRM_MAP_HASH_OFFSET) >> PAGE_SHIFT);
	ret = drm_destroy_ttm(ttm);
	drm_ttm_destroy_delayed(&dev->mm_driver->ttm_mm, TRUE);
	up(&dev->struct_sem);

	if (ret != -EBUSY) {

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
	int ret = 0;
	drm_ttm_arg_t ttm_arg;

	down(&dev->ttm_sem);
	if (!dev->mm_driver) {
		DRM_ERROR("Called without initialization\n");
		up(&dev->ttm_sem);
		return -EINVAL;
	}

	DRM_COPY_FROM_USER_IOCTL(ttm_arg, (void __user *)data, sizeof(ttm_arg));
	switch (ttm_arg.op) {
	case ttm_add:
		if (ttm_arg.num_bufs) {
			LOCK_TEST_WITH_RETURN(dev, filp);
		}
		ret = drm_ttm_handle_add(priv, &ttm_arg);
		if (ret)
			break;
		if (!ttm_arg.num_bufs)
			break;
		ret = drm_ttm_handle_bufs(priv, &ttm_arg);
		if (ret)
			break;
		break;
	case ttm_bufs:
		LOCK_TEST_WITH_RETURN(dev, filp);
		ret = drm_ttm_handle_bufs(priv, &ttm_arg);
		if (ret)
			break;
		break;
	case ttm_remove:
		ret = drm_ttm_handle_remove(priv, ttm_arg.handle);
		if (ret)
			break;
		break;
	}

	up(&dev->ttm_sem);
	if (ret)
		return ret;

	DRM_COPY_TO_USER_IOCTL((void __user *)data, ttm_arg, sizeof(ttm_arg));
	return 0;
}

/*
 * FIXME: Temporarily non-static to allow for intel initialization hack.
 */

int drm_mm_do_takedown(drm_device_t * dev)
{
	drm_mm_driver_t *mm_driver;

	if (!dev->mm_driver) {
		DRM_ERROR("Memory manager not initialized.\n");
		return -EINVAL;
	}

	mm_driver = dev->mm_driver;
	drm_ttm_mm_takedown(&mm_driver->ttm_mm);
	drm_rmmap_locked(dev, mm_driver->mm_sarea_map->map);
	dev->mm_driver = NULL;
	mm_driver->takedown(dev->mm_driver);

	return 0;
}

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
	drm_mm_driver_t *mm_driver;

	if (dev->mm_driver) {
		DRM_ERROR("Trying to reinitialize memory manager.\n");
		return -EINVAL;
	}

	vr_size = arg->req.vr_size_lo;
	vr_offset = arg->req.vr_offset_lo;
	tt_p_size = arg->req.tt_p_size_lo;
	tt_p_offset = arg->req.tt_p_offset_lo;

	if (sizeof(vr_size) == 8) {
		int shift = 32;
		vr_size |= (arg->req.vr_size_hi << shift);
		vr_offset |= (arg->req.vr_offset_hi << shift);
		tt_p_size |= (arg->req.tt_p_size_hi << shift);
		tt_p_offset |= (arg->req.tt_p_offset_hi << shift);
	}

	DRM_DEBUG("Offset 0x%lx, Pages %ld\n",
		  tt_p_offset << PAGE_SHIFT, tt_p_size);

	mm_driver = dev->driver->init_mm(dev);

	if (!mm_driver) {
		DRM_ERROR("Memory manager initialization failed.\n");
		return -EINVAL;
	}

	down(&dev->struct_sem);
	dev->mm_driver = mm_driver;
	up(&dev->struct_sem);

	drm_ttm_mm_init(dev, &dev->mm_driver->ttm_mm, tt_p_offset, tt_p_size);
	drm_mm_init(&dev->mm_driver->vr_mm, vr_offset >> MM_VR_GRANULARITY,
		    vr_size >> MM_VR_GRANULARITY);

	ret = drm_addmap_core(dev, 0, DRM_MM_SAREA_SIZE,
			      _DRM_SHM, _DRM_READ_ONLY, &mm_sarea);

	if (ret) {
		DRM_ERROR("Failed to add a memory manager SAREA.\n");
		dev->mm_driver->takedown(dev->mm_driver);
		return -ENOMEM;
	}

	dev->mm_driver->mm_sarea_map = mm_sarea;
	dev->mm_driver->mm_sarea = (drm_mm_sarea_t *) mm_sarea->map->handle;
	memset((void *)dev->mm_driver->mm_sarea, 0, DRM_MM_SAREA_SIZE);

	arg->rep.mm_sarea = mm_sarea->user_token;
	arg->rep.kernel_emit = dev->mm_driver->kernel_emit;
	arg->rep.fence_types = dev->mm_driver->fence_types;
	return 0;
}

EXPORT_SYMBOL(drm_mm_do_init);

static int drm_mm_do_query(drm_device_t * dev, drm_mm_init_arg_t * arg)
{
	if (!dev->mm_driver) {
		DRM_ERROR("Memory manager query without initialization\n");
		return -EINVAL;
	}

	arg->rep.mm_sarea = dev->mm_driver->mm_sarea_map->user_token;
	arg->rep.kernel_emit = dev->mm_driver->kernel_emit;
	arg->rep.fence_types = dev->mm_driver->fence_types;

	return 0;
}

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

	down(&dev->ttm_sem);
	switch (arg.req.op) {
	case mm_init:
		if (!DRM_SUSER(DRM_CURPROC) && !priv->master) {
			ret = -EPERM;
			break;
		}
		ret = drm_mm_do_init(dev, &arg);
		break;
	case mm_takedown:
		if (!DRM_SUSER(DRM_CURPROC) && !priv->master) {
			ret = -EPERM;
			break;
		}
		ret = drm_mm_do_takedown(dev);
		break;
	case mm_query:
		ret = drm_mm_do_query(dev, &arg);
		break;
	default:
		DRM_ERROR("Unsupported memory manager operation.\n");
		ret = -EINVAL;
	}
	up(&dev->ttm_sem);

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
	drm_mm_driver_t *mm_driver;

	down(&dev->ttm_sem);

	mm_driver = dev->mm_driver;

	if (!mm_driver) {
		DRM_ERROR("Memory manager is not initialized.\n");
		up(&dev->ttm_sem);
		return -EINVAL;
	}

	DRM_COPY_FROM_USER_IOCTL(arg, (void __user *)data, sizeof(arg));

	ret = 0;
	down(&dev->struct_sem);
	switch (arg.req.op) {
	case emit_fence:
		LOCK_TEST_WITH_RETURN(dev, filp);
		arg.rep.fence_seq =
		    mm_driver->emit_fence(dev, arg.req.fence_type);
		break;
	case wait_fence:
		ret = mm_driver->wait_fence(dev, arg.req.fence_type,
					    arg.req.fence_seq);
		break;
	case test_fence:
		arg.rep.ret = mm_driver->test_fence(dev, arg.req.fence_type,
						    arg.req.fence_seq);
		break;
	default:
		DRM_ERROR("Unsupported memory manager operation.\n");
		ret = -EINVAL;
	}

	up(&dev->struct_sem);
	up(&dev->ttm_sem);
	if (ret)
		return ret;

	DRM_COPY_TO_USER_IOCTL((void __user *)data, arg, sizeof(arg));

	return 0;
}
