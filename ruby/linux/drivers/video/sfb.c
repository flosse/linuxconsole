/*
 *  linux/drivers/video/sfb.c -- Simple frame buffer device
 *
 *	 Based on virtual frame buffer by Geert Uytterhoeven
 *
 *  This is a simple no-frills frame buffer driver for devices that don't
 *  (yet) have hardware-specific drivers written.  It is very limited in that
 *  it does no hardware-specific operations, so hardware initialization must
 *  be done prior to boot, no mode changes are allowed, and the palette cannot
 *  be changed.  It also requires a fixed, known virtual address for the base
 *  of video ram.  For these reasons, it is mostly for test and development
 *  purposes.
 *
 *  NOTE:  No hardware-specific code should go in this file.  Instead, once
 *  a device needs specific code, it should be split off into its own frame
 *  buffer device.  On the other hand, feel free to add more general
 *  capabilities to the frame buffer parameters below.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/config.h>

/*
 * Linux VR uses SFB extensively, so params based on platform in that case
 */
#ifdef CONFIG_CPU_VR41XX
#include <asm/vr41xx-platdep.h>
#endif

/*
 * VIDEORAM_BASE     Address of start of video ram (virtual address)
 * VIDEORAM_SIZE     Size of video ram (default is x * y * (bpp/8)) 
 * FB_X_RES          Visible horizontal resolution in pixels
 * FB_X_VIRTUAL_RES  Horizontal resolution of framebuffer memory in pixels
 *                   (default is FB_X_RES)
 * FB_Y_RES          Visible vertical resolution in pixels
 * FB_Y_VIRTUAL_RES  Vertical resolution of framebuffer memory in pixels
 *                   (default FB_Y_RES)
 * FB_BPP            Bits per pixel
 * FB_PALETTE        The palette for a paletted device, whatever has been
 *                   initialized before boot (since sfb can't change it)
 * FB_IS_GREY        1 means greyscale device, otherwise 0 (default is 0)
 * FB_IS_TRUECOLOR   Define this as 1 to force truecolor for any mode
 *                   less than 16bpp, this is useful for 8bpp truecolor
 *                   devices (this defaults to 1 for 16bpp or greater)
 * The last three are done with 1 / 0 instead of #define / #undef in order
 * to facilitate a generic interface, if we should coose to do one in the
 * future.
 *
 * Some example settings:
 *
 * #define VIDEORAM_BASE	0x12345678
 * #define VIDEORAM_SIZE	(1024 * 1024)
 * #define FB_X_RES		800
 * #define FB_X_VIRTUAL_RES	1024
 * #define FB_Y_RES		600
 * #define FB_Y_VIRTUAL_RES	1024
 * #define FB_BPP		8
 * #define FB_PALETTE		{ {0x12,0x34,0x56,0}, {0x78,0x9a,0xbc,0}, ... }
 * #define FB_IS_GREY		0
 * #define FB_IS_TRUECOLOR	0
 */

/*
 * Please put all hardware-specific defines (or includes) above this line, the
 * frame buffer should be completely described by above parameters.
 */

#ifndef VIDEORAM_BASE
#error "SFB frame buffer parameters have not been defined."
#endif

#ifndef FB_X_VIRTUAL_RES
#define FB_X_VIRTUAL_RES FB_X_RES
#endif

#ifndef FB_Y_VIRTUAL_RES
#define FB_Y_VIRTUAL_RES FB_Y_RES
#endif

#ifndef VIDEORAM_SIZE
#define VIDEORAM_SIZE (FB_X_VIRTUAL_RES * FB_Y_VIRTUAL_RES * FB_BPP / 8)
#endif

#ifndef FB_IS_GREY
#define FB_IS_GREY 0
#endif

#ifndef FB_IS_TRUECOLOR
#if (FB_BPP >= 16)
#define FB_IS_TRUECOLOR 1
#else
#define FB_IS_TRUECOLOR 0
#endif
#endif

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

static u32 pseudo_palette[17];
static struct fb_info fb_info;

static struct fb_var_screeninfo sfb_default = {
    FB_X_RES, FB_Y_RES, FB_X_VIRTUAL_RES, FB_Y_VIRTUAL_RES, 0, 0, FB_BPP, FB_IS_GREY,
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    0, FB_ACTIVATE_NOW, -1, -1, 0, 20000, 64, 64, 32, 32, 64, 2,
    0, FB_VMODE_NONINTERLACED, {0,0,0,0,0,0}
};

static struct fb_fix_screeninfo sfb_fix __initdata = {
	"Simple FB", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
	FB_VISUAL_TRUECOLOR, 1, 0, 0, 0 , (unsigned long) NULL, 0, FB_ACCEL_NONE
};

    /*
     *  Internal routines
     */
static int sfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int sfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);

static struct fb_ops sfb_ops = {
    owner:          THIS_MODULE,
    fb_check_var:   sfb_check_var,	
    fb_setcolreg:   sfb_setcolreg,
    fb_fillrect:    cfb_fillrect,
    fb_copyarea:    cfb_copyarea,
    fb_imageblit:   cfb_imageblit,
};

    /*
     *  Initialization
     */

int __init sfb_init(void)
{

    sfb_fix.smem_start     = VIDEORAM_BASE;
    sfb_fix.smem_len       = VIDEORAM_SIZE;

    fb_info.node           = -1;
    fb_info.fbops          = &sfb_ops;
    fb_info.flags          = FBINFO_FLAG_DEFAULT;
    fb_info.var            = sfb_default;
    fb_info.fix            = sfb_fix;
    fb_info.pseudo_palette = pseudo_palette;

    if (register_framebuffer(&fb_info) < 0)
	return -EINVAL;

    return 0;
}

   /*
    * We assume you can't change the graphcis resolution. 
    */		
static int sfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
   return -EINVAL;	 
}

#if FB_IS_TRUECOLOR
#define MAX_REGNO 15
#else
#define MAX_REGNO ((1 << FB_BPP) - 1)
#endif

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     *
     *  For paletted and greyscale modes, this just ignores the value
     *  being set in the color register.
     */

static int sfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
    if (regno > MAX_REGNO)
	return 1;

    red >>= 8;
    green >>= 8;
    blue >>= 8;

    switch (info->var.bits_per_pixel) {
	case 8:
 	    red   &= 0xe0;
	    green &= 0xe0;
	    blue  &= 0xc0;

	    ((u8 *)(info->pseudo_palette))[regno] =
		   (red       ) |
		   (green >> 3) |
		   (blue  >> 6);
	    break;
	case 15:
	case 16:
	    red   &= 0xf8;
	    green &= 0xfc;
	    blue  &= 0xf8;

	    ((u16 *)(info->pseudo_palette))[regno] =
		    (red   << 8) |
		    (green << 3) |
		    (blue  >> 3);
	    break;
	case 24:
	case 32:
	    ((u32 *)(info->pseudo_palette))[regno] =
		    (red   << 16) |
		    (green <<  8) |
		    (blue       );
	    break;
    }

    return 0;
}

static void __exit sfb_exit(void)
{
    unregister_framebuffer(&fb_info);
}

#ifdef MODULE
module_init(sfb_init);
module_exit(sfb_exit);
#endif

