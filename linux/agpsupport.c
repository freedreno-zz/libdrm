/* agpsupport.c -- DRM support for AGP/GART backend -*- linux-c -*-
 * Created: Mon Dec 13 09:56:45 1999 by faith@precisioninsight.com
 * Revised: Mon Dec 13 15:26:28 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * All Rights Reserved.
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

#define __NO_VERSION__
#include "drmP.h"

drm_agp_func_t drm_agp = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

typedef union {
	void          (*free_memory)(agp_memory *);
	agp_memory    *(*allocate_memory)(size_t, u32);
	int           (*bind_memory)(agp_memory *, off_t);
	int           (*unbind_memory)(agp_memory *);
	void          (*enable)(u32);
	int           (*acquire)(void);
	void          (*release)(void);
	void          (*copy_info)(agp_kern_info *);
	unsigned long address;
} drm_agp_func_u;

typedef struct drm_agp_fill {
        const char     *name;
	drm_agp_func_u *f;
} drm_agp_fill_t;

static drm_agp_fill_t drm_agp_fill[] = {
	{ "agp_free_memory",     (drm_agp_func_u *)&drm_agp.free_memory     },
	{ "agp_allocate_memory", (drm_agp_func_u *)&drm_agp.allocate_memory },
	{ "agp_bind_memory",     (drm_agp_func_u *)&drm_agp.bind_memory     },
	{ "agp_unbind_memory",   (drm_agp_func_u *)&drm_agp.unbind_memory   },
	{ "agp_enable",          (drm_agp_func_u *)&drm_agp.enable          },
	{ "agp_backend_acquire", (drm_agp_func_u *)&drm_agp.acquire         },
	{ "agp_backend_release", (drm_agp_func_u *)&drm_agp.release         },
	{ "agp_copy_info",       (drm_agp_func_u *)&drm_agp.copy_info       },
	{ NULL, NULL }
};

int drm_agp_free(agp_memory *handle)
{
	return 0;
}

agp_memory  *allocate_memory(size_t page_count, u32 type) { return NULL;    }
int         bind_memory(agp_memory *curr, off_t pg_start) { return -EINVAL; }
int         unbind_memory(agp_memory *curr)               { return -EINVAL; }
void        enable(u32 mode)                              { return;         }
int         acquire(void);
	void       (*release)(void);
	void       (*copy_info)(agp_kern_info *);

unsigned long drm_agp_alloc(unsigned long page_count)
{
	return 0;
}


drm_agp_info_t *drm_agp_init(void)
{
	drm_agp_fill_t *fill;
	drm_agp_info_t *info         = NULL;
	int            agp_available = 1;

	for (fill = &drm_agp_fill[0]; fill->name; fill++) {
		char *n  = (char *)fill->name;
		*fill->f = (drm_agp_func_u)get_module_symbol(NULL, n);
		DRM_DEBUG("%s resolves to 0x%08lx\n", n, (*fill->f).address);
		if (!(*fill->f).address) agp_available = 0;
	}

	if (agp_available) {
		if (!(info = drm_alloc(sizeof(*info), DRM_MEM_AGPLISTS)))
			return NULL;
		(*drm_agp.copy_info)(&info->agp_info);
		info->memory = NULL;
		switch (info->agp_info.chipset) {
		case INTEL_GENERIC:  info->chipset = "Intel";          break;
		case INTEL_LX:       info->chipset = "Intel 440LX";    break;
		case INTEL_BX:       info->chipset = "Intel 440BX";    break;
		case INTEL_GX:       info->chipset = "Intel 440GX";    break;
		case INTEL_I810:     info->chipset = "Intel i810";     break;
		case VIA_GENERIC:    info->chipset = "VIA";            break;
		case VIA_VP3:        info->chipset = "VIA VP3";        break;
		case VIA_MVP3:       info->chipset = "VIA MVP3";       break;
		case VIA_APOLLO_PRO: info->chipset = "VIA Apollo Pro"; break;
		case SIS_GENERIC:    info->chipset = "SiS";            break;
		case AMD_GENERIC:    info->chipset = "AMD";            break;
		case AMD_IRONGATE:   info->chipset = "AMD Irongate";   break;
		case ALI_GENERIC:    info->chipset = "ALi";            break;
		case ALI_M1541:      info->chipset = "ALi M1541";      break;
		default:
		}
		DRM_INFO("AGP %d.%d on %s @ 0x%08lx %dMB\n",
			 info->agp_info.version.major,
			 info->agp_info.version.minor,
			 info->chipset,
			 info->agp_info.aper_base,
			 info->agp_info.aper_size);
	}
	return info;
}
