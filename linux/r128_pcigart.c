/* r128_pcigart.c -- Rage 128 PCI GART support -*- linux-c -*-
 * Created: Wed Dec 13 21:52:19 2000 by gareth@valinux.com
 *
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
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
 * Authors:
 *   Gareth Hughes <gareth@valinux.com>
 *
 */

#define __NO_VERSION__
#include "drmP.h"
#include "r128_drv.h"

#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>




static unsigned long r128_alloc_pages( void )
{
	unsigned long address;
	unsigned long addr_end;
	struct page *page;

	DRM_INFO( "%s\n", __FUNCTION__ );

	address = __get_free_pages( GFP_KERNEL, 3 );
	if ( address == 0UL ) {
		return 0;
	}

	addr_end = address + ((PAGE_SIZE * (1 << 3)) - 1);
	for (page = virt_to_page(address); 
	     page <= virt_to_page(addr_end);
	     page++) {
		atomic_inc( &page->count );
		SetPageReserved( page );
	}

	DRM_INFO( "%s: returning 0x%08lx\n", __FUNCTION__, address );
	return address;
}

static void r128_free_pages( unsigned long address )
{
	unsigned long addr_end;
	struct page *page;

	DRM_INFO( "%s\n", __FUNCTION__ );
	if ( !address ) return;

	addr_end = address + ((PAGE_SIZE * (1 << 3)) - 1);

	for (page = virt_to_page(address); 
	     page <= virt_to_page(addr_end);
	     page++) {
		atomic_dec( &page->count );
		ClearPageReserved( page );
	}

	free_pages( address , 3 );
}

int r128_pcigart_init( drm_device_t *dev )
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	drm_sg_mem_t *entry = dev->sg;
	unsigned long address;
	unsigned long pages;
	unsigned long *pci_gart;
	int i;
	DRM_INFO( "%s\n", __FUNCTION__ );

#if 0
	dev_priv->phys_pci_gart_page = 0;
	dev_priv->pci_gart_page = NULL;

	return 0;
#endif

	if ( !entry ) {
		DRM_ERROR( "no scatter/gather memory!\n" );
		return -EINVAL;
	}

	/* 32 MB aperture is the largest size */
	pages = ( entry->pages <= 8192 )
		? entry->pages : 8192;

	address = r128_alloc_pages();

	
	if ( !address ) {
		DRM_ERROR( "cannot allocate PCI GART page!\n" );
		return -ENOMEM;
	}

	dev_priv->phys_pci_gart_page = address;
	dev_priv->pci_gart_page = (unsigned long *)address;

	DRM_INFO( "%s: phys=0x%08lx virt=%p\n",
		  __FUNCTION__, dev_priv->phys_pci_gart_page,
		  dev_priv->pci_gart_page );

	pci_gart = (unsigned long *)dev_priv->pci_gart_page;

	for ( i = 0; i < 8192 ; i++) pci_gart[i] = 0;
	for ( i = 0 ; i < pages ; i++ ) {
		pci_gart[i] = virt_to_bus( entry->pagelist[i]->virtual );
	}

	DRM_INFO( "%s: writing PCI_GART_PAGE...\n", __FUNCTION__ );
	R128_WRITE( R128_PCI_GART_PAGE, virt_to_bus((void *)address) );
	DRM_INFO( "%s: writing PCI_GART_PAGE... done.\n", __FUNCTION__ );

	asm volatile ( "wbinvd" ::: "memory" );

	return 0;
}

int r128_pcigart_cleanup( drm_device_t *dev )
{
	drm_r128_private_t *dev_priv = dev->dev_private;
	DRM_INFO( "%s\n", __FUNCTION__ );
#if 0
	if ( dev_priv->pci_gart_page ) {
		iounmap( dev_priv->pci_gart_page );
	}
#endif
	if ( dev_priv->phys_pci_gart_page ) {
		r128_free_pages( dev_priv->phys_pci_gart_page );
	}

	return 0;
}
