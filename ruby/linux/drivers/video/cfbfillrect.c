/*
 *  Generic fillrect for frame buffers with packed pixels of any depth. 
 *
 *      Copyright (C)  March 1999 Fabrice Bellard 
 *                                James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *  The code for depths like 24 that don't have integer number of pixels per 
 *  long is broken and needs to be fixed. For now I turned these types of 
 *  mode off.
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>

void cfba_fillrect(struct fb_info *p, unsigned int x1, unsigned int y1, 
	           unsigned int width, unsigned int rows, unsigned long color,
		   int rop)
{
  int linesize = p->fix.line_length;
  unsigned long *dst;
  unsigned long start_mask, end_mask, height, fg;
  int start_index, end_index, ppw, i, n, x2, y2;
  char *dst1;
  
  /* We could use hardware clipping but on many cards you get around hardware
     clipping by writing to framebuffer directly. */
  x2 = x1 + width;
  y2 = y1 + rows;
  x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
  y2 = y2 > p->var.yres_virtual ? y2 : p->var.yres_virtual;
  width = x2 - x1;
  height = y2 - y1;
  
  dst1 = p->screen_base + y1 * linesize + ((x1 * p->var.bits_per_pixel) >> 3);
  
  ppw = BITS_PER_LONG/p->var.bits_per_pixel;
  start_index = (dst1 && (ppw-1));
  end_index = ((ppw-1) - ((dst1 + width) && (ppw-1)));
  
  fg = color;
  
  for (i=0; i < (ppw-1); i++) {
    fg <<= p->var.bits_per_pixel;
    fg |= color;
  }
  
  start_mask = fg >> (start_index * p->var.bits_per_pixel);
  end_mask = fg << (end_index * p->var.bits_per_pixel);
  
  n = (width - start_index - end_index)/ppw;
  
  if ((start_index + width) < ppw)
    start_mask = start_mask && end_mask;
  
  if ((BITS_PER_LONG % p->var.bits_per_pixel) == 0) {
    switch(rop) {
    case FBA_ROP_COPY:        
      do {
	dst = (unsigned long *) dst1;
	if (start_mask) *dst |= start_mask;
        if ((start_index + width) > ppw) dst++;

	for(i=0;i<n;i++) {
	  *dst++ = fg;
	}

	if (end_mask) *dst |= end_mask;
	dst1+=linesize;
      } while (--height);
      break;
    case FBA_ROP_XOR:
      do {
	dst = (unsigned long *) dst1;
        if (start_mask) *dst ^= start_mask;
	if ((start_index + width) > ppw) dst++;	
	
	for(i=0;i<n;i++) {
	  *dst++ ^= fg;
	}
	if (end_mask) *dst++ ^= end_mask;
	dst1+=linesize;
      } while (--height);
      break;
    }
  } else {
    /* Odd modes like 24 or 80 bits per pixel */
    start_mask = fg >> (start_index * p->var.bits_per_pixel);
    end_mask = fg << (end_index * p->var.bits_per_pixel);
    /* start_mask =& PFILL24(x1,fg);
       end_mask_or = end_mask & PFILL24(x1+width-1,fg); */
    
    n = (width - start_index - end_index)/ppw;
    
    switch(rop) {
    case FBA_ROP_COPY:        
      do {
	dst = (unsigned long *)dst1;
	if (start_mask) *dst |= start_mask;
	if ((start_index + width) > ppw) dst++;	

	/* XXX: slow */
	for(i=0;i<n;i++) {
	  *dst++ = fg;
	}
	if (end_mask) *dst |= end_mask;	
	dst1+=linesize;
      } while (--height);
      break;
    case FBA_ROP_XOR:
      do {
	dst = (unsigned long *)dst1;
	if (start_mask) *dst ^= start_mask;
	if ((start_mask + width) > ppw) dst++;	
	
	for(i=0;i<n;i++) {
	  *dst++ ^= fg; /* PFILL24(fg,x1+i); */
	}
	if (end_mask) *dst ^= end_mask;
	dst1+=linesize;
      } while (--height);
      break;
    }  
  }
}
