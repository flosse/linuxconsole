/*
 *  Generic fillrect for frame buffers with packed pixels of any depth. 
 *
 *      Copyright (C)  2000 James Simmons (jsimmons@linux-fbdev.org) 
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

void cfb_fillrect(struct fb_info *p, int x1, int y1, unsigned int width,  
		  unsigned int rows, unsigned long color, int rop) 
{
  unsigned long start_index, end_index, start_mask = 0, end_mask = 0;
  unsigned long height, ppw, fg;
  int i, n, x2, y2, linesize = p->fix.line_length;
  int bpl = sizeof(unsigned long);	
  unsigned long *dst;
  char *dst1;	

  if (!width || !rows) return;	
 
  /* We could use hardware clipping but on many cards you get around hardware
     clipping by writing to framebuffer directly. */
  x2 = x1 + width;
  y2 = y1 + rows;
  x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
  y2 = y2 < p->var.yres_virtual ? y2 : p->var.yres_virtual;
  width = x2 - x1;
  height = y2 - y1;

  /* Size of the scanline in bytes */ 	
  n = ((width * p->var.bits_per_pixel) >> 3);	
  ppw = BITS_PER_LONG/p->var.bits_per_pixel;

  dst1 = p->screen_base + y1 * linesize + ((x1 * p->var.bits_per_pixel) >> 3); 
  start_index = ((unsigned long) dst1 & (bpl-1));
  end_index = ((unsigned long)(dst1 + n) & (bpl-1));	

 // printk("start_index is %ld\n", start_index);
 // printk("end_index is %ld\n", end_index);	
 // printk("width is %d\n", width);	

  fg = color;
  
  for (i = 0; i < ppw; i++) {
    fg <<= p->var.bits_per_pixel;
    fg |= color;
  }

  if (start_index) {
  	start_mask = fg << (start_index << 3);
  	n -= (bpl - start_index);
  }
  
  if (end_index) {
  	end_mask = fg >> ((bpl - end_index) << 3);
  	n -= end_index;
  }

  n = n/bpl;

  if (n <= 0) {
	if (start_mask) {
		if (end_mask) 
			end_mask &= start_mask;
		else 
			end_mask = start_mask;
		start_mask = 0;
    	}
    	n = 0;	
  }

 // printk("start_mask is %ld\n", start_mask);
//  printk("end_mask is %ld\n", end_mask);
//  printk("n is %d\n", n); 	

  if ((BITS_PER_LONG % p->var.bits_per_pixel) == 0) {
    switch(rop) {
    case ROP_COPY:        
      do {
	/* Word align to increases performace :-) */
	dst = (unsigned long *) (dst1 - start_index);
	
	if (start_mask) {
#if BITS_PER_LONG == 32
		fb_writel(fb_readl(dst) | start_mask, dst);
#else
		fb_writeq(fb_readq(dst) | start_mask, dst);
#endif
		dst++;
	}
  	
	for(i=0;i<n;i++) {
#if BITS_PER_LONG == 32
	  	fb_writel(fg, dst);
#else
		fb_writeq(fg, dst);
#endif
	  	dst++;			
	}

	if (end_mask)
#if BITS_PER_LONG == 32
		fb_writel(fb_readl(dst) | end_mask, dst);
#else
		fb_writeq(fb_readq(dst) | end_mask, dst);
#endif
	dst1+=linesize;
      } while (--height);
      break;
    case ROP_XOR:
      do {
	dst = (unsigned long *) (dst1 - start_index);
      
#if BITS_PER_LONG == 32
	fb_writel(fb_readl(dst) ^ start_mask, dst);
#else
	fb_writeq(fb_readq(dst) ^ start_mask, dst);
#endif
	for(i=0;i<n;i++) {
	  	dst++;
#if BITS_PER_LONG == 32
	  	fb_writel(fb_readl(dst) ^ fg, dst); 
#else
	  	fb_writeq(fb_readq(dst) ^ fg, dst); 
#endif
	}
	
	if (end_mask) {
	  	dst++;
#if BITS_PER_LONG == 32
	  	fb_writel(fb_readl(dst) ^ end_mask, dst);
#else
	  	fb_writeq(fb_readq(dst) ^ end_mask, dst);
#endif
	}
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
    case ROP_COPY:        
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
    case ROP_XOR:
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
  return;	
}
