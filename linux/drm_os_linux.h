#define __NO_VERSION__


#include <linux/interrupt.h>	/* For task queue support */
#include <linux/delay.h>

#define DRM_OS_IOCTL		struct inode *inode, struct file *filp, unsigned int cmd, unsigned long data
#define DRM_OS_RETURN(d)	return -(d)
#define DRM_OS_ERR(d)		-(d)
#define DRM_OS_CURRENTPID	current->pid
#define DRM_OS_DELAY(d)		udelay(d)
#define DRM_OS_READ8(addr)		readb(addr)
#define DRM_OS_READ32(addr)		readl(addr)
#define DRM_OS_WRITE8(addr, val)	writeb(val, addr)
#define DRM_OS_WRITE32(addr, val)	writel(val, addr)
#define DRM_OS_READMEMORYBARRIER()	mb()
#define DRM_OS_WRITEMEMORYBARRIER()	wmb()
#define DRM_OS_DEVICE	drm_file_t	*priv	= filp->private_data; \
			drm_device_t	*dev	= priv->dev

/* For data going from/to the kernel through the ioctl argument */
#define DRM_OS_KRNFROMUSR(arg1, arg2, arg3) \
	if ( copy_from_user(&arg1, arg2, arg3) ) \
		return -EFAULT
#define DRM_OS_KRNTOUSR(arg1, arg2, arg3) \
	if ( copy_to_user(arg1, &arg2, arg3) ) \
		return -EFAULT
/* Other copying of data from/to kernel space */
#define DRM_OS_COPYFROMUSR(arg1, arg2, arg3) \
	copy_from_user(arg1, arg2, arg3)
#define DRM_OS_COPYTOUSR(arg1, arg2, arg3) \
	copy_to_user(arg1, arg2, arg3)
/* Macros for checking readability once */
#define DRM_OS_VERIFYAREA_READ( uaddr, size ) 		\
	verify_area( VERIFY_READ, uaddr, size )
#define DRM_OS_COPYFROMUSR_NC(arg1, arg2, arg3) 	\
	__copy_from_user(arg1, arg2, arg3)
#define DRM_OS_FETCHU_32_NC(val, uaddr)			\
	__get_user(val, uaddr)


/* malloc/free without the overhead of DRM(alloc) */
#define DRM_OS_MALLOC(x) kmalloc(x, GFP_KERNEL)
#define DRM_OS_FREE(x) kfree(x)

#define DRM_OS_GETSAREA()							 \
do { 										 \
	struct list_head *list;							 \
	list_for_each( list, &dev->maplist->head ) {				 \
		drm_map_list_t *entry = (drm_map_list_t *)list;			 \
		if ( entry->map &&						 \
		     entry->map->type == _DRM_SHM &&				 \
		     (entry->map->flags & _DRM_CONTAINS_LOCK) ) {		 \
			dev_priv->sarea = entry->map;				 \
 			break;							 \
 		}								 \
 	}									 \
} while (0)

