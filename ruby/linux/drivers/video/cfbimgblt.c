/*
 *  Generic BitBLT function for frame buffer with packed pixels of any depth.
 *
 *      Copyright (C)  June 1999 James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 *
 * NOTES:
 *
 *    This function copys a image from system memory to video memory. The
 *  image can be a bitmap where each 0 represents the background color and
 *  each 1 represents the foreground color. Great for font handling. It can
 *  also be a color image. This is determined by image_depth. The color image
 *  must be laid out exactly in the same format as the framebuffer. Yes I know
 *  their are cards with hardware that coverts images of various depths to the
 *  framebuffer depth. But not every card has this. All images must be rounded
 *  up to the nearest byte. For example a bitmap 12 bits wide must be two 
 *  bytes width. 
 *
 *  FIXME
 *  The code for 24 bit is horrible. It copies byte by byte size instead of
 *  longs like the other sizes. Needs to be optimized.
 *
 *  Also need to add code to deal with cards endians that are different than
 *  the native cpu endians. I also need to deal with MSB position in the word.
 *
 */
#include <linux/string.h>
#include <linux/fb.h>
#include <asm/types.h>

#define DEBUG

#ifdef DEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt,__FUNCTION__,## args)
#else
#define DPRINTK(fmt, args...)
#endif

void cfb_imageblit(struct fb_info *p, struct fb_image *image)
{
  unsigned long end_index, end_mask, mask, tmp, fgx, bgx, eorx;
  int ppw, shift, shift_right, shift_left, x2, y2, n, i, j, k;
  int linesize = p->fix.line_length;
  unsigned long *dst, *src = NULL;
  u8 *dst1, *src1;
  
  /* We could use hardware clipping but on many cards you get around hardware
     clipping by writing to framebuffer directly like we are doing here. */
  x2 = image->x + image->width;
  y2 = image->y + image->height;
  image->x = image->x > 0 ? image->x : 0;
  image->y = image->y > 0 ? image->y : 0;
  x2 = x2 < p->var.xres_virtual ? x2 : p->var.xres_virtual;
  y2 = y2 < p->var.yres_virtual ? y2 : p->var.yres_virtual;
  image->width  = x2 - image->x;
  image->height = y2 - image->y;
  
  dst1 = p->screen_base + image->y * linesize + 
		((image->x * p->var.bits_per_pixel) >> 3);
  
  ppw = BITS_PER_LONG/p->var.bits_per_pixel;

  src1 = image->data;	
  
  fgx = image->fg_color;
 
  for (i = 0; i < ppw; i++) {
    fgx <<= p->var.bits_per_pixel;
    fgx |= image->fg_color;
  }

  bgx = image->bg_color;
 
  for (i = 0; i < ppw; i++) {
    bgx <<= p->var.bits_per_pixel;
    bgx |= image->bg_color;
  }
  eorx = fgx ^ bgx;
  tmp = (1 << p->var.bits_per_pixel) - 1;

  for (i = 0; i < image->height; i++) {
	dst = (unsigned long *) dst1; 
	for (j = image->width; j > 0; j--) {
		mask = 0;
		for (k = 0; k < ppw; k++) {
			if (test_bit(j-k, src1))
				mask |= (tmp << (p->var.bits_per_pixel*k));
		}
		fb_writel((mask & eorx)^bgx, dst);
		j -= (k-1);
		dst++;
	}
	dst1 += p->fix.line_length;	
	src1++;	
  }	
}
