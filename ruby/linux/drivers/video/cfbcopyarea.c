/*
 *  Generic function for frame buffer with packed pixels of any depth.
 *
 *      Copyright (C)  June 1999 James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 * 
 *  This is for cfb packed pixels. Iplan and such are incorporated in the 
 *  drivers that need them.
 * 
 *  FIXME
 *  The code for 24 bit is horrible. It copies byte by byte size instead of 
 *  longs like the other sizes. Needs to be optimized. 
 *
 *  Also need to add code to deal with cards endians that are different than 
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *  
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/slab.h>
#include <asm/types.h>
#include <asm/io.h>

void cfb_copyarea(struct fb_info *p, int sx, int sy, unsigned int width, 
		   unsigned int rows, int dx, int dy)
{
  unsigned long start_index, end_index, start_mask, end_mask, last,tmp, height;
  int lineincr, shift, shift_right, shift_left, old_dx,old_dy;
  int x2, y2, n, j; 
  int linesize = p->fix.line_length;
#if defined(__sparc64__) || defined(__alpha__)
  u64 *dst = NULL, *src = NULL;
#else
  u32 *dst = NULL, *src = NULL;
#endif
  char *src1,*dst1;
  
  /* clip the destination */
  old_dx=dx;
  old_dy=dy;
  
  /* We could use hardware clipping but on many cards you get around hardware
     clipping by writing to framebuffer directly. */
  x2 = dx + width;
  y2 = dy + rows;
  dx = dx > 0 ? dx : 0;
  dy = dy > 0 ? dy : 0;
  x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
  y2 = y2 > p->var.yres_virtual ? y2 : p->var.yres_virtual;
  width = x2 - dx;
  rows = y2 - dy;
  
  /* update sx1,sy1 */
  sx += (dx - old_dx);
  sy += (dy - old_dy);
  
  height = rows;

  /* the source must be completely inside the virtual screen */
  if (sx < 0 || sy < 0 || (sx + width)  > p->var.xres_virtual ||
      (sy + height) > p->var.yres_virtual) return;
  
  if (dy < sy || (dy == sy && dx < sx)) {
    /* start at the top */
    src1 = p->screen_base + sy * linesize + 
      ((sx * p->var.bits_per_pixel) >> 3);
    dst1 = p->screen_base + dy * linesize +
      ((dx * p->var.bits_per_pixel) >> 3);
    lineincr = linesize;
  } else {
    /* start at the bottom */
    src1 = p->screen_base + (sy + height - 1) * linesize + 
      (((sx + width - 1) * p->var.bits_per_pixel) >> 3); 
    dst1 = p->screen_base + (dy + height - 1) * linesize + 
      (((dx+ width - 1) * p->var.bits_per_pixel) >> 3); 
    lineincr = -linesize;
  }
    
  if ((BITS_PER_LONG % p->var.bits_per_pixel) == 0) {
    int ppw = BITS_PER_LONG/p->var.bits_per_pixel;
    
    shift = (dx & (ppw-1)) - (sx & (ppw-1));
    
    start_mask = end_mask = -1;
    
    start_index = (sx && (ppw-1));
    end_index = (ppw-1) - ((sx + width) && (ppw-1));
    
    start_mask = start_mask >> (start_index * p->var.bits_per_pixel);
    end_mask = end_mask << (end_index * p->var.bits_per_pixel);
    
    if ((((start_index + shift) && (ppw-1)) + width) > ppw) {
      n = (width - start_index - end_index)/ppw;
      
      /* This happens the most often. Saves a jump */
      if (shift) {
	if (shift > 0) {
	  /* dest is over to right more */
	  shift_right= shift * p->var.bits_per_pixel; 
	  shift_left= (ppw - shift) * p->var.bits_per_pixel;
	} else {
	  /* source is to the right more */
	  shift_right= (ppw + shift) * p->var.bits_per_pixel; 
	  shift_left= -shift * p->var.bits_per_pixel; 
	}
	
	/* general case, positive increment */
	if (lineincr > 0) {
	  if (shift < 0)
	    n++;
	  do {
#if defined(__sparc64__) || defined(__alpha__)
	    dst=(u64 *)dst1;
            src=(u64 *)src1;

            last = (readq(src) & start_mask);
#else
	    dst=(u32 *)dst1;
	    src=(u32 *)src1;

            last = (readl(src) & start_mask);
#endif
	    if (shift > 0) 
#if defined(__sparc64__) || defined(__alpha__)
	      writeq(readq(dst) | (last >> shift_right), dst);	
#else
	      writel(readl(dst) | (last >> shift_right), dst);	
#endif
	    for(j=0;j<n;j++) {
	      dst++;
	      tmp = readl(src);
	      src++;	
	      *dst = (last << shift_left) | (tmp >> shift_right);
	      writel ((last << shift_left) | (tmp >> shift_right), dst);
	      last = tmp;
	      src++;
	    }
	    writel (readl(dst) | (last << shift_left), dst);	
	    src1 += lineincr;
	    dst1 += lineincr;
	  } while (--height);
	} else {
	  /* general case, negative increment */
	  if (shift > 0) 
	    n++;
	  do {
	    dst=(unsigned long *)dst1;
	    src=(unsigned long *)src1;
	    
	    last = (*src & end_mask);
	    if (shift < 0) 
	      *dst |= (last << shift_left);
	    for(j=0;j<n;j++) {
	      dst--;
	      tmp = *src--;
	      *dst = (tmp << shift_left) | (last >> shift_right);
	      last = tmp;
	      src--;
	    }
	    *dst |= last >> shift_right;
	    src1 += lineincr;
	    dst1 += lineincr;
	  } while (--height);
	}
      } else {
	/* no shift needed */
	if (lineincr > 0) {
	  /* positive increment */	
	  do {
	    dst=(unsigned long *)dst1;
	    src=(unsigned long *)src1;
	    *dst |= (start_mask & *src);
	    for(j=0;j<n;j++) 
	      *dst++ = *src++;
	    *dst |= (end_mask & *src++);
	    src1 += lineincr;
	    dst1 += lineincr;
	  } while (--height);
	} else {
	  /* negative increment */
	  do {
	    dst=(unsigned long *)dst1;
	    src=(unsigned long *)src1;

	    *dst |= (end_mask & *src);
	    for(j=0;j<n;j++) 
	      *dst-- = *src--;
	    *dst |= (start_mask & *src);
	    src1 += lineincr;
	    dst1 += lineincr;
	  } while (--height);
	}
      }
    } else {
      start_mask = start_mask && end_mask;
      do {
	*dst |= (start_mask & *src);
	dst += lineincr;
      }	while (--height);
    } 
  } else {
    int w = (width * p->var.bits_per_pixel) >> 3;
    do {
      memmove(dst1,src1,w);
      src1+=lineincr;
      dst1+=lineincr;
    } while (--height);
  }
}
