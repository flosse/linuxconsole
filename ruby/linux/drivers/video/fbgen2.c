/*
 * linux/drivers/video/fbgen2.c -- Generic routines for frame buffer devices
 *
 *  Created 27 June 2001 by "Crazy" James Simmons <jsimmons@transvirtual.com>
 *
 *	2001 - Documented with DocBook
 *	- Brad Douglas <brad@neruo.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#include <video/fbcon.h>

/**
 *	fbgen_set_disp - set generic display
 *	@con: virtual console number
 *	@info: generic frame buffer info structure
 *
 *	Sets a display on virtual console @con for device @info.
 *
 */

void fbgen2_set_disp(int con, struct fb_info *info)
{
	struct display *display;

    	if (con >= 0)
		display = &fb_display[con];
    	else
		display = info->disp;	/* used during initialization */

	display->screen_base = info->screen_base;
    	display->var = info->var;
	display->visual = info->fix.visual;
    	display->type = info->fix.type;
    	display->type_aux = info->fix.type_aux;
    	display->ypanstep = info->fix.ypanstep;
    	display->ywrapstep = info->fix.ywrapstep;
    	display->line_length = info->fix.line_length;
    	if (info->blank || info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
	    info->fix.visual == FB_VISUAL_DIRECTCOLOR)
		display->can_soft_blank = 1;
    	else
		display->can_soft_blank = 0;
#if 0 /* FIXME: generic inverse is not supported yet */
    	display->inverse = (info->fix.visual==FB_VISUAL_MONO01 ? !inverse : inverse);
#else
    	display->inverse = info->fix.visual == FB_VISUAL_MONO01;
#endif

	switch (info->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
        case 4:
                display->dispsw         = &fbcon_cfb4;
                break;
#endif
#ifdef FBCON_HAS_CFB8
        case 8:
		display->dispsw         = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 12:
        case 16:
                display->dispsw         = &fbcon_cfb16;
                display->dispsw_data    = info->pseudo_palette;
                break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		display->dispsw         = &fbcon_cfb32;
                display->dispsw_data    = info->pseudo_palette;
		break;
#endif
        default:
                display->dispsw = &fbcon_dummy;
                break;
        }
	
		
}

/* ---- `Generic' versions of the frame buffer device operations ----------- */

/**
 *	fbgen_get_fix - get fixed part of display
 *	@fix: fb_fix_screeninfo structure
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Get the fixed information part of the display and place it
 *	into @fix for virtual console @con on device @info.
 *
 *	Returns negative errno on error, or zero on success.
 *
 */
int fbgen_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
	*fix = info->fix;
    	return 0;
}


/**
 *	fbgen_get_var - get user defined part of display
 *	@var: fb_var_screeninfo structure
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Get the user defined part of the display and place it into @var
 *	for virtual console @con on device @info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */
int fbgen_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
    if (con == info->currcon)
	var = &info->var;	
    else
	var = &fb_display[con].var;
    return 0;	
}

/**
 *	fbgen_set_var - set the user defined part of display
 *	@var: fb_var_screeninfo user defined part of the display
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Set the user defined part of the display as dictated by @var
 *	for virtual console @con on device @info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */
int fbgen_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
	struct fb_bitfield oldred, oldgreen, oldblue, oldalpha;
	int oldbpp, err;

    	if (memcmp(&info->var, var, sizeof(var)) || con < 0) {
        	if ((err = info->fbops->fb_check_var(var, info)))
                	return err;

		if (var->activate & FB_ACTIVATE_ALL)
                	info->disp->var = *var;

       		if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
        		oldbpp = info->var.bits_per_pixel;
			oldred = info->var.red;
			oldblue = info->var.blue;
			oldgreen = info->var.green;
			oldalpha = info->var.transp;

               		if (info->fbops->fb_set_par)
                		info->fbops->fb_set_par(info);
               		info->var = *var;
               		fbgen2_set_disp(con, info);
               		if (info->changevar)
                		(*info->changevar)(con);
                	if (oldbpp != var->bits_per_pixel || 
			  memcmp(&oldred,&info->var.red,sizeof(oldred)) ||
			  memcmp(&oldgreen,&info->var.green,sizeof(oldgreen)) ||
			  memcmp(&oldblue, &info->var.blue, sizeof(oldblue)) ||
			  memcmp(&oldalpha,&info->var.transp,sizeof(oldalpha))){
                        	if ((err = fb_alloc_cmap(&info->cmap, 0, 0)))
                                	return err;
                        	fb_set_cmap(&info->cmap, 1, 
					     info->fbops->fb_setcolreg, info);
                	}
        	}
		var->activate = 0;
	}
    	return 0;
}

/**
 *	fbgen_get_cmap - get the colormap
 *	@cmap: frame buffer colormap structure
 *	@kspc: boolean, 0 copy local, 1 put_user() function
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Gets the colormap for virtual console @con and places it into
 *	@cmap for device @info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */
int fbgen_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
	struct fb_cmap *dcmap;

	if (con == info->currcon) 
		dcmap = &info->cmap;
	else 
		dcmap = &fb_display[con].cmap;
        
	fb_copy_cmap(dcmap, cmap, kspc ? 0 : 2);
 	return 0;
}

/**
 *	fbgen_set_cmap - set the colormap
 *	@cmap: frame buffer colormap structure
 *	@kspc: boolean, 0 copy local, 1 get_user() function
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Sets the colormap @cmap for virtual console @con on
 *	device @info.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */

int fbgen_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
        struct fb_cmap *dcmap;
        int err = 0;

	if (con == info->currcon) 
		dcmap = &info->cmap;
	else
		dcmap = &fb_display[con].cmap;

        /* no colormap allocated? */
        if (!dcmap->len)
                err = fb_alloc_cmap(dcmap, 256, 0);

        if (!err && con == info->currcon)
                err = fb_set_cmap(cmap, kspc, info->fbops->fb_setcolreg, info);

        if (!err)
                fb_copy_cmap(cmap, dcmap, kspc ? 0 : 1);
        return err;
}

/**
 *	fbgen_update_var - update user defined part of display
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Updates the user defined part of the display ('var'
 *	structure) on virtual console @con for device @info.
 *	This function is called by fbcon.c.
 *
 *	Returns negative errno on error, or zero for success.
 *
 */

int fbgen_update_var(int con, struct fb_info *info)
{
	int err = 0;

    	if (info->fbops->fb_pan_display) 
        	err = info->fbops->fb_pan_display(&fb_display[con].var,con,info);
    	return err;
}

/**
 *	fbgen_switch - switch to a different virtual console.
 *	@con: virtual console number
 *	@info: frame buffer info structure
 *
 *	Switch to virtuall console @con on device @info.
 *
 *	Returns zero.
 *
 */

int fbgen_switch(int con, struct fb_info *info)
{
        struct display *disp;
        struct fb_cmap *cmap;

        if (con == info->currcon)
                return 0;

        if (info->currcon >= 0) {
                disp = fb_display + info->currcon;

                /*
                 * Save the old colormap and video mode.
                 */
                disp->var = info->var;
                if (disp->cmap.len)
                        fb_copy_cmap(&info->cmap, &disp->cmap, 0);
        }

        info->currcon = con;
        disp = fb_display + con;

        if (disp->cmap.len)
                cmap = &disp->cmap;
        else
                cmap = fb_default_cmap(1 << disp->var.bits_per_pixel);

        fb_copy_cmap(cmap, &info->cmap, 0);

        info->var = disp->var;
        info->var.activate = FB_ACTIVATE_NOW;

        fbgen_set_var(&info->var, con, info);
        return 0;
}
