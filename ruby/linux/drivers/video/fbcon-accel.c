/*
 *  linux/drivers/video/fbcon-accel.c -- Framebuffer accel console wrapper 
 *
 *      Created 15 Jan 2000 by James Simmons
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/accels.h>

#include "fbcon.h"

extern struct display_switch fbcon_accel;
extern void fbcon_accel_setup(struct display *p);
extern void fbcon_accel_bmove(struct display *p, int sy, int sx, int dy,
                              int dx, int height, int width);
extern void fbcon_accel_clear(struct vc_data *conp, struct display *p, int sy,
                              int sx, int height, int width);
extern void fbcon_accel_putc(struct vc_data *conp, struct display *p, int c,
                             int yy, int xx);
extern void fbcon_accel_putcs(struct vc_data *conp, struct display *p,
                              const unsigned short *s,int count,int yy, int xx);
extern void fbcon_accel_revc(struct display *p, int xx, int yy);
extern void fbcon_accel_clear_margins(struct vc_data *conp, struct display *p,
                                      int bottom_only);

/*
 * Accelerated low level operations
 */

void fbcon_accel_setup(struct display *p)
{
    /* nothing to do ? */
}

void fbcon_accel_bmove(struct display *p, int sy, int sx, int dy, int dx, 
                       int height, int width)
{
    int board = GET_FB_IDX(p->fb_info->node);

    do { } while(gfxops[board]->engine_state(p->fb_info->par));
    gfxops[board]->copyarea(p->fb_info->par, sx * fontwidth(p), 
			    sy * fontheight(p), width * fontwidth(p),
       	                    height * fontheight(p), dx * fontwidth(p),
                            dy * fontheight(p));
}

void fbcon_accel_clear(struct vc_data *conp, struct display *p, int sy,
                       int sx, int height, int width)
{
    int board = GET_FB_IDX(p->fb_info->node);	
    u32 bg_color;	

    if (p->fb_info->fix.visual == FB_VISUAL_TRUECOLOR || 
	p->fb_info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
      if (p->fb_info->var.bits_per_pixel <= 16)
        bg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_bgcol_ec(p, conp)];
      else
        bg_color = ((u32 *) p->fb_info->pseudo_palette)[attr_bgcol_ec(p, conp)];
    } else {
      bg_color = attr_bgcol_ec(p, conp);
    }

    do { } while(gfxops[board]->engine_state(p->fb_info->par));
    gfxops[board]->fillrect(p->fb_info->par, sx * fontwidth(p), 
                            sy * fontheight(p), width * fontwidth(p), 
       	    	            height * fontheight(p), bg_color, ROP_COPY); 
}

void fbcon_accel_putc(struct vc_data *conp, struct display *p, int c, int yy,
                      int xx)
{
    int board = GET_FB_IDX(p->fb_info->node);
    u32 fg_color, bg_color;
    void *bitmap = NULL;

    if (p->fb_info->fix.visual == FB_VISUAL_TRUECOLOR ||
        p->fb_info->fix.visual == FB_VISUAL_DIRECTCOLOR) {	
       if (p->fb_info->var.bits_per_pixel <= 16) {
          fg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_fgcol(p, c)];
          bg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_bgcol(p, c)];
       } else {
          fg_color = ((u32 *) p->fb_info->pseudo_palette)[attr_fgcol(p, c)];
          bg_color = ((u32 *) p->fb_info->pseudo_palette)[attr_bgcol(p, c)];
       }
    } else {
       fg_color = attr_fgcol(p, c);
       bg_color = attr_bgcol(p, c);
    }

    if (fontwidth(p) <= 8)
       bitmap = p->fontdata + (c & p->charmask) * fontheight(p);
    else
       bitmap = p->fontdata + ((c & p->charmask) * fontheight(p) << 1);

    do { } while(gfxops[board]->engine_state(p->fb_info->par));	
    gfxops[board]->imageblit(p->fb_info->par, xx * fontwidth(p), 
                             yy * fontheight(p), fontwidth(p), fontheight(p), 
			     1, bitmap);
}

