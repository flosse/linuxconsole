/*
 * linux/drivers/video/tx3912fb.c
 *
 * Copyright (C) 1999 Harald Koerfgen
 * Copyright (C) 2001 Steven Hill (sjhill@realitydiluted.com)
 * Copyright (C) 2001 Dean Scott (dean@thestuff.net)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Framebuffer for LCD controller in TMPR3912/05 and PR31700 processors
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/fb.h>
#include <asm/bootinfo.h>
#include <asm/uaccess.h>
#include <linux/config.h>
#include "tx3912fb.h"

/*
 * Frame buffer, palette and console structures
 */
static struct fb_info fb_info;
static u32 pseudo_palette[17];

static struct fb_fix_screeninfo tx3912fb_fix __initdata = {
	TX3912FB_NAME, (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
	FB_VISUAL_TRUECOLOR, 0, 1, 1, 1, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static int tx3912fb_setcolreg(u_int regno, u_int red, u_int green,
			      u_int blue, u_int transp,
			      struct fb_info *info)
{
	if (regno > 255)
		return 1;

	red >>= 8;
	green >>= 8;
	blue >>= 8;

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

/*
 * Frame buffer operations structure used by console driver
 */
static struct fb_ops tx3912fb_ops = {
	owner:		THIS_MODULE,
	fb_setcolreg:	tx3912fb_setcolreg,
	fb_fillrect:	cfb_fillrect,
	fb_copyarea:	cfb_copyarea,
	fb_imageblit:	cfb_imageblit,
};

/*
 * Initialization of the framebuffer
 */
int __init tx3912fb_init(void)
{
	/* Stop the video logic when frame completes */
	VidCtrl1 |= ENFREEZEFRAME;
	IntClear1 |= INT1_LCDINT;
	while (!(IntStatus1 & INT1_LCDINT));

	/* Disable the video logic */
	VidCtrl1 &= ~(ENVID | DISPON);
	udelay(200);

	/* Set start address for DMA transfer */
	VidCtrl3 = tx3912fb_paddr &
	    (TX3912_VIDCTRL3_VIDBANK_MASK |
	     TX3912_VIDCTRL3_VIDBASEHI_MASK);

	/* Set end address for DMA transfer */
	VidCtrl4 = (tx3912fb_paddr + tx3912fb_size + 1) &
	    TX3912_VIDCTRL4_VIDBASELO_MASK;

	/* Set the pixel depth */
	switch (tx3912fb_info.bits_per_pixel) {
	    case 1:
		/* Monochrome */
		VidCtrl1 &= ~TX3912_VIDCTRL1_BITSEL_MASK;
		tx3912fb_fix.visual = FB_VISUAL_MONO10;
		break;
	    case 4:
		/* 4-bit gray */
		VidCtrl1 &= ~TX3912_VIDCTRL1_BITSEL_MASK;
		VidCtrl1 |= TX3912_VIDCTRL1_4BIT_GRAY;
		tx3912fb_fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	    case 8:
		/* 8-bit color */
		VidCtrl1 &= ~TX3912_VIDCTRL1_BITSEL_MASK;
		VidCtrl1 |= TX3912_VIDCTRL1_8BIT_COLOR;
		tx3912fb_fix.visual = FB_VISUAL_TRUECOLOR;
		break;
	    case 2:
	    default:
		/* 2-bit gray */
		VidCtrl1 &= ~TX3912_VIDCTRL1_BITSEL_MASK;
		VidCtrl1 |= TX3912_VIDCTRL1_2BIT_GRAY;
		tx3912fb_fix.visual = FB_VISUAL_PSEUDOCOLOR;
		break;
	}

	/* Unfreeze video logic and enable DF toggle */
	VidCtrl1 &= ~(ENFREEZEFRAME | DFMODE);
	udelay(200);

	/* Clear the framebuffer */
	memset((void *) tx3912fb_vaddr, 0xff, tx3912fb_size);
	udelay(200);

	/* Enable the video logic */
	VidCtrl1 |= (DISPON | ENVID);
	
	tx3912fb_fix.smem_start = tx3912fb_vaddr;
	tx3912fb_fix.smem_len   = tx3912fb_size;

	fb_info.node = -1;
	fb_info.fbops = &tx3912fb_ops;
	fb_info.flags = FBINFO_FLAG_DEFAULT;
	fb_info.var = tx3912fb_info;
	fb_info.fix = tx3912fb_fix;
	fb_info.pseudo_palette = pseudo_palette;

	if (register_framebuffer(&fb_info) < 0)
		return -EINVAL;

	return 0;
}

static void __exit tx3912fb_exit(void)
{
	unregister_framebuffer(&fb_info);
}

#ifdef MODULE
module_init(tx3912fb_init);
module_exit(tx3912fb_exit);

MODULE_LICENSE("GPL");
#endif
