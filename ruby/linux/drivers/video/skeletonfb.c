/*
 * linux/drivers/video/skeletonfb.c -- Skeleton for a frame buffer device
 *
 *  Modified to new api Jan 2001 by James Simmons (jsimmons@linux-fbdev.org)
 *
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>

/* This header contains struct xxx_par for your graphics card */
#include <video/skeletion.h>

    /*
     *  This is just simple sample code.
     *
     *  No warranty that it actually compiles.
     *  Even less warranty that it actually works :-)
     */

static struct fb_fix_screeninfo xxxfb_fix __initdata = {
    "FB's name", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_PSEUDOCOLOR, 1, 1, 1, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

    /*
     * 	Modern graphical hardware not only supports pipelines but some 
     *  also support multiple monitors where each display can have its  
     *  its own unique data. In this case each display could be  
     *  represented by a seperate framebuffer device thus a seperate 
     *  struct fb_info. In this case all of the par structures for the
     *  graphics card would be shared between each struct fb_info. This
     *  allows when one display changes it video resolution (info->var) 
     *  the other displays know instantly. Each display can always be
     *  aware of the entire hardware state that affects it. I hope this 
     *  covers every possible hardware design. If not feel free to send 
     *  me more design types. 
     */

    /*
     *  If your driver supports multiple boards, you should make these  
     *  arrays, or allocate them dynamically (using kmalloc()). 
     */ 
static struct fb_info info;
    /* 
     * This struct represents the state of a rendering pipe. A modern 
     * graphics card can have more than one pipe per card depending 
     * on the hardware design. So the same holds true if you have
     * multiple pipes. Use arrays or allocate them dynamically.
     *
     * Read video/skeleton.h for more information about graphics pipes. 
     */
static struct xxx_par __initdata current_par;

static u32 xxxfb_pseudo_palette[17];
static int inverse = 0;

int xxxfb_init(void);
int xxxfb_setup(char*);

/* ------------------- chipset specific functions -------------------------- */

static int xxxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    const struct xxx_par *par = (const struct xxx_par *) info->par;
    /* 
     * We test to see if the hardware can support var. Struct xxx_par will
     * have the information needed to see if it does. Note we don't change
     * par nor info->var. This function can be called on its own if we
     * intent to only test a mode and not set it. Return 0 if it is a
     * acceptable mode.
     */

    /* ... */
    return 0;	   	
}

static void xxxfb_set_par(struct fb_info *info)
{
    /*
     *  xxx_fb_check_var tested the mode we want to set the hardware to.
     *  If it passes it then is set to info->var. Now we set the hardware
     *  (and struct par) according to info->var.
     */

    /* ... */
}

    /*
     *  Set a single color register. The values supplied have a 16 bit
     *  magnitude. Return != 0 for invalid regno. This routine assumes
     *  your graphics hardware is packed pixel based (most are :-). 
     *  Return != 0 for invalid regno.
     */
static int xxxfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   const struct fb_info *info)
{
    if (regno >= 256)  /* no. of hw registers */
       return 1;
    /*
     * Program hardware... do anything you want with transp
     */

    /* grayscale works only partially under directcolor */
    if (info->var.grayscale) {
       /* grayscale = 0.30*R + 0.59*G + 0.11*B */
       red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
    }

    /* Directcolor:
     *   var->{color}.offset contains start of bitfield
     *   var->{color}.length contains length of bitfield
     *   {hardwarespecific} contains width of DAC
     *   cmap[X] is programmed to (X << red.offset) | (X << green.offset) | (X << blue.offset)
     *   DAC[X] is programmed to (red, green, blue)
     *
     * Pseudocolor:
     *    uses offset = 0 && length = DAC register width.
     *    var->{color}.offset is 0
     *    var->{color}.length contains widht of DAC
     *    cmap is not used
     *    DAC[X] is programmed to (red, green, blue)
     * Truecolor:
     *    does not use DAC.
     *    var->{color}.offset contains start of bitfield
     *    var->{color}.length contains length of bitfield
     *    cmap is programmed to (red << red.offset) | (green << green.offset) |
     *                      (blue << blue.offset) | (transp << transp.offset)
     *    DAC does not exist
     */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
    switch (info->fix.visual) {
       case FB_VISUAL_TRUECOLOR:
       case FB_VISUAL_PSEUDOCOLOR:
               red = CNVT_TOHW(red, info->var.red.length);
               green = CNVT_TOHW(green, info->var.green.length);
               blue = CNVT_TOHW(blue, info->var.blue.length);
               transp = CNVT_TOHW(transp, info->var.transp.length);
               break;
       case FB_VISUAL_DIRECTCOLOR:
	       /* example here assumes 8 bit DAC. Might be different 
		* for your hardware */	
               red = CNVT_TOHW(red, 8);       
               green = CNVT_TOHW(green, 8);
               blue = CNVT_TOHW(blue, 8);
               /* hey, there is bug in transp handling... */
               transp = CNVT_TOHW(transp, 8);
               break;
    }
#undef CNVT_TOHW
    /* Truecolor has hardware independent palette */
    if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
       u32 v;

       if (regno >= 16)
           return 1;

       v = (red << info->var.red.offset) |
           (green << info->var.green.offset) |
           (blue << info->var.blue.offset) |
           (transp << info->var.transp.offset);
       if (info->var.bits_per_pixel == 16)
           ((u16*)(info->pseudo_palette))[regno] = v;
       else
           ((u32*)(info->pseudo_palette))[regno] = v;
       return 0;
    }
    /* ... */
    return 0;
}

