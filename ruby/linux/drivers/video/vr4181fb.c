/*
 * linux/drivers/video/vr4181.c
 *
 * NEC VR4181 LCD Controller
 * 
 * Copyright (C) 1999 Bradley D. LaRonde.
 *
 * Based on sed1354 frame buffer by Bradley D. LaRonde.
 *
 * I've only done 8bpp for now (for example the hardware palette management),
 * but I've left the code for the other bpps in for when I (or someone else)
 * eventually does them.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <linux/config.h>
#include <asm/addrspace.h>
#include <asm/vr41xx.h>
#include <asm/uaccess.h>

// #define VR4181FB_DEBUG KERN_INFO

#define VR4181FB_XRES_MAX     320
#define VR4181FB_YRES_MAX     320
#define VR4181FB_BPP_MAX      8
#define VR4181FB_MEM_LEN_MAX  ((VR4181FB_XRES_MAX * VR4181FB_YRES_MAX * VR4181FB_BPP_MAX) / 8)

// VR4181FB_MEM_START should probably should be in video config options, since it depends on
// location of kernel data.
#define VR4181FB_MEM_START    (KSEG1 + 0x1000)

static struct fb_var_screeninfo vr4181fb_default = {
	0, 0, 0, 0,
	0, 0, 0, 0,
	{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
	0, FB_ACTIVATE_NOW, -1, -1, 0, 20000, 64, 64, 32, 32, 64, 2,
	0, FB_VMODE_NONINTERLACED, {0,0,0,0,0,0}
};

static struct fb_fix_screeninfo vr4181fb_fix __initdata = {
	"vr4181fb", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
	FB_VISUAL_PSEUDOCOLOR, 1, 1, 1, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static struct fb_info fb_info;
static u32 pseudo_palette[17];

static int vr4181fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	// Set a single color register. The values supplied are already
	// rounded down to the hardware's capabilities (according to the
	// entries in the var structure). Return != 0 for invalid regno.

	// So it says anyway.
	// From what I can tell, drivers/video/fbcmap.c passes in
	// un-rounded-down 16-bit values.
	
#ifdef VR4181FB_DEBUG
	printk(VR4181FB_DEBUG DEVICE_NAME ": scr %d %x/%x/%x\n",
		regno, red, green, blue);
#endif

	if (regno > 255)
		return 1;

	switch (info->var.bits_per_pixel) {
	    case 8:
		((u8 *)(info->pseudo_palette))[regno] =
		       ((red   & 0xe000) >>  8) |
		       ((green & 0xe000) >> 11) |
		       ((blue  & 0xc000) >> 14);
		break;
	}

	if (info->inverted_cmaps) {
		/* Invert intensities */
		red   = 0xffff - red;
		green = 0xffff - green;
		blue  = 0xffff - blue;
	}

	// Set the hardware palette register.
	*VR41XX_CPINDCTREG = regno;
	*VR41XX_CPALDATREG =
		((red & 0xfc00) << 2)
		| ((green & 0xfc00) >> 4)
		| ((blue & 0xfc00) >> 10);

	return 0;
}

static void vr4181fb_clock(int enable)
{
	if(enable)
		*VR41XX_LCDCTRLREG |= 0x0008;
	else
		*VR41XX_LCDCTRLREG &= ~0x0008;
}

