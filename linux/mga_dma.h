#ifndef MGA_DMA_H
#define MGA_DMA_H

#include "mga_drm_public.h"


/* Isn't this fun - we copy the outstanding card state for every
 * MGA_DMA_VERTEX buffer.  This has to be fixed asap by emitting
 * primary dma commands in the 'vertex_dma' ioctl.  
 */
typedef struct {
	int dma_type;

   	unsigned int ContextState[MGA_CTX_SETUP_SIZE];
   	unsigned int ServerState[MGA_2D_SETUP_SIZE];
   	unsigned int TexState[2][MGA_TEX_SETUP_SIZE];
   	unsigned int WarpPipe;
   	unsigned int dirty;

	unsigned short clear_color;
	unsigned short clear_zval;
	unsigned int   clear_flags;

	unsigned int vertex_real_idx;
	unsigned int vertex_discard;

        unsigned int age;

   	unsigned int nbox;
   	xf86drmClipRectRec boxes[MGA_NR_SAREA_CLIPRECTS];
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



#define MGA_VERBOSE 1



/* Macros for inserting commands into a secondary dma buffer.
 */
#define DMALOCALS	u8 tempIndex[4]; u32 *dma_ptr; \
			int outcount, num_dwords;

#define	DMAGETPTR(buf) do {				\
  dma_ptr = (u32 *)((u8 *)buf->address + buf->used);	\
  outcount = 0;						\
  num_dwords = buf->used / 4;				\
  if (MGA_VERBOSE)					\
	printk(KERN_INFO "DMAGETPTR in %s, start %d\n",	\
	       __FUNCTION__, num_dwords);		\
} while(0)

#define DMAADVANCE(buf)	do {			\
  if (MGA_VERBOSE)				\
	printk(KERN_INFO "DMAADVANCE\n");	\
  buf->used = num_dwords * 4;			\
} while(0)

#define DMAOUTREG(reg, val) do {				\
  tempIndex[outcount]=ADRINDEX(reg);				\
  dma_ptr[++outcount] = val;					\
  if (MGA_VERBOSE)						\
	printk(KERN_INFO					\
	       "   DMAOUT %d: 0x%x -- 0x%x\n",			\
	       num_dwords +1+outcount, ADRINDEX(reg), val);	\
  if (outcount == 4) {						\
     outcount = 0;						\
     dma_ptr[0] = *(u32 *)tempIndex;				\
     dma_ptr+=5;						\
     num_dwords += 5;						\
  }								\
}while (0)



/* Primary buffer versions of above -- pretty similar really.
 */
#define PRIMLOCALS	u8 tempIndex[4]; u32 *dma_ptr; u32 phys_head; \
			int outcount, num_dwords

#define PRIMRESET(dev_priv) do {				\
	dev_priv->prim_num_dwords = 0;				\
   	dev_priv->current_dma_ptr = dev_priv->prim_head;	\
} while (0)
	
#define	PRIMGETPTR(dev_priv) do {					\
	dma_ptr = dev_priv->current_dma_ptr;				\
	phys_head = dev_priv->prim_phys_head;				\
	num_dwords = dev_priv->prim_num_dwords;				\
	outcount = 0;							\
	if (MGA_VERBOSE)						\
		printk(KERN_INFO "PRIMGETPTR in %s, start %d\n",	\
		       __FUNCTION__, num_dwords);			\
} while (0)

#define PRIMADVANCEPAD(dev_priv)	do {		\
        while(outcount & 3) {				\
	    if (MGA_VERBOSE)				\
      	        printk(KERN_INFO "PAD %d\n",		\
                       num_dwords + 1 + outcount);	\
	    tempIndex[outcount++]=0x15;			\
        }						\
							\
	if (MGA_VERBOSE)				\
		printk(KERN_INFO "PRIMADVANCEPAD\n");	\
	dev_priv->prim_num_dwords = num_dwords;		\
	dev_priv->current_dma_ptr = dma_ptr;		\
} while (0)

#define PRIMADVANCE(dev_priv)	do {				\
	if (MGA_VERBOSE) {					\
		printk(KERN_INFO "PRIMADVANCE\n");		\
                if (outcount & 3)				\
                      printk(KERN_INFO " --- truncation\n");	\
        }							\
	dev_priv->prim_num_dwords = num_dwords;			\
	dev_priv->current_dma_ptr = dma_ptr;			\
} while (0)


#define PRIMOUTREG(reg, val) do {					\
	tempIndex[outcount]=ADRINDEX(reg);				\
	dma_ptr[1+outcount] = val;					\
	if (MGA_VERBOSE)						\
		printk(KERN_INFO					\
		       "   PRIMOUT %d: 0x%x -- 0x%x\n",			\
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

#define MGA_ILOAD_CMD (DC_opcod_iload | DC_atype_rpl |          	\
		       DC_linear_linear | DC_bltmod_bfcol |       	\
		       (0xC << DC_bop_SHIFT) | DC_sgnzero_enable |	\
		       DC_shftzero_enable | DC_clipdis_enable)


#endif