static int xxxfb_pan_display(struct fb_var_screeninfo *var,
			     const struct fb_info *info)
{
    /*
     *  Pan (or wrap, depending on the `vmode' field) the display using the
     *  `xoffset' and `yoffset' fields of the `var' structure.
     *  If the values don't fit, return -EINVAL.
     */

    /* ... */
    return 0;
}

static int xxxfb_blank(int blank_mode, const struct fb_info *info)
{
    /*
     *  Blank the screen if blank_mode != 0, else unblank. If blank == NULL
     *  then the caller blanks by setting the CLUT (Color Look Up Table) to all
     *  black. Return 0 if blanking succeeded, != 0 if un-/blanking failed due
     *  to e.g. a video mode which doesn't support it. Implements VESA suspend
     *  and powerdown modes on hardware that supports disabling hsync/vsync:
     *    blank_mode == 2: suspend vsync
     *    blank_mode == 3: suspend hsync
     *    blank_mode == 4: powerdown
     */

    /* ... */
    return 0;
}

/* ------------ Accelerated Functions --------------------- */

/*
 * We provide our own functions if we have hardware acceleration
 * or non packed pixel format layouts.  
 */

void xxxfb_fillrect(struct fb_info *p, int x1, int y1, unsigned int width,
                    unsigned int height, unsigned long color, int rop)
{
}

void xxxfb_copyarea(struct fb_info *p, int sx, int sy, unsigned int width,
                         unsigned int height, int dx, int dy)
{
}

void xxxfb_imageblit(struct fb_info *p, struct fb_image *image) 
{
}

/* ------------ Hardware Independent Functions ------------ */

    /*
     *  Initialization
     */

int __init xxxfb_init(void)
{
    int retval;	
   
    /* 
     * Here we set the screen_base to the vitrual memory address
     * for the framebuffer. Usually we obtain the resource address
     * from the bus layer and then translate it to virtual memory
     * space via ioremap. Consult ioport.h. 
     */
    info.screen_base = framebuffer_virtual_memory;	
    info.node = -1;
    info.fbops = &xxxfb_ops;
    info.fix = xxxfb_fix;
    info.par = xxx_par;
    info.pseudo_palette = xxxfb_pseudo_palette;
    info.flags = FBINFO_FLAG_DEFAULT;
    /* This should give a reasonable default video mode */
    if (!mode_option)
	mode_option = "640x480@60";	 	

    retval = fb_find_mode(&info.var, &info, mode_option, NULL, 0, NULL, 8);
  
    if (!retval || retval == 4)
	return -EINVAL;			

    info.cmap = fb_default_cmap(1<<info.var.bits_per_pixel);	
	
    if (register_framebuffer(&info) < 0)
	return -EINVAL;
    printk(KERN_INFO "fb%d: %s frame buffer device\n", GET_FB_IDX(info.node),
	   info.fix.id);

    /* uncomment this if your driver cannot be unloaded */
    /* MOD_INC_USE_COUNT; */
    return 0;
}

    /*
     *  Cleanup
     */

static void __exit xxxfb_cleanup(void)
{
    /*
     *  If your driver supports multiple boards, you should unregister and
     *  clean up all instances.
     */

    unregister_framebuffer(info);
    /* ... */
}

    /*
     *  Setup
     */

int __init xxxfb_setup(char *options)
{
    /* Parse user speficied options (`video=xxxfb:') */
}


/* ------------------------------------------------------------------------- */

    /*
     *  Frame buffer operations
     */

/* If all you need is that - just don't define ->fb_open */
static int xxxfb_open(const struct fb_info *info, int user)
{
    return 0;
}

/* If all you need is that - just don't define ->fb_release */
static int xxxfb_release(const struct fb_info *info, int user)
{
    return 0;
}

static struct fb_ops xxxfb_ops = {
	owner:		THIS_MODULE,
	fb_open:	xxxfb_open,    /* only if you need it to do something */
	fb_release:	xxxfb_release, /* only if you need it to do something */
	fb_check_var:	xxxfb_check_var,
	fb_set_par:	xxxfb_set_par,
	fb_setcolreg:	xxxfb_setcolreg,
	fb_blank:	xxxfb_blank,
	fb_pan_display:	xxxfb_pan_display,
	fb_fillrect:	xxxfb_fillrect,  
	fb_copyarea:	xxxfb_copyarea,  
	fb_imageblit:	xxxfb_imageblit, 
	fb_ioctl:       xxxfb_ioctl,     /* optional */
	fb_mmap:	xxxfb_mmap,      /* optional */	
};

/* ------------------------------------------------------------------------- */


    /*
     *  Modularization
     */

#ifdef MODULE
module_init(xxxfb_init);
module_exit(xxxfb_cleanup);
#endif /* MODULE */
