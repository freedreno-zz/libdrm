#include "drmP.h"
#include <linux/hash.h>

int drm_create_hashtab(drm_closedhash_t * ht, unsigned int order)
{
	ht->size = 1 << order;
	ht->order = order;
	ht->fill = 0;
	ht->table = drm_calloc(ht->size, sizeof(void *), DRM_MEM_MAPPINGS);
	return (NULL == ht->table);
}

int drm_insert_ht_val(drm_closedhash_t * ht, void *item, unsigned int *hash)
{

	unsigned i, tmp_hash;

	if (!item) {
		DRM_ERROR("Trying to hash a NULL pointer.\n");
		return DRM_ERR(EINVAL);
	}

	if (ht->fill >= ht->size) {
		DRM_ERROR("Hash table full.\n");
		return DRM_ERR(ENOMEM);
	}

	tmp_hash = hash_ptr(item, ht->order);
	for (i = 0; i < ht->size; ++i) {
		if (!ht->table[tmp_hash]) {
			ht->table[tmp_hash] = item;
			ht->fill++;
			break;
		}
		if (++tmp_hash >= ht->size)
			tmp_hash = 0;
	}
	BUG_ON(i == ht->size);

	*hash = tmp_hash;
	return 0;
}

int drm_get_ht_val(drm_closedhash_t * ht, unsigned int hash, void **item)
{

	if (hash >= ht->size || !ht->table[hash]) {
		DRM_ERROR("Illegal hash value\n");
		return DRM_ERR(EINVAL);
	}
	*item = ht->table[hash];
	return 0;
}

int drm_find_ht_item(drm_closedhash_t * ht, void *item, unsigned int *hash)
{
	unsigned i, tmp_hash;
	void **cur_item;

	if (!item) {
		DRM_ERROR("Trying to hash a NULL pointer.\n");
		return DRM_ERR(EINVAL);
	}

	cur_item = ht->table + (tmp_hash = hash_ptr(item, ht->order));

	for (i = 0; i < ht->size; ++i) {
		if (!*cur_item) {
			return DRM_ERR(EINVAL);
		} else if (*cur_item == item) {
			break;
		}
		++cur_item;
		if (++tmp_hash >= ht->size) {
			cur_item = ht->table;
			tmp_hash = 0;
		}
	}
	if (i == ht->size) {
		return DRM_ERR(EINVAL);
	}

	*hash = tmp_hash;
	return 0;
}

int drm_remove_ht_val(drm_closedhash_t * ht, unsigned int hash)
{

	if (hash >= ht->size) {
		DRM_ERROR("Illegal hash value\n");
		return DRM_ERR(EINVAL);
	}
	ht->table[hash] = NULL;
	ht->fill = 0;
	return 0;
}

void drm_remove_hashtab(drm_closedhash_t * ht)
{
	if (ht->table)
		drm_free(ht->table, ht->size * sizeof(void *),
			 DRM_MEM_MAPPINGS);
}
