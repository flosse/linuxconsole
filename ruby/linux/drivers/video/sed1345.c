/*
 *  linux/drivers/video/sed1345fb.c
 *
 *  Epson SED1345 Color Graphics LCD/CRT Controller
 * 
 *  Copyright (C) 1999 Bradley D. LaRonde.
 *
 *  Based on simple frame buffer by Mike Klar, which was
 *  Based on virtual frame buffer by Geert Uytterhoeven.
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/config.h>
#include <asm/addrspace.h>

#ifdef CONFIG_VADEM_CLIO_1000
#include <asm/vr41xx.h>
#define FB_X_RES	640
#define FB_Y_RES	480
#define FB_BPP		8
#define FB_IS_GREY	0
#define VIDEORAM_BASE   (KSEG1 + VR41XX_LCD)
#else
#error "Fix me for non-Clio"
#endif

#ifndef VIDEORAM_SIZE
  #define VIDEORAM_SIZE (FB_X_RES * FB_Y_RES * FB_BPP / 8)
#endif

static struct fb_info fb_info;
static u32 pseudo_palette[17];

static struct fb_var_screeninfo sed1345fb_default = {
	FB_X_RES, FB_Y_RES, FB_X_RES, FB_Y_RES, 0, 0, FB_BPP, FB_IS_GREY,
	{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
	0, FB_ACTIVATE_NOW, -1, -1, 0, 20000, 64, 64, 32, 32, 64, 2,
	0, FB_VMODE_NONINTERLACED, {0,0,0,0,0,0}
};

static struct fb_fix_screeninfo sed1345fb_fix __initdata = {
	"SED1345", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
	FB_VISUAL_TRUECOLOR, 1, 1, 1, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static int sed1345fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			       u_int transp, struct fb_info *info)
{
	// Set a single color register. The values supplied are already
	// rounded down to the hardware's capabilities (according to the
	// entries in the var structure). Return != 0 for invalid regno.

	// So it says anyway.
	// From what I can tell, drivers/video/fbcmap.c passes in
	// un-rounded-down 16-bit values.
	
	if (regno > 255)
		return 1;

	red   >>= 8;
	green >>= 8;
	blue  >>= 8;

	switch (info->var.bits_per_pixel) {
	    case 8:
	    	red   &= 0xe000;
		green &= 0xe000;
		blue  &= 0xc000;
		
	    	((u8 *)(info->pseudo_palette))[regno] =
		       (red   >>  8) |
		       (green >> 11) |
		       (blue  >> 14);
		break;
	}

	return 0;
}

static struct fb_ops sed1345fb_ops = {
    owner:          THIS_MODULE,
    fb_setcolreg:   sed1345fb_setcolreg,
    fb_fillrect:    cfb_fillrect,
    fb_copyarea:    cfb_copyarea,
    fb_imageblit:   cfb_imageblit,
};

static void __init set_recommended_lut_values(void)
{
	// these are the 8 bpp LUT values recommended by Epson
	unsigned char lutval[] = {
		0x00, 0x00, 0x00,
		0x03, 0x03, 0x05,
		0x05, 0x05, 0x0a,
		0x07, 0x07, 0x0f,
		0x09, 0x09, 0x00,
		0x0b, 0x0b, 0x00,
		0x0d, 0x0d, 0x00,
		0x0f, 0x0f, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
		0x00, 0x00, 0x00,
	};

	int i;

#ifdef CONFIG_VADEM_CLIO_1000
	const unsigned int reg_base = VIDEORAM_BASE + 0x200000;
#endif

	// set up to write lut entries auto-increment from 0
	*((volatile unsigned char*)reg_base + 0x24) = 0x00;

	barrier();

	// write lut entries
	for ( i = 0; i < sizeof(lutval); i++ )
		*((volatile unsigned char*)reg_base + 0x26) = lutval[i];

	// select bank 0 (only affects output)
	*((volatile unsigned char*)reg_base + 0x27) = 0x00;

	printk("LUTs set to recommended values.\n");
}

int __init sed1345_init(void)
{
	sed1345fb_fix.smem_start = VIDEORAM_BASE;
	sed1345fb_fix.smem_len   = VIDEORAM_SIZE;

	fb_info.node           = -1;
	fb_info.fbops          = &sed1345fb_ops;
	fb_info.flags          = FBINFO_FLAG_DEFAULT;
	fb_info.var            = sed1345fb_default;
	fb_info.fix            = sed1345fb_fix;
	fb_info.pseudo_palette = pseudo_palette;
	
	set_recommended_lut_values();

	switch (fb_info.var.bits_per_pixel) {
	    case 1:
	    	fb_info.fix.visual = FB_VISUAL_MONO01;
		break;
	    case 2:
	    case 4:
	    	fb_info.fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	if (register_framebuffer(&fb_info) < 0) {
		return -EINVAL;
	}

	return 0;
}

static void __exit sed1345_exit(void)
{
	unregister_framebuffer(&fb_info);
}

#ifdef MODULE
module_init(sed1345_init);
module_exit(sed1345_exit);

MODULE_LICENSE("GPL");
#endif