static void vr4181fb_set_hardware(struct fb_var_screeninfo *var)
{
	int fblen;
	unsigned short m_signal, pre_scalar, color_depth, panel_color, panel_bus;

#ifdef VR4181FB_DEBUG
	printk(VR4181FB_DEBUG DEVICE_NAME ": vr4181fb_set_hardware %dx%d-%d@%d\n",
		var->xres, var->yres, var->bits_per_pixel, 0);
#endif

	// Set up LCD mode.
	*VR41XX_LCDGPMODE = 0x0;

	// Set fifo, turn clock off, and set clock polarities.
	*VR41XX_LCDCTRLREG = 0xe0;

	// No interrupts, clear interrupts.
	*VR41XX_LCDIMSKREG = 0x0;
	*VR41XX_LCDINRQREG = 0x0;

	// Set horizontal total and visible.
	*VR41XX_HRTOTALREG = (var->xres + 10) / 2;
	*VR41XX_HRVISIBREG = var->xres / 8;

	// Set load clock (LOCLK).
	*VR41XX_LDCLKSTREG = (var->xres - 2) / 2; 
	*VR41XX_LDCLKNDREG = (var->xres + 4) / 2;

	// Set vertical total and visible.
	*VR41XX_VRTOTALREG = var->yres; // maybe need a little extra here?
	*VR41XX_VRVISIBREG = var->yres;

	// Set frame clock (FLM).
	*VR41XX_FHSTARTREG = 0;
	*VR41XX_FVSTARTREG = 0;
	*VR41XX_FHENDREG =   (var->xres + 8) / 2;
	*VR41XX_FVENDREG =   0;

	// Set M signal, gclck pre-scalar, color depth, and panel type.
	m_signal = 0 << 8;
	pre_scalar = 0 << 4;
	color_depth = ((var->bits_per_pixel == 8) ? 3 : var->bits_per_pixel / 2) << 2;
	panel_color = ((var->grayscale || (var->bits_per_pixel == 1)) ? 1 : 0) << 1;
	panel_bus = (var->bits_per_pixel == 8) ? 1 : 0;
	*VR41XX_LCDCFGREG0 = m_signal | pre_scalar | color_depth | panel_color | panel_bus;

	// Set gclk to Hpck ratio.
	*VR41XX_LCDCFGREG1 = var->pixclock;

	// Set frame buffer start address.
	*VR41XX_FBSTAD1REG = VR4181FB_MEM_START & 0xffff;
	*VR41XX_FBSTAD2REG = (VR4181FB_MEM_START & 0xffff0000) >> 16;

	// Set frame buffer end address.
	fblen = var->yres * get_line_length(var->xres_virtual, var->bits_per_pixel);
	*VR41XX_FBNDAD1REG = (VR4181FB_MEM_START + fblen) & 0xffff;
	*VR41XX_FBNDAD2REG = ((VR4181FB_MEM_START + fblen) & 0xffff0000) >> 16;

	// Turn LCD clock on.
	vr4181fb_clock(1);

	// Turn LCD power on.
	display_power(1);

#ifdef VR4181FB_DEBUG
	printk(VR4181FB_DEBUG "LCD Controller and related registers:\n");
	printk(VR4181FB_DEBUG
		"HT   %.4x  HV   %.4x  LS   %.4x  LE   %.4x  VT   %.4x  VV   %.4x  FVS  %.4x  FVE  %.4x\n",
		*VR41XX_HRTOTALREG, *VR41XX_HRVISIBREG, *VR41XX_LDCLKSTREG, *VR41XX_LDCLKNDREG,
		*VR41XX_VRTOTALREG, *VR41XX_VRVISIBREG, *VR41XX_FVSTARTREG, *VR41XX_FVENDREG);
	printk(VR4181FB_DEBUG
		"LCTR %.4x  LIRQ %.4x  LCG0 %.4x  LCG1 %.4x  FBS1 %.4x  FBS2 %.4x  FBE1 %.4x  FBE2 %.4x\n",
 		*VR41XX_LCDCTRLREG, *VR41XX_LCDINRQREG, *VR41XX_LCDCFGREG0, *VR41XX_LCDCFGREG1,
		*VR41XX_FBSTAD1REG, *VR41XX_FBSTAD2REG, *VR41XX_FBNDAD1REG, *VR41XX_FBNDAD2REG);
	printk(VR4181FB_DEBUG
		"FHS  %.4x  FHE  %.4x  PCG1 %.4x  PCG2 %.4x  PC0S %.4x  PC0E %.4x  PC0H %.4x  PCSM %.4x\n",
		*VR41XX_FHSTARTREG, *VR41XX_FHENDREG,   *VR41XX_PWRCONREG1, *VR41XX_PWRCONREG2,
		*VR41XX_PCS0STRA,   *VR41XX_PCS0STPA,   *VR41XX_PCS0HIA,    *VR41XX_PCSMODE);
	printk(VR4181FB_DEBUG
		"LGPM %.4x  GPM1 %.4x\n",
                *VR41XX_LCDGPMODE,  *VR41XX_GPMD1REG);
#endif
}