void fbcon_accel_putcs(struct vc_data *conp, struct display *p,
                       const unsigned short *s, int count, int yy, int xx)
{
    int board = GET_FB_IDX(p->fb_info->node);
    u32 fg_color, bg_color;
    void *bitmap = NULL;
    u16 c;

    if (p->fb_info->fix.visual == FB_VISUAL_TRUECOLOR ||
        p->fb_info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
       if (p->fb_info->var.bits_per_pixel <= 16) {
          fg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_fgcol(p, scr_readw(s))];
          bg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_bgcol(p, scr_readw(s))];
       } else {
          fg_color = ((u32 *) p->fb_info->pseudo_palette)[attr_fgcol(p, scr_readw(s))];
          bg_color = ((u32 *) p->fb_info->pseudo_palette)[attr_bgcol(p, scr_readw(s))];
       }
    } else {
       fg_color = attr_fgcol(p, scr_readw(s));
       bg_color = attr_bgcol(p, scr_readw(s));
    }

    while (count--) {
        c = scr_readw(s++) & p->charmask;
        if (fontwidth(p) <= 8) {
           bitmap = p->fontdata + c * fontheight(p);
        } else {
           bitmap = p->fontdata + (c * fontheight(p) << 1);
        }
   
        do { } while(gfxops[board]->engine_state(p->fb_info->par));
        gfxops[board]->imageblit(p->fb_info->par, xx * fontwidth(p), 
                                 yy * fontheight(p), fontwidth(p), 
			         fontheight(p), 1, bitmap);
        xx += fontwidth(p);
    }
}

void fbcon_accel_revc(struct display *p, int xx, int yy)
{
    int board = GET_FB_IDX(p->fb_info->node);
    u32 fg_color;	

    /* fg_color = p->dispsw_cursor_value; */
    fg_color = ((u16 *) p->fb_info->pseudo_palette)[17]; 	
 
    do { } while(gfxops[board]->engine_state(p->fb_info->par));
    gfxops[board]->fillrect(p->fb_info->par, xx * fontwidth(p), 
			    yy * fontheight(p), fontwidth(p), fontheight(p), 
                            fg_color, ROP_XOR);
}

void fbcon_accel_clear_margins(struct vc_data *conp, struct display *p,
                               int bottom_only)
{
    unsigned int right_start  = conp->vc_cols*fontwidth(p);
    unsigned int bottom_start = conp->vc_rows*fontheight(p);
    unsigned int right_width  = p->fb_info->var.xres - right_start;
    unsigned int bottom_width = p->fb_info->var.yres - bottom_start;
    int board = GET_FB_IDX(p->fb_info->node);
    u32 bg_color; 

    if (p->fb_info->fix.visual == FB_VISUAL_TRUECOLOR ||
        p->fb_info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
        if (p->fb_info->var.bits_per_pixel <= 16)
            bg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_bgcol_ec(p, conp)];
        else
            bg_color = ((u16 *) p->fb_info->pseudo_palette)[attr_bgcol_ec(p, conp)];
    } else {
       bg_color = attr_bgcol_ec(p,conp);
    }
    
    do { } while(gfxops[board]->engine_state(p->fb_info->par));
    if (right_width && !bottom_only) {
        gfxops[board]->fillrect(p->fb_info->par, right_start, 0, right_width, 
                                p->fb_info->var.yres_virtual, bg_color,
                                ROP_COPY);
    }
    if (bottom_width) {
        gfxops[board]->fillrect(p->fb_info->par, 0, 
                                p->fb_info->var.yoffset + bottom_start,
                                right_start, bottom_width, bg_color,
                                ROP_COPY);
    }
}

/*
 *  `switch' for the low level operations
 */

struct display_switch fbcon_accel = {
  fbcon_accel_setup, fbcon_accel_bmove, fbcon_accel_clear, fbcon_accel_putc,
  fbcon_accel_putcs, fbcon_accel_revc, NULL, NULL, fbcon_accel_clear_margins,
  FONTWIDTH(4)|FONTWIDTH(8)|FONTWIDTH(12)|FONTWIDTH(16)
};
