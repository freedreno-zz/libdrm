#ifndef MGA_DMA_H
#define MGA_DMA_H

#include "mga_drm_public.h"
typedef enum {
	TT_GENERAL,
	TT_BLIT,
	TT_VECTOR,
	TT_VERTEX
} transferType_t;

typedef struct {
   	drm_mga_freelist_t *my_freelist;
} drm_mga_buf_priv_t;

#define MGA_DMA_GENERAL 0	/* not used */
#define MGA_DMA_VERTEX  1
#define MGA_DMA_SETUP   2
#define MGA_DMA_ILOAD   3
#define MGA_DMA_CLEAR   4	/* placeholder */
#define MGA_DMA_SWAP    5	/* placeholder */
#define MGA_DMA_DISCARD 6

#define DWGREG0 	0x1c00
#define DWGREG0_END 	0x1dff
#define DWGREG1		0x2c00
#define DWGREG1_END	0x2dff

#define ISREG0(r)	(r >= DWGREG0 && r <= DWGREG0_END)
#define ADRINDEX0(r)	(u8)((r - DWGREG0) >> 2)
#define ADRINDEX1(r)	(u8)(((r - DWGREG1) >> 2) | 0x80)
#define ADRINDEX(r)	(ISREG0(r) ? ADRINDEX0(r) : ADRINDEX1(r)) 

#define MGA_VERBOSE 0

#define MGA_NUM_PRIM_BUFS 	8
/* Primary buffer versions of above -- pretty similar really.
 */

#define PRIMLOCALS	u8 tempIndex[4]; u32 *dma_ptr; u32 phys_head; \
			int outcount, num_dwords

#define PRIM_OVERFLOW(dev, dev_priv, length) do {      			\
drm_mga_prim_buf_t *tmp_buf = 						\
	dev_priv->prim_bufs[dev_priv->current_prim_idx];		\
if( (tmp_buf->max_dwords - tmp_buf->num_dwords) < length ||    		\
tmp_buf->sec_used > (MGA_DMA_BUF_NR / 2)) {				\
	atomic_set(&tmp_buf->force_fire, 1);				\
	mga_advance_primary(dev);					\
	mga_dma_schedule(dev, 1);					\
   } else if( atomic_read(&tmp_buf->needs_overflow)) {			\
	mga_advance_primary(dev);					\
	mga_dma_schedule(dev, 1);					\
}									\
} while(0)

#define PRIMGETPTR(dev_priv) do {					\
drm_mga_prim_buf_t *tmp_buf = 						\
	dev_priv->prim_bufs[dev_priv->current_prim_idx];		\
if(MGA_VERBOSE) \
DRM_DEBUG("PRIMGETPTR in %s\n", __FUNCTION__); \
dma_ptr = tmp_buf->current_dma_ptr;    					\
num_dwords = tmp_buf->num_dwords;      					\
phys_head = tmp_buf->phys_head;						\
outcount = 0;								\
} while(0)

#define PRIMPTR(prim_buf) do {					\
if(MGA_VERBOSE) \
DRM_DEBUG("PRIMPTR in %s\n", __FUNCTION__); \
dma_ptr = prim_buf->current_dma_ptr;				\
num_dwords = prim_buf->num_dwords;				\
phys_head = prim_buf->phys_head;       				\
outcount = 0;							\
} while(0)

#define PRIMFINISH(prim_buf) do {				\
	if (MGA_VERBOSE) {					\
		DRM_DEBUG( "PRIMFINISH in %s\n", __FUNCTION__);	\
                if (outcount & 3)				\
                      DRM_DEBUG(" --- truncation\n");	        \
        }							\
	prim_buf->num_dwords = num_dwords;			\
	prim_buf->current_dma_ptr = dma_ptr;			\
} while(0)

#define PRIMADVANCE(dev_priv)	do {				\
drm_mga_prim_buf_t *tmp_buf = 					\
	dev_priv->prim_bufs[dev_priv->current_prim_idx];	\
	if (MGA_VERBOSE) {					\
		DRM_DEBUG("PRIMADVANCE in %s\n", __FUNCTION__);	\
                if (outcount & 3)				\
                      DRM_DEBUG(" --- truncation\n");	\
        }							\
	tmp_buf->num_dwords = num_dwords;      			\
	tmp_buf->current_dma_ptr = dma_ptr;    			\
} while (0)

#define PRIMUPDATE(dev_priv)	do {				\
drm_mga_prim_buf_t *tmp_buf = 					\
	dev_priv->prim_bufs[dev_priv->current_prim_idx];	\
	tmp_buf->sec_used++;					\
} while (0)

#define PRIMOUTREG(reg, val) do {					\
	tempIndex[outcount]=ADRINDEX(reg);				\
	dma_ptr[1+outcount] = val;					\
	if (MGA_VERBOSE)						\
		DRM_DEBUG("   PRIMOUT %d: 0x%x -- 0x%x\n",		\
		       num_dwords + 1 + outcount, ADRINDEX(reg), val);	\
	if( ++outcount == 4) {						\
		outcount = 0;						\
		dma_ptr[0] = *(u32 *)tempIndex;				\
		dma_ptr+=5;						\
		num_dwords += 5;					\
	}								\
}while (0)

#define MGA_CLEAR_CMD (DC_opcod_trap | DC_arzero_enable | 		\
		       DC_sgnzero_enable | DC_shftzero_enable | 	\
		       (0xC << DC_bop_SHIFT) | DC_clipdis_enable | 	\
		       DC_solid_enable | DC_transc_enable)
	  

#define MGA_COPY_CMD (DC_opcod_bitblt | DC_atype_rpl | DC_linear_xy |	\
		      DC_solid_disable | DC_arzero_disable | 		\
		      DC_sgnzero_enable | DC_shftzero_enable | 		\
		      (0xC << DC_bop_SHIFT) | DC_bltmod_bfcol | 	\
		      DC_pattern_disable | DC_transc_disable | 		\
		      DC_clipdis_enable)				\

#endif
