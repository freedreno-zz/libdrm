#ifndef MGA_DMA_H
#define MGA_DMA_H


#define DWGREG0 	0x1c00
#define DWGREG0_END 	0x1dff
#define DWGREG1		0x2c00
#define DWGREG1_END	0x2dff

#define ISREG0(r)	(r >= DWGREG0 && r <= DWGREG0_END)
#define ADRINDEX0(r)	(u8)((r - DWGREG0) >> 2)
#define ADRINDEX1(r)	(u8)(((r - DWGREG1) >> 2) | 0x80)
#define ADRINDEX(r)	(ISREG0(r) ? ADRINDEX0(r) : ADRINDEX1(r)) 


/* Macros for inserting commands into a secondary dma buffer.
 */

#define DMALOCALS	u8 tempIndex[4]; u32 *dma_ptr; \
			int outcount, num_dwords;

#define	DMAGETPTR(buf) do {			\
  dma_ptr = (u32 *)((u8 *)buf->address + buf->used);	\
  outcount = 0;					\
  num_dwords = buf->used / 4;			\
} while(0)

#define DMAADVANCE(buf)	do {			\
  buf->used = num_dwords * 4;			\
} while(0)

#define DMAOUTREG(reg, val) do {		\
  tempIndex[outcount]=ADRINDEX(reg);		\
  dma_ptr[++outcount] = val;			\
  if (outcount == 4) {				\
     outcount = 0;				\
     dma_ptr[0] = *(u32 *)tempIndex;		\
     dma_ptr+=5;				\
     num_dwords += 5;				\
  }						\
}while (0)

/* Not used for emitting state, as we assume the state will always fit 
 * in a single buffer.  
 *
 * For clip/swap -- choose a maximum number of cliprects so that a single
 * buffer is always sufficient?
 *
 * For vertex cliprects -- ???
 */
#define CHECK_OVERFLOW(length) do {		\
   if (buf->total - buf->used < length * 4)	\
	mga_prim_overflow(dev);			\
}while(0)


#endif
