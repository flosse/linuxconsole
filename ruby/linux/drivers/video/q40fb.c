/* 
 * linux/drivers/video/q40fb.c -- Q40 frame buffer device
 *
 * Copyright (C) 2000 
 *
 *      Richard Zidlicky <Richard.Zidlicky@stud.informatik.uni-erlangen.de>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/q40_master.h>
#include <linux/fb.h>
#include <linux/module.h>
#include <asm/pgtable.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb16.h>

#define Q40_PHYS_SCREEN_ADDR 0xFE800000

static u16 pseudo_palette[17];
static struct display disp;
static struct fb_info fb_info;
static int currcon = 0;

static struct fb_fix_screeninfo q40fb_fix __initdata = {
    "Q40", (unsigned long) NULL, 1024*1024, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_TRUECOLOR, 0, 0, 0, 1024*2, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static struct fb_var_screeninfo q40fb_var __initdata = {
    /* 1024x512, 16 bpp */
    1024, 512, 1024, 512, 0, 0, 16, 0,
    {6, 5, 0}, {11, 5, 0}, {0, 6, 0}, {0, 0, 0},
    0, FB_ACTIVATE_NOW, 230, 300, 0, 0, 0, 0, 0, 0, 0, 0,
    0, FB_VMODE_NONINTERLACED
};

/* frame buffer operations */
int q40fb_init(void);

static int q40fb_open(struct fb_info *info, int user);
static int q40fb_release(struct fb_info *info, int user);
static int q40fb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int q40fb_setcolreg(unsigned regno, unsigned red, unsigned green,
                           unsigned blue, unsigned transp,
                           struct fb_info *info);

static struct fb_ops q40fb_ops = {
        fb_open:        q40fb_open,
        fb_release:     q40fb_release,
        fb_get_fix:     fbgen_get_fix,
        fb_get_var:     fbgen_get_var,
        fb_set_var:     q40fb_set_var,
        fb_get_cmap:    fbgen_get_cmap,
        fb_set_cmap:    fbgen_set_cmap,
	fb_setcolreg:	q40fb_setcolreg
};

static void q40fb_set_disp(int con, struct fb_info *info);

static int q40fb_open(struct fb_info *info, int user)
{
        /*
         * Nothing, only a usage count for the moment
         */

        MOD_INC_USE_COUNT;
        return(0);
}

static int q40fb_release(struct fb_info *info, int user)
{
        MOD_DEC_USE_COUNT;
        return(0);
}

static int q40fb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
	return -EINVAL;
}

static int q40fb_setcolreg(unsigned regno, unsigned red, unsigned green,
		  	   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
    /*
     *  Set a single color register. The values supplied have a 16 bit
     *  magnitude.
     *  Return != 0 for invalid regno.
     */
  
    red>>=11;
    green>>=11;
    blue>>=10;

    if (regno < 16) {
	info->pseudo_palette[regno] = ((red & 31) <<6) |
	                              ((green & 31) << 11) |
	                         	(blue & 63);
    }
    return 0;
}

static void q40fb_set_disp(int con, struct fb_info *info)
{
  struct display *display;

  if (con>=0)
    display = &fb_display[con];
  else 	
    display = &disp;

  if (con<0) con=0;

   display->screen_base = (char *) info->fix.smem_start;
   display->visual = info->fix.visual;
   display->type = info->fix.type;
   display->type_aux = info->fix.type_aux;
   display->ypanstep = info->fix.ypanstep;
   display->ywrapstep = info->fix.ywrapstep;
   display->can_soft_blank = 0;
   display->inverse = 0;
   display->line_length = info->fix.line_length;

   display->scrollmode = SCROLL_YREDRAW;

#ifdef FBCON_HAS_CFB16
   display->dispsw = &fbcon_cfb16;
   display->dispsw_data = pseudo_palette;
#else
   display->dispsw = &fbcon_dummy;
#endif
   fb_copy_cmap(&info->cmap, &display->cmap, 0);
}
  
int q40fb_init(void)
{
        if ( !MACH_IS_Q40)
	  return -ENXIO;

	/* mapped in q40/config.c */
	q40fb_fix.smem_start = Q40_PHYS_SCREEN_ADDR;
	
        fb_info.changevar = NULL;
	fb_info.disp = &disp; 
	fb_info.var = q40fb_var;
	fb_info.fix = q40fb_fix;
	strcpy(&fb_info.modename[0],fb_info.fix.id);
	fb_info.switch_con = &fbgen_switch;
	fb_info.updatevar = &fbgen_updatevar;
	fb_info.node = -1;
	fb_info.fbops = &q40fb_ops;
	fb_info.flags = FBINFO_FLAG_DEFAULT;  /* not as module for now */
	
	master_outb(3,DISPLAY_CONTROL_REG);

        memcpy(&disp.var, &fb_info.var, sizeof(struct fb_var_screeninfo));
	fb_copy_cmap(fb_default_cmap(1<<fb_info.var.bits_per_pixel), 
			&fb_info.cmap, 0);
	fb_set_cmap(&fb_info.cmap, 1, q40fb_setcolreg, &fb_info);
	q40fb_set_disp(-1, &fb_info);

	if (register_framebuffer(&fb_info) < 0) {
		printk(KERN_ERR "unable to register Q40 frame buffer\n");
		return -EINVAL;
	}

        printk(KERN_INFO "fb%d: Q40 frame buffer alive and kicking !\n",
	       GET_FB_IDX(fb_info.node));
	return 0;
}	
