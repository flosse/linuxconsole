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

/* ---- `Generic' versions of the frame buffer device operations ----------- */

    /*
     *  Set the User Defined Part of the Display
     */

int fb_set_var(struct fb_var_screeninfo *var, struct fb_info *info)
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
