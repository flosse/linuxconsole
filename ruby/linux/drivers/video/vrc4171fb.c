/*
 *  linux/drivers/video/vrc4171fb.c
 *
 *  NEC VRC4171(A) LCD controller frame buffer device
 *
 *  For now, this is a crude hack of SFB, which in turn was a crude hack of
 *  virtual frame buffer by Geert Uytterhoeven.  In my opinion, it really
 *  ought to be rewritten from scratch.
 *
 *  Further hackiness to work under new API. Most definately should be
 *  rewritten from scratch. -Lethal
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
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/config.h>

#ifndef CONFIG_CPU_VR41XX
#error "Trying to use a VRC4171 chip without a VR41xx CPU???"
#endif

#include <asm/vrc4171.h>
/* This is a fairly broken thing to do, but we're currently hardcoding the
frame buffer params based on platform: */
#include <asm/vr41xx-platdep.h>

/*
 * VIDEORAM_SIZE     Size of video ram (default is x * y * (bpp/8)) 
 * FB_X_RES          Visible horizontal resolution in pixels
 * FB_X_VIRTUAL_RES  Horizontal resolution of framebuffer memory in pixels
 *                   (default is FB_X_RES)
 * FB_Y_RES          Visible vertical resolution in pixels
 * FB_Y_VIRTUAL_RES  Vertical resolution of framebuffer memory in pixels
 *                   (default FB_Y_RES)
 * FB_BPP            Bits per pixel
 * FB_IS_GREY        1 means greyscale device, otherwise 0 (default is 0)
 *
 * The last three are done with 1 / 0 instead of #define / #undef in order
 * to facilitate a generic interface, if we should coose to do one in the
 * future.
 *
 * Some example settings:
 *
 * #define VIDEORAM_SIZE	(1024 * 1024)
 * #define FB_X_RES		800
 * #define FB_X_VIRTUAL_RES	1024
 * #define FB_Y_RES		600
 * #define FB_Y_VIRTUAL_RES	1024
 * #define FB_BPP		8
 * #define FB_IS_GREY		0
 */

/*
 * Please put all hardware-specific defines (or includes) above this line, the
 * frame buffer should be completely described by above parameters.
 */

#ifndef FB_X_RES
#error "Frame buffer parameters have not been defined."
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

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

const u_long videomemory = VRC4171_LCD_BASE, videomemorysize = VIDEORAM_SIZE;

static struct fb_info fb_info;
static u32 pseudo_palette[17];

static char vrc4171_name[16] = "VRC4171 FB";

static struct fb_var_screeninfo vrc4171_default = {
    FB_X_RES, FB_Y_RES, FB_X_VIRTUAL_RES, FB_Y_VIRTUAL_RES, 0, 0, FB_BPP, FB_IS_GREY,
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    0, FB_ACTIVATE_NOW, -1, -1, 0, 20000, 64, 64, 32, 32, 64, 2,
    0, FB_VMODE_NONINTERLACED, {0,0,0,0,0,0}
};

static struct fb_fix_screeninfo vrc4171_fix __initdata = {
    "VRC4171", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_PSEUDOCOLOR, 1, 1, 1, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

    /*
     *  Internal routines
     */

static int vrc4171_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);

static struct fb_ops vrc4171_ops = {
    owner:          THIS_MODULE,
    fb_setcolreg:   vrc4171_setcolreg,
    fb_fillrect:    cfb_fillrect,
    fb_copyarea:    cfb_copyarea,
    fb_imageblit:   cfb_imageblit,
};

    /*
     *  Initialization
     */

int __init vrc4171fb_init(void)
{
    u16 cfg2id = *VRC4171_Config2 >> 12;

    if (cfg2id == 0 || cfg2id > 2) {
	printk(KERN_WARNING "VRC4171(A) device not found\n");
	return -1;
    }

// OK, this is bad.  We should really set up all the registers, and for all
// BPP.  For now, we rely on hardware already being set up.
// (This is a hack to make Everex Freestyle go into 16-grey mode)
// MFK: this should also probably be in set_var
#if (FB_BPP == 4)
    *VRC4171_LCDPanelCtl = 0xf002;
    *VRC4171_Offset = FB_X_VIRTUAL_RES * FB_BPP / 8 / 8;
#endif

    fb_info.changevar = NULL;
    fb_info.node = -1;
    fb_info.var = vrc4171_default;
    fb_info.fix = vrc4171_fix;
    fb_info.fbops = &vrc4171_ops;
    fb_info.flags = FBINFO_FLAG_DEFAULT;
    fb_info.pseudo_palette = pseudo_palette;

    switch (fb_info.var.bits_per_pixel) {
    	case 1:
	    fb_info.fix.visual = FB_VISUAL_MONO01;
	    break;
	case 16:
	    fb_info.fix.visual = FB_VISUAL_TRUECOLOR;
	    break;
    }

    if (register_framebuffer(&fb_info) < 0) {
	return -EINVAL;
    }

    return 0;
}

#if (FB_BPP == 16)
#define MAX_REGNO 15
#else
#define MAX_REGNO ((1 << FB_BPP) - 1)
#endif

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int vrc4171_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
    if (regno > MAX_REGNO)
	return 1;

    /*
     *  Round color down to what the video mode can actually do, so if the
     *  palette is read back, the actual hardware value is returned.  Also, 
     *  maintain index to RGB map for fbcon (accessed through dispsw_data)
     */

    switch (info->var.bits_per_pixel) {
    	case 1:
	    unsigned int grey;

	    /* FIXME: Should be weighted */
	    grey = ((red + green + blue + 1) / 3) >> 8;

	    info->pseudo_palette[regno] = (grey | grey | grey);

	    // shouldn't this look at FB_IS_INVERSE ?
	    grey = 0x3f - (grey * 0x3f + 128) / 255;
	    *VRC4171_RAMWriteAddress = regno;
	    barrier();
	    *VRC4171_RAMWritePort0 = grey | (grey << 8);
	    *VRC4171_RAMWritePort1 = grey | (grey << 8);
	    break;
	case 2:
	case 4:
	case 8:
	    red   = (red   >> 8) & 0xfc;
	    green = (green >> 8) & 0xfc;
	    blue  = (blue  >> 8) & 0xfc;
	    
	    *VRC4171_RAMWriteAddress = regno;
	    barrier();
	    *VRC4171_RAMWritePort0 = (red << 6) | (green >> 2);
	    *VRC4171_RAMWritePort1 = blue >> 2;

	    ((u8 *)(info->pseudo_palette))[regno] =
	    	   (red | green | blue);
	    break;
    	case 16:
	    red   = (red   >> 8) & 0xf8;
	    green = (green >> 8) & 0xfc;
	    blue  = (blue  >> 8) & 0xf8;

    	    ((u16 *)(info->pseudo_palette))[regno] =
		    (red   << 8) |
		    (green << 3) |
		    (blue  >> 3);
	    break;
    }

    return 0;
}

static void __exit vrc4171fb_exit(void)
{
    unregister_framebuffer(&fb_info);
}

#ifdef MODULE
module_init(vrc4171fb_init);
module_exit(vrc4171fb_exit);
#endif