static int vr4181fb_ioctl(struct inode *inode, struct file *file, u_int cmd,
	u_long arg, int con, struct fb_info *info)
{
	int value;

	switch(cmd) {
		case FBIOGET_BACKLIGHT:
			value = display_get_backlight();
			put_user(value, (int*)arg);
			return 0;

		case FBIOPUT_BACKLIGHT:
			display_set_backlight(arg);
			return 0;

		case FBIOGET_CONTRAST:
			value = display_get_contrast();
			put_user(value, (int*)arg);
			return 0;

		case FBIOPUT_CONTRAST:
			display_set_contrast(arg);
			return 0;

#ifdef VR4181FB_DEBUG
		// bdl:  Hack to mess with LCD timings.
		// Should be moved to set_var I think.
		case 0:
			display_power(0);
			vr4181fb_clock(0);

			*VR41XX_LCDCFGREG0 = (unsigned short)arg;

			vr4181fb_clock(1);
			display_power(1);
			return 0;

		case 1:
			display_power(0);
			vr4181fb_clock(0);

			*VR41XX_LCDCFGREG1 = (unsigned short)arg;

			vr4181fb_clock(1);
			display_power(1);
			return 0;
#endif
	}

	return -EINVAL;
}

static void vr4181fbcon_blank(int blank, struct fb_info *info)
{
#ifdef VR4181FB_DEBUG
	printk(VR4181FB_DEBUG DEVICE_NAME ": fbcon_blank blank %d info %p\n",
		blank, info);
#endif
	if(blank)
		display_power(0);
	else
		display_power(1);
}

#ifdef CONFIG_PM
static unsigned short hw[21];

static void vr4181fb_hw_save(void)
{
	int i = 0;
	hw[i++] = *VR41XX_LCDGPMODE;
	hw[i++] = *VR41XX_LCDIMSKREG;
	hw[i++] = *VR41XX_LCDINRQREG;
	hw[i++] = *VR41XX_HRTOTALREG;
	hw[i++] = *VR41XX_HRVISIBREG;
	hw[i++] = *VR41XX_LDCLKSTREG; 
	hw[i++] = *VR41XX_LDCLKNDREG;
	hw[i++] = *VR41XX_VRTOTALREG;
	hw[i++] = *VR41XX_VRVISIBREG;
	hw[i++] = *VR41XX_FHSTARTREG;
	hw[i++] = *VR41XX_FVSTARTREG;
	hw[i++] = *VR41XX_FHENDREG;
	hw[i++] = *VR41XX_FVENDREG;
	hw[i++] = *VR41XX_LCDCFGREG0;
	hw[i++] = *VR41XX_LCDCFGREG1;
	hw[i++] = *VR41XX_FBSTAD1REG;
	hw[i++] = *VR41XX_FBSTAD2REG;
	hw[i++] = *VR41XX_FBNDAD1REG;
	hw[i++] = *VR41XX_FBNDAD2REG;
	hw[i++] = *VR41XX_LCDCTRLREG;
	hw[i++] = *VR41XX_PWRCONREG1;
	hw[i++] = *VR41XX_PWRCONREG2;
}

static void vr4181fb_hw_restore(void)
{
	int i = 0;
	*VR41XX_LCDGPMODE =  hw[i++];
	*VR41XX_LCDIMSKREG = hw[i++];
	*VR41XX_LCDINRQREG = hw[i++];
	*VR41XX_HRTOTALREG = hw[i++];
	*VR41XX_HRVISIBREG = hw[i++];
	*VR41XX_LDCLKSTREG = hw[i++];
	*VR41XX_LDCLKNDREG = hw[i++];
	*VR41XX_VRTOTALREG = hw[i++];
	*VR41XX_VRVISIBREG = hw[i++];
	*VR41XX_FHSTARTREG = hw[i++];
	*VR41XX_FVSTARTREG = hw[i++];
	*VR41XX_FHENDREG =   hw[i++];
	*VR41XX_FVENDREG =   hw[i++];
	*VR41XX_LCDCFGREG0 = hw[i++];
	*VR41XX_LCDCFGREG1 = hw[i++];
	*VR41XX_FBSTAD1REG = hw[i++];
	*VR41XX_FBSTAD2REG = hw[i++];
	*VR41XX_FBNDAD1REG = hw[i++];
	*VR41XX_FBNDAD2REG = hw[i++];
	*VR41XX_LCDCTRLREG = hw[i++];
	*VR41XX_PWRCONREG1 = hw[i++];
	*VR41XX_PWRCONREG2 = hw[i++];
}

#define VR4181_PALETTE_ENTRIES   0x100
static unsigned long palette_cpindctreg_backup;
static unsigned long palette_data_backup[VR4181_PALETTE_ENTRIES];

