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

static int currcon = 0;

/* ---- `Generic' versions of the frame buffer device operations ----------- */

    /*
     *  Get the Fixed Part of the Display
     */

int fbgen_get_fix(struct fb_fix_screeninfo *fix, int con, struct fb_info *info)
{
    *fix = info->fix;	    
    return 0;
}

    /*
     *  Get the User Defined Part of the Display
     */

int fbgen_get_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
    *var = info->var;
    return 0;
}

    /*
     *  Set the User Defined Part of the Display
     */

int fbgen_set_var(struct fb_var_screeninfo *var, int con, struct fb_info *info)
{
    int oldbpp, err;

    if ((err = fbgen_do_set_var(var, con == currcon, info)))
	return err;
    if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldbpp = info->var.bits_per_pixel;
	info->var = *var;
	if (info->var.xres != var->xres || info->var.yres != var->yres ||
	    info->var.xres_virtual != var->xres_virtual ||
	    info->var.yres_virtual != var->yres_virtual ||
	    info->var.yoffset != var->yoffset || 
	    oldbpp != var->bits_per_pixel) {
	    fbgen_set_disp(con, info);
	    if (oldbpp != var->bits_per_pixel) {
		if ((err = fb_set_cmap(&info->cmap, 1, fb_setcolreg, info)))
            		return err;
      	    }
	    if (info->changevar)
                (*info->changevar)(con);	
	}
    }
    var->activate = 0;
    return 0;
}

    /*
     *  Get the Colormap
     */

int fbgen_get_cmap(struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
    fb_copy_cmap(&info->cmap, cmap, kspc ? 0 : 2);	
    return 0;
}

    /*
     *  Set the Colormap
     */

int fbgen_set_cmap(struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
    struct fbgen_hwswitch *fbhw = info->fbhw;
    int err = 0;

    /* current console? */
    if (con == currcon) {
    	if ((err = fb_set_cmap(cmap, kspc, fbhw->fb_setcolreg, info))) {
                return err;
        } else {
                fb_copy_cmap(cmap, &info->cmap, kspc ? 0 : 1);
        }
    }
    /* Always copy colormap to fb_display. */
    fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
    return 0;
}

    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

int fbgen_pan_display(struct fb_var_screeninfo *var, int con,
		      struct fb_info *info)
{
    int err;

    if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	var->yoffset + info->var.yres > info->var.yres_virtual
	var->xoffset < 0 || var->yoffset < 0)
	return -EINVAL;
    if (con == currcon) {
	if (fbhw->pan_display) {
	    if ((err = fbhw->pan_display(var, info2)))
		return err;
	} else
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

/* ---- Helper functions --------------------------------------------------- */

    /*
     *  Change the video mode
     */

int fbgen_do_set_var(struct fb_var_screeninfo *var, int isactive,
		     struct fb_info_gen *info)
{
    struct fbgen_hwswitch *fbhw = info->fbhw;
    int err, activate;
    char par[info->parsize];

    if ((err = fbhw->decode_var(var, &par, info)))
	return err;
    activate = var->activate;
    if (((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) && isactive)
	fbhw->set_par(&par, info);
    fbhw->encode_var(var, &par, info);
    var->activate = activate;
    return 0;
}

void fbgen_set_disp(int con, struct fb_info *info)
{
    struct display *display;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = info->disp;	/* used during initialization */

    display->visual = info->fix.visual;
    display->type = info->fix.type;
    display->type_aux = info->fix.type_aux;
    display->ypanstep = info->fix.ypanstep;
    display->ywrapstep = info->fix.ywrapstep;
    display->line_length = info->fix.line_length;
    if (info->fbhw->blank || info->fix.visual == FB_VISUAL_PSEUDOCOLOR ||
	info->fix.visual == FB_VISUAL_DIRECTCOLOR)
	display->can_soft_blank = 1;
    else
	display->can_soft_blank = 0;
    fbhw->set_disp(&par, display, info);
#if 0 /* FIXME: generic inverse is not supported yet */
    display->inverse = (fix.visual == FB_VISUAL_MONO01 ? !inverse : inverse);
#else
    display->inverse = fix.visual == FB_VISUAL_MONO01;
#endif
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

int fbgen_update_var(int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    int err;

    if (fbhw->pan_display) {
        if ((err = fbhw->pan_display(&fb_display[con].var, info2)))
            return err;
    }
    return 0;
}

    /*
     *  Switch to a different virtual console
     */

int fbgen_switch(int con, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;

    /* Do we have to save the colormap ? */
    if (fb_display[currcon].cmap.len)
	fb_get_cmap(&fb_display[currcon].cmap, 1, fbhw->getcolreg,
		    &info2->info);
    fbgen_do_set_var(&fb_display[con].var, 1, info2);
    currcon = con;
    /* Install new colormap */
    fbgen_install_cmap(con, info2);
    return 0;
}

    /*
     *  Blank the screen
     */

void fbgen_blank(int blank, struct fb_info *info)
{
    struct fb_info_gen *info2 = (struct fb_info_gen *)info;
    struct fbgen_hwswitch *fbhw = info2->fbhw;
    u16 black[16];
    struct fb_cmap cmap;

    if (fbhw->blank && !fbhw->blank(blank, info2))
	return;
    if (blank) {
	memset(black, 0, 16*sizeof(u16));
	cmap.red = black;
	cmap.green = black;
	cmap.blue = black;
	cmap.transp = NULL;
	cmap.start = 0;
	cmap.len = 16;
	fb_set_cmap(&cmap, 1, fbhw->setcolreg, info);
    } else
	fbgen_install_cmap(currcon, info2);
}
