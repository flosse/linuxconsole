/*
 * linux/drivers/video/fbgen.c -- Generic routines for frame buffer devices
 *
 *  fb_setup() added June 2001 by Paul Mundt
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
#include <linux/init.h>
#include <linux/config.h>

#include <asm/uaccess.h>
#include <asm/io.h>

/* ---- `Generic' versions of the frame buffer device operations ----------- */

/**
 * 	fb_set_var - set var
 *
 * 	@var:  frame buffer user defined part of display
 * 	@info: frame buffer info structure
 *
 * 	This call sets the user defined part of the display.
 *	
 *	Returns 0 upon success, non 0 on error.
 *
 */
int fb_set_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    int oldbpp, err;

    if (memcmp(&info->var, var, sizeof(var))) {	
	if (!info->fbops->fb_check_var) {
		*var = info->var;	
        	return 0;
	}

	if ((err = info->fbops->fb_check_var(var, info)))
		return err;

    	if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		oldbpp = info->var.bits_per_pixel;
		info->var = *var;		

	    	if (info->fbops->fb_set_par) 
			info->fbops->fb_set_par(info); 

		if (info->cursor.enable) {
			info->cursor.set = FB_CUR_SETALL;
			info->fbops->fb_cursor(info, &info->cursor);
		}
 
		if ((err = fb_alloc_cmap(&info->cmap, 0, 0)))
            		return err;
		fb_set_cmap(&info->cmap, 1, info);
	}
	var->activate = 0;
    }
    return 0;
}

/**
 * 	fb_pan_display - pan or wrap the display
 *
 * 	@var:  frame buffer user defined part of display
 * 	@info: frame buffer info structure
 *
 * 	This call pans or wraps the display. It only looks at xoffset, yoffset
 * 	and the FB_VMODE_YWRAP flag.
 *
 * 	Returns 0 upon success, -EINVAL on failure.
 *
 */
int fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info) 
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

extern const char *global_mode_option;

/**
 * 	fb_setup - generic setup function
 *
 * 	@options: String of options, ',' delimited.
 *
 * 	Process command line options for frame buffer driver.
 *
 *	Command line example:
 *
 *		video=drivername:opt1,opt2,...,optN
 *
 * 	Supported options:
 *
 * 		inverse	- inverts colormap
 * 		nomtrr	- disables MTRR usage (if supported)
 *
 * 	Returns 0 upon completion.
 *
 */
int __init fb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options) {
		return 0;
	}

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) {
			continue;
		}

#if 0
		if (!strncmp(this_opt, "inverse", 7)) {
			fb_invert_cmaps(info);
#ifdef CONFIG_MTRR
		} else if (!strncmp(this_opt, "nomtrr", 6)) {
			fb_disable_mtrrs(info);
#endif
		} else {
			global_mode_option = this_opt;
		}
#endif
	}
	return 0;
}
