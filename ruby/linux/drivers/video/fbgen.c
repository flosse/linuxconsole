/*
 * linux/drivers/video/fbgen.c -- Generic routines for frame buffer devices
 *
 *  Modified to new API May 2000 by James Simmons
 *
 *  Created 3 Jan 1998 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include "fbcon.h"

/* ---- `Generic' versions of the frame buffer device operations ----------- */

    /*
     *  Get the Fixed Part of the Display
     */

int fbgen_get_fix(struct fb_fix_screeninfo *fix, struct fb_info *info)
{
    *fix = info->fix;	    
    return 0;
}

    /*
     *  Get the User Defined Part of the Display
     */

int fbgen_get_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    *var = info->var;
    return 0;
}

    /*
     *  Set the User Defined Part of the Display
     */

int fbgen_set_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    int err, oldbpp;
	
    if (memcmp(&info->var, var, sizeof(var))) {	
	if ((err = info->fbops->fb_check_var(var, info)))
		return err;

    	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldbpp = info->var.bits_per_pixel;
	    	info->fbops->fb_set_par(info); 
 
	    	if (oldbpp != var->bits_per_pixel) {
			if ((err = fb_set_cmap(&info->cmap, 1, info)))
            			return err;
      	    	}
	}
	var->activate = 0;
    }
    return 0;
}

    /*
     *  Blank the screen
     */

void fbgen_blank(int blank, struct fb_info *info)
{
    struct fb_cmap cmap;
    u16 black[16];

    if (info->fbops->fb_blank && !info->fbops->fb_blank(blank, info))
        return;
    if (blank) {
        memset(black, 0, 16*sizeof(u16));
        cmap.red = black;
        cmap.green = black;
        cmap.blue = black;
        cmap.transp = NULL;
        cmap.start = 0;
        cmap.len = 16;
        fb_set_cmap(&cmap, 1, info);
        return;
    }
    return;
}

    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

int fbgen_pan_display(struct fb_var_screeninfo *var, struct fb_info *info) 
{
    int err;

    if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	var->yoffset + info->var.yres > info->var.yres_virtual || 
	var->xoffset < 0 || var->yoffset < 0)
	return -EINVAL;
    if (info->fbops->fb_pan_display) {
    	if ((err = info->fbops->fb_pan_display(var, info)))
		return err;
	} else {
	    return -EINVAL;
    }
    info->var.xoffset = var->xoffset;
    info->var.yoffset = var->yoffset;
    if (var->vmode & FB_VMODE_YWRAP)
	info->var.vmode |= FB_VMODE_YWRAP;
    else
	info->var.vmode &= ~FB_VMODE_YWRAP;
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

int fbgen_update_var(struct fb_info *info)
{
    int err;

    if (info->fbops->fb_pan_display) {
        if ((err = info->fbops->fb_pan_display(&info->var, info)))
            return err;
    }
    return 0;
}
