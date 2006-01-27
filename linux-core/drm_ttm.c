#include "drmP.h"
#include <linux/rmap.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>

/*
 * Change the page protecting of an existing vma. Stolen from linux memory.c and mprotect.c
 */

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
}

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
					      list, &hash))
				drm_remove_ht_val(&ttm->dev->ttmreghash, hash);
			drm_unbind_ttm_region(entry);
		}

		drm_free(ttm->be_list, sizeof(*ttm->be_list), DRM_MEM_MAPS);
	}

	if (ttm->pages) {
		for (i = 0; i < ttm->num_pages; ++i) {
			cur_page = ttm->pages + i;
			if (ttm->nocached && ttm->nocached[i] &&
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

	if (ttm->nocached) {
		vfree(ttm->nocached);
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
 * FIXME: Avoid using vmalloc for the page- and nocached tables?
 */

drm_ttm_t *drm_init_ttm(struct drm_device * dev, unsigned long size)
{

	drm_ttm_t *ttm;

	if (!dev->driver->create_ttm_backend_entry)
		return NULL;

	ttm = drm_calloc(1, sizeof(*ttm), DRM_MEM_MAPS);
	if (!ttm)
		return NULL;

	ttm->lhandle = 0;
	atomic_set(&ttm->vma_count, 0);
	ttm->num_pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	ttm->nocached = vmalloc(ttm->num_pages * sizeof(*ttm->nocached));
	if (!ttm->nocached) {
		drm_destroy_ttm(ttm);
		DRM_ERROR("Failed allocating nocached table\n");
		return NULL;
	}
	memset(ttm->nocached, 0, ttm->num_pages);

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

static void restore_vma_protection(drm_ttm_t * ttm, unsigned long page_offset,
				   unsigned long num_pages)
{
	struct list_head *list;

	return;
	list_for_each(list, &ttm->vma_list->head) {
		drm_ttm_vma_list_t *entry =
		    list_entry(list, drm_ttm_vma_list_t, head);
		drm_change_protection(entry->vma,
				      entry->vma->vm_start +
				      (page_offset << PAGE_SHIFT),
				      entry->vma->vm_start +
				      ((page_offset + num_pages) << PAGE_SHIFT),
				      entry->orig_protection, FALSE);
	}
}

static void set_vma_nocached(drm_ttm_t * ttm, unsigned long page_offset,
			     unsigned long num_pages)
{
	struct list_head *list;

	return;
	list_for_each(list, &ttm->vma_list->head) {
		drm_ttm_vma_list_t *entry =
		    list_entry(list, drm_ttm_vma_list_t, head);
		drm_change_protection(entry->vma,
				      entry->vma->vm_start +
				      (page_offset << PAGE_SHIFT),
				      entry->vma->vm_start +
				      ((page_offset + num_pages) << PAGE_SHIFT),
				      pgprot_noncached(entry->vma->
						       vm_page_prot), FALSE);
	}
}

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

void drm_unbind_ttm_region(drm_ttm_backend_list_t * entry)
{
	struct page **cur_page;
	int i, cur;
	drm_ttm_backend_t *be = entry->be;
	drm_ttm_t *ttm = entry->owner;

	list_del(&entry->head);

	if (be) {
		be->clear(entry->be);
		if (be->needs_cache_adjust(be)) {
		        down_write(&current->mm->mmap_sem);
			if (atomic_read(&ttm->vma_count) > 0)
				spin_lock(&current->mm->page_table_lock);
			unmap_vma_pages(ttm, entry->page_offset,
					entry->num_pages);
			for (i = 0; i < entry->num_pages; ++i) {
				cur = entry->page_offset + i;
				if (ttm->nocached[cur]) {
					cur_page = ttm->pages + cur;
					if (*cur_page &&
					    !PageHighMem(*cur_page)) {
						change_page_attr(*cur_page, 1,
								 PAGE_KERNEL);
					}
					ttm->nocached[cur] = FALSE;
				}
			}
			restore_vma_protection(ttm, entry->page_offset,
					       entry->num_pages);
			global_flush_tlb();
			if (atomic_read(&ttm->vma_count) > 0)
				spin_unlock(&current->mm->page_table_lock);
		        up_write(&current->mm->mmap_sem);
		}
		be->destroy(be);
	}
	drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
}

int drm_bind_ttm_region(drm_ttm_t * ttm, unsigned long page_offset,
			unsigned long n_pages, unsigned long aper_offset,
			drm_ttm_backend_list_t ** region)
{

	struct page **cur_page;
	drm_ttm_backend_list_t *entry;
	drm_ttm_backend_t *be;
	int ret, i;

	if ((page_offset + n_pages) > ttm->num_pages) {
		DRM_ERROR("Region Doesn't fit ttm\n");
		return -EINVAL;
	}

	/*
	 * FIXME: Check for overlapping regions.
	 */

	entry = drm_calloc(1, sizeof(*entry), DRM_MEM_MAPS);
	if (!entry)
		return -ENOMEM;

	be = ttm->dev->driver->create_ttm_backend_entry(ttm->dev);
	if (!be) {
		drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
		return -EINVAL;
	}
	entry->bound = FALSE;
	entry->page_offset = page_offset;
	entry->num_pages = n_pages;
	entry->be = be;
	entry->owner = ttm;

	INIT_LIST_HEAD(&entry->head);
	list_add(&entry->head, &ttm->be_list->head);

	for (i = 0; i < entry->num_pages; ++i) {
		cur_page = ttm->pages + (page_offset + i);
		if (!*cur_page) {
			*cur_page = alloc_page(GFP_USER);
			if (!*cur_page) {
				DRM_ERROR("Page allocation failed\n");
				drm_unbind_ttm_region(entry);
				return -ENOMEM;
			}
			SetPageLocked(*cur_page);
		}
	}

	if (be->needs_cache_adjust(be)) {
	        down_write(&current->mm->mmap_sem);
		spin_lock(&current->mm->page_table_lock);
		unmap_vma_pages(ttm, page_offset, n_pages);
		for (i = 0; i < entry->num_pages; ++i) {
			cur_page = ttm->pages + (page_offset + i);
			if (page_address(*cur_page) != NULL
			    && PageHighMem(*cur_page)) {
				DRM_ERROR("Illegal mapped HighMem Page\n");
				up_write(&current->mm->mmap_sem);
				spin_unlock(&current->mm->page_table_lock);
				drm_unbind_ttm_region(entry);
				return -EINVAL;
			}
			change_page_attr(*cur_page, 1, PAGE_KERNEL_NOCACHE);
			ttm->nocached[page_offset + i] = TRUE;
		}
		set_vma_nocached(ttm, page_offset, n_pages);
		global_flush_tlb();
		spin_unlock(&current->mm->page_table_lock);
	        up_write(&current->mm->mmap_sem);
	}
	if ((ret = be->populate(be, n_pages, ttm->pages + page_offset))) {
		drm_unbind_ttm_region(entry);
		return ret;
	}

	if ((ret = be->bind(be, aper_offset))) {
		drm_unbind_ttm_region(entry);
		return ret;
	}
	entry->bound = TRUE;
	*region = entry;

	return 0;
}

int drm_evict_ttm_region(drm_ttm_backend_list_t * entry)
{

	int ret;

	if (!entry || !entry->bound)
		return -EINVAL;

	if (0 != (ret = entry->be->unbind(entry->be))) {
		return ret;
	}

	entry->bound = FALSE;
	return 0;
}

int drm_rebind_ttm_region(drm_ttm_backend_list_t * entry,
			  unsigned long aper_offset)
{

	int ret;

	if (!entry || entry->bound)
		return -EINVAL;
	if (0 != (ret = entry->be->bind(entry->be, aper_offset))) {
		return ret;
	}
	entry->bound = TRUE;
	return 0;
}

void drm_user_unbind_region(drm_ttm_backend_list_t * entry)
{
	drm_ttm_backend_t *be;
	struct page **cur_page;
	int i;

	if (!entry || entry->owner)
		return;

	be = entry->be;
	if (!be) {
		drm_free(entry, sizeof(*entry), DRM_MEM_MAPS);
		return;
	}

	if (entry->bound) {
		be->unbind(be);
	}

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

int drm_user_bind_region(drm_device_t * dev, unsigned long start, int len,
			 unsigned long aper_offset,
			 drm_ttm_backend_list_t ** entry)
{
	drm_ttm_backend_list_t *tmp;
	drm_ttm_backend_t *be;
	int ret;

	if (len <= 0)
		return -EINVAL;
	if (!dev->driver->create_ttm_backend_entry)
		return -EFAULT;
	tmp = drm_calloc(1, sizeof(*tmp), DRM_MEM_MAPS);

	if (!tmp)
		return -ENOMEM;

	be = dev->driver->create_ttm_backend_entry(dev);
	tmp->be = be;

	if (!be) {
		drm_user_unbind_region(tmp);
		return -ENOMEM;
	}
	if (be->needs_cache_adjust(be)) {
		drm_user_unbind_region(tmp);
		return -EFAULT;
	}

	tmp->anon_pages = vmalloc(sizeof(*(tmp->anon_pages)) * len);

	if (!tmp->anon_pages) {
		drm_user_unbind_region(tmp);
		return -ENOMEM;
	}

	down_read(&current->mm->mmap_sem);
	ret = get_user_pages(current, current->mm, start, len, 1, 0,
			     tmp->anon_pages, NULL);
	up_read(&current->mm->mmap_sem);

	if (ret != len) {
		drm_user_unbind_region(tmp);
		DRM_ERROR("Could not lock %d pages. Return code was %d\n",
			  len,ret);
		return -EPERM;
	}
	tmp->anon_locked = len;

	ret = be->populate(be, len, tmp->anon_pages);

	if (!ret)
		ret = be->bind(be, aper_offset);

	if (ret) {
		drm_user_unbind_region(tmp);
		return ret;
	}

	tmp->bound = TRUE;
	*entry = tmp;

	return 0;
}

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

int drm_ttm_ioctl(DRM_IOCTL_ARGS)
{

	DRM_DEVICE;

	drm_map_list_t *map_list;
	drm_ttm_backend_list_t *entry;
	drm_map_t *map = NULL;
	void *hash_val;
	drm_ttm_t *ttm;
	drm_ttm_arg_t ttm_arg;
	int ret;
	unsigned long start;
	unsigned long end;
	int len;
	unsigned hash;

	DRM_COPY_FROM_USER_IOCTL(ttm_arg, (void __user *)data, sizeof(ttm_arg));

	switch (ttm_arg.op) {
	case ttm_add:
		down(&dev->struct_sem);
		ret = drm_add_ttm(dev, ttm_arg.size, &map_list);
		if (ret) {
			up(&dev->struct_sem);
			return ret;
		}
		list_add(&map_list->head, &priv->ttms);
		ttm = (drm_ttm_t *) map_list->map->offset;
		ttm->owner = priv;
		up(&dev->struct_sem);
		ttm_arg.handle = (uint32_t) map_list->user_token;
		break;
	case ttm_remove:
		down(&dev->struct_sem);
		if (drm_get_ht_val(&dev->maphash,
				   hash = (ttm_arg.handle -
					   DRM_MAP_HASH_OFFSET) >> PAGE_SHIFT,
				   &hash_val)) {
			up(&dev->struct_sem);
			return -EINVAL;
		}
		map_list = (drm_map_list_t *) hash_val;
		map = map_list->map;

		if (!map || map->type != _DRM_TTM) {
			DRM_ERROR("No ttm map found to remove\n");
			up(&dev->struct_sem);
			return -EINVAL;
		}
		ttm = (drm_ttm_t *) map->offset;
		if (ttm->owner != priv) {
			DRM_ERROR("Wrong ttm owner\n");
			up(&dev->struct_sem);
			return -EPERM;
		}
		drm_destroy_ttm((drm_ttm_t *) map->offset);
		list_del(&map_list->head);
		ret = drm_remove_ht_val(&dev->maphash, hash);
		up(&dev->struct_sem);
		if (!ret)
			drm_free(map, sizeof(*map), DRM_MEM_MAPS);
		drm_free(map_list, sizeof(*map_list), DRM_MEM_MAPS);
		return 0;
	case ttm_bind:
		LOCK_TEST_WITH_RETURN(dev, filp);

		down(&dev->struct_sem);
		if (drm_get_ht_val(&dev->maphash,
				   (ttm_arg.handle -
				    DRM_MAP_HASH_OFFSET) >> PAGE_SHIFT,
				   &hash_val)) {
			up(&dev->struct_sem);
			return -EINVAL;
		}
		map_list = (drm_map_list_t *) hash_val;
		map = map_list->map;
		if (!map || map->type != _DRM_TTM) {
			DRM_ERROR("No ttm map found to bind\n");
			up(&dev->struct_sem);
			return -EINVAL;
		}
		ttm = (drm_ttm_t *) map->offset;
		if (ttm->owner != priv) {
			DRM_ERROR("Wrong ttm owner\n");
			up(&dev->struct_sem);
			return -EINVAL;
		}

		if ((ret = drm_bind_ttm_region(ttm, ttm_arg.page_offset,
					       ttm_arg.num_pages,
					       ttm_arg.aper_offset, &entry))) {
			up(&dev->struct_sem);
			return ret;
		}
		if ((ret =
		     drm_insert_ht_val(&dev->ttmreghash, entry,
				       &ttm_arg.region))) {
			drm_unbind_ttm_region(entry);
			up(&dev->struct_sem);
			return ret;
		}
		up(&dev->struct_sem);
		break;
	case ttm_bind_user:

		LOCK_TEST_WITH_RETURN(dev, filp);

		end = (unsigned long)ttm_arg.addr + ttm_arg.size;
		start = ((unsigned long)ttm_arg.addr) & ~(PAGE_SIZE - 1);
		end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
		len = ((end - start) >> PAGE_SHIFT);
		if (len <= 0)
			return -EINVAL;

		down(&dev->struct_sem);
		ret =
		    drm_user_bind_region(dev, start, len, ttm_arg.aper_offset,
					 &entry);
		if (ret) {
			up(&dev->struct_sem);
			return ret;
		}
		entry->anon_owner = priv;
		if ((ret = drm_insert_ht_val(&dev->ttmreghash, entry,
					     &ttm_arg.region))) {
			drm_user_unbind_region(entry);
			up(&dev->struct_sem);
			return ret;
		}
		list_add(&entry->head, &priv->anon_ttm_regs);
		up(&dev->struct_sem);
		break;

	case ttm_unbind:
		LOCK_TEST_WITH_RETURN(dev, filp);

		down(&dev->struct_sem);

		if (drm_get_ht_val(&dev->ttmreghash, ttm_arg.region, &hash_val)) {
			up(&dev->struct_sem);
			return -EINVAL;
		}
		entry = (drm_ttm_backend_list_t *) hash_val;
		if (entry->owner) {
			if (entry->owner->owner == priv) {
				drm_remove_ht_val(&dev->ttmreghash,
						  ttm_arg.region);
				drm_unbind_ttm_region(entry);
			} else {
				up(&dev->struct_sem);
				return -EPERM;
			}
		} else {
			if (entry->anon_owner == priv) {
				list_del(&entry->head);
				drm_user_unbind_region(entry);
			} else {
				up(&dev->struct_sem);
				return -EPERM;
			}
		}
		up(&dev->struct_sem);

		return 0;
	case ttm_evict:
		LOCK_TEST_WITH_RETURN(dev, filp);

		down(&dev->struct_sem);
		if (drm_get_ht_val(&dev->ttmreghash, ttm_arg.region, &hash_val)) {
			up(&dev->struct_sem);
			return -EINVAL;
		}
		entry = (drm_ttm_backend_list_t *) hash_val;
		if ((ret = drm_evict_ttm_region(entry))) {
			up(&dev->struct_sem);
			return ret;
		}
		up(&dev->struct_sem);
		return 0;
	case ttm_rebind:
		LOCK_TEST_WITH_RETURN(dev, filp);

		down(&dev->struct_sem);
		if (drm_get_ht_val(&dev->ttmreghash, ttm_arg.region, &hash_val)) {
			up(&dev->struct_sem);
			return -EINVAL;
		}
		entry = (drm_ttm_backend_list_t *) hash_val;
		if ((ret = drm_rebind_ttm_region(entry, ttm_arg.aper_offset))) {
			up(&dev->struct_sem);
			return ret;
		}
		up(&dev->struct_sem);
		return 0;
	default:
		return -EINVAL;
	}

	DRM_COPY_TO_USER_IOCTL((void __user *)data, ttm_arg, sizeof(ttm_arg));
	return 0;
}
