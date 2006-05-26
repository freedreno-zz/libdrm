/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Steamboat Springs, CO. USA.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE 
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * 
 **************************************************************************/
/*
 * Simple open hash tab implementation.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include "drmP.h"
#include "drm_hashtab.h"
#include <linux/hash.h>

int
drm_ht_create(drm_open_hash_t *ht, unsigned int order)
{
        unsigned int i;

        ht->size = 1 << order;
        ht->order = order;
        ht->fill = 0;
        ht->table = vmalloc(ht->size*sizeof(*ht->table));
        if (!ht->table) {
                DRM_ERROR("Out of memory for hash table\n");
                return -ENOMEM;
        }
        for (i=0; i< ht->size; ++i) {
                INIT_LIST_HEAD(&ht->table[i]);
        }
        return 0;
}

void
drm_ht_verbose_list(drm_open_hash_t *ht, unsigned long key)
{
        drm_hash_item_t *entry;
        struct list_head *list, *h_list;
        unsigned int hashed_key;
        int count = 0;

        hashed_key = hash_long(key, ht->order);
        DRM_DEBUG("Key is 0x%08lx, Hashed key is 0x%08x\n", key, hashed_key);
        h_list = &ht->table[hashed_key];
        list_for_each(list, h_list) {
                entry = list_entry(list, drm_hash_item_t, head);
                DRM_DEBUG("count %d, key: 0x%08lx\n", count++, entry->key);
        }
}


static struct list_head
*drm_ht_find_key(drm_open_hash_t *ht, unsigned long key, int *found)
{
        drm_hash_item_t *entry;
        struct list_head *list, *h_list, *ret;
        unsigned int hashed_key;

        hashed_key = hash_long(key, ht->order);
        
        *found = FALSE;
        h_list = &ht->table[hashed_key];
        ret = h_list;
        list_for_each(list, h_list) {
                entry = list_entry(list, drm_hash_item_t, head);
                if (entry->key == key) {
                        ret = list;
                        *found = TRUE;
                        break;
                }
                if (entry->key > key) {
                        ret = list;
                        break;
                }
        }
        return ret;
}

int
drm_ht_insert_item(drm_open_hash_t *ht, drm_hash_item_t *item)
{
        int found;
        struct list_head *list;
        
        list = drm_ht_find_key(ht, item->key, &found);
        if (found) {
                return -EINVAL;
        } else {
                list_add_tail(&item->head, list);
                ht->fill++;
        }
        return 0;
}

/*
 * Just insert an item and return any "bits" bit key that hasn't been used before.
 */

int
drm_ht_just_insert_please(drm_open_hash_t *ht, drm_hash_item_t *item,
                          unsigned long seed, int bits)
{
        int ret;
        unsigned long mask = (1 << bits) - 1;
        unsigned long first;

        item->key = hash_long(seed, bits);
        first = item->key;
        do{
                ret = drm_ht_insert_item(ht, item);
                if (ret)
                        item->key = item->key++ & mask; 
        } while(ret && (item->key != first));

        if (ret) {
                DRM_ERROR("Available key bit space exhausted\n");
                return -EINVAL;
        }
        return 0;
}
        
int
drm_ht_find_item(drm_open_hash_t *ht, unsigned long key, drm_hash_item_t **item) 
{
        int found;
        struct list_head *list;

        list = drm_ht_find_key(ht, key, &found);
        if (!found) {
                return -1;
        } else {
                *item = list_entry(list, drm_hash_item_t, head);
                return 0;
        }
}
             
int 
drm_ht_remove_key(drm_open_hash_t *ht, unsigned long key)
{
        int found;
        struct list_head *list;
        
        list = drm_ht_find_key(ht, key, &found);
        if (found) {
                list_del_init(list);
                ht->fill--;
                return 0;
        }
        return -1;
}

int 
drm_ht_remove_item(drm_open_hash_t *ht, drm_hash_item_t *item)
{
        list_del_init(&item->head);
        ht->fill--;
        return 0;
}

void drm_ht_remove(drm_open_hash_t *ht)
{
        if (ht->table) {
                vfree(ht->table);
                ht->table = NULL;
        }
} 