static void vr4181fb_palette_save(void)
{
	int i;
	palette_cpindctreg_backup = *VR41XX_CPINDCTREG;
	*VR41XX_CPINDCTREG = 0x0300;
	for(i=0; i < VR4181_PALETTE_ENTRIES; i++)
		palette_data_backup[i] = *VR41XX_CPALDATREG;
}

static void vr4181fb_palette_restore(void)
{
	int i;
	*VR41XX_CPINDCTREG = 0x0300;
	for(i=0; i < VR4181_PALETTE_ENTRIES; i++)
		*VR41XX_CPALDATREG = palette_data_backup[i];
	*VR41XX_CPINDCTREG = palette_cpindctreg_backup;
}

static int vr4181fb_pm_request(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	static int contrast_save;

	switch (rqst) {
	case PM_SUSPEND:
#ifdef VR4181FB_DEBUG
		printk(VR4181FB_DEBUG DEVICE_NAME ": powering off\n");
#endif
		// Try to do this in the least confusing way.

		// NOTE: it seems that this particular code, though only
		// slightly modified from the previous, cures the
		// "won't-wake-from-suspend" problem on the Agenda VR3.
		// Or maybe it was some small change in the resume code
		// below that cured it?  bdl

		// First, save the current state.
		contrast_save = display_get_contrast();
		vr4181fb_palette_save();
		vr4181fb_hw_save();
		// Next, initiate the LCD power-down sequence.
		display_power(0);
		// Wait long enough for the LCD controller to finish the power-down sequence.
		// mdelay(200);
		// Finally, turn off the LCD clock.
		vr4181fb_clock(0);
		break;

	case PM_RESUME:
#ifdef VR4181FB_DEBUG
		printk(VR4181FB_DEBUG DEVICE_NAME ": powering on\n");
#endif
		// First, restore the hardware settings before turning on the clock.
		vr4181fb_hw_restore();
		// Turn on the clock to be able to restore the palette.
		vr4181fb_clock(1);
		// Restore the palette.
		vr4181fb_palette_restore();
		// Restore the contrast.
		display_set_contrast(contrast_save);
		// Initiate the LCD power-on sequence.
		display_power(1);
		break;
	}
	return 0;
}

static int __init vr4181fb_pm_init(void)
{
#ifdef VR4181FB_DEBUG
	printk(VR4181FB_DEBUG DEVICE_NAME ": pm_init\n");
#endif
	pm_register(PM_SYS_DEV, PM_SYS_UNKNOWN, vr4181fb_pm_request);
	return 0;
}

__initcall(vr4181fb_pm_init);
#endif

static struct fb_ops vr4181fb_ops = {
	owner: 		THIS_MODULE,
	fb_ioctl: 	vr4181fb_ioctl,
	fb_setcolreg: 	vr4181fb_setcolreg,
	fb_fillrect: 	cfb_fillrect,
	fb_copyarea: 	cfb_copyarea,
	fb_imageblit: 	cfb_imageblit,
};

int __init vr4181fb_init(void)
{
#ifdef VR4181FB_DEBUG
	printk(VR4181FB_DEBUG DEVICE_NAME ": initializing\n");
#endif

	vr4181fb_fix.smem_start = VR4181FB_MEM_START;
	vr4181fb_fix.smem_len   = VR4181FB_MEM_LEN_MAX;
	
	fb_info.node           = -1;
	fb_info.fbops          = &vr4181fb_ops;
	fb_info.blank          = &vr4181fbcon_blank;
	fb_info.flags          = FBINFO_FLAG_DEFAULT;
	fb_info.fix            = vr4181fb_fix;
	fb_info.var            = vr4181fb_default;
	fb_info.pseudo_palette = pseudo_palette;

	vr4181fb_set_hardware(fb_info.var);

	switch (fb_info.var.bits_per_pixel) {
	    case 1:
	    	fb_info.fix.visual = FB_VISUAL_MONO01;
		break;
	}

	if (register_framebuffer(&fb_info) < 0) {
		return -EINVAL;
	}

	return 0;
}

static void __exit vr4181fb_exit(void)
{
	unregister_framebuffer(&fb_info);
}

#ifdef MODULE
module_init(vr4181fb_init);
module_exit(vr4181fb_exit);
#endif

