/*
 *  linux/drivers/video/r3912fb.c -- R3912 embedded frame buffer device
 *
 *	 Based on simple frame buffer which was
 *	 based on virtual frame buffer by Geert Uytterhoeven
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
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/config.h>
#include <asm/bootinfo.h>
#include <asm/r39xx.h>
#include <asm/uaccess.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>
#include <video/fbcon-mac.h>

/*
 * This can go back into a header file if/when more platforms are defined.
 * For now, it's just Mobilon parameters.
 *
 * VIDEORAM_SIZE     Normally x_res * y_res * (bpp/8) 
 * FB_X_RES          Visible horizontal resolution in pixels
 * FB_X_VIRTUAL_RES  Horizontal resolution of framebuffer memory in pixels
 *                   (default is FB_X_RES)
 * FB_Y_RES          Visible vertical resolution in pixels
 * FB_Y_VIRTUAL_RES  Vertical resolution of framebuffer memory in pixels
 *                   (default FB_Y_RES)
 * FB_BPP            Bits per pixel
 * FB_IS_GREY        1 means greyscale device, otherwise 0
 *
 * FIXME: this has to be set up depending on size and color depth
 */

#ifdef CONFIG_PHILIPS_VELO
#define FB_X_RES       480
#define FB_Y_RES       240
#ifdef CONFIG_PHILIPS_VELO1_4GRAY
#define FB_BPP         2
#else
#ifdef CONFIG_PHILIPS_VELO1_16GRAY
#define FB_BPP	       4
#else
#error velo500 owner, please fix this
#endif
#endif
#define FB_IS_GREY     1
#define VIDEORAM_SIZE  (FB_X_RES * FB_Y_RES * FB_BPP / 8)

#elif defined(CONFIG_VTECH_HELIO)
#define FB_X_RES       160
#define FB_Y_RES       160
#ifdef CONFIG_FB_R3912_FORCE_MONO
#define FB_BPP 1
#else
#define FB_BPP         4
#endif
#define FB_IS_GREY     1
#define VIDEORAM_SIZE  (FB_X_RES * FB_Y_RES * FB_BPP / 8)

 #define LCD_CLK_36M    0
 #define LCD_CLK_18M    1
 #define LCD_CLK_9M     2
 #define LCD_CLK_4M     3

 #define VIDEO_RF       LCD_CLK_9M
 #define LCD_CLK        (36864000 / (1 << VIDEO_RF))
 #define LCD_BAUD       0x6               /* for 4.5MHz Lcd Clock only */
 #define LCD_CP         (LCD_CLK / (LCD_BAUD * 2 + 2))

 #define LCD_FRAMERATE  70
 #define LCD_LINERATE   (70 * (FB_X_RES + 1))
 #define LCD_VIDRATE    ((LCD_CP / LCD_LINERATE) - 1)

#else
#define FB_X_RES       640
#define FB_Y_RES       240
#define FB_BPP         8
#define FB_IS_GREY     0
#define VIDEORAM_SIZE  (FB_X_RES * FB_Y_RES * FB_BPP / 8)
#endif

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))

u_long videomemory, videomemorysize = VIDEORAM_SIZE;

static struct fb_info fb_info;
static u32 pseudo_palette[17];

#ifndef FB_X_VIRTUAL_RES
#define FB_X_VIRTUAL_RES FB_X_RES
#endif

#ifndef FB_Y_VIRTUAL_RES
#define FB_Y_VIRTUAL_RES FB_Y_RES
#endif

static struct fb_var_screeninfo r3912fb_default = {
    FB_X_RES, FB_Y_RES, FB_X_VIRTUAL_RES, FB_Y_VIRTUAL_RES, 0, 0, FB_BPP, FB_IS_GREY,
    {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0},
    0, FB_ACTIVATE_NOW, -1, -1, 0, 20000, 64, 64, 32, 32, 64, 2,
    0, FB_VMODE_NONINTERLACED, {0,0,0,0,0,0}
};

static struct fb_fix_screeninfo r3912fb_fix __initdata = {
	"R3912 FB", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
	FB_VISUAL_TRUECOLOR, 1, 1, 1, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};


/*
 * Helio specific LCD control
 */

#if defined(CONFIG_VTECH_HELIO)

void turn_on_helio_lcd(void)
{
    VidCtrl2 = ((LCD_VIDRATE << VIDRATE_SHIFT) & VIDRATE_MASK) |
	    (((FB_X_RES / 4 - 1) << HORZVAL_SHIFT) & HORZVAL_MASK) |
	    ((FB_Y_RES - 1) & LINEVAL_MASK);

    VidCtrl1 |= ENVID;		/* Enable Video Logic */
    VidCtrl1 |= DISPON;		/* Display on, and enable video logic */
    VidCtrl1 &= ~INVVID;	/* Inverted off */

    ClockControl |= CLK_ENVIDCLK;

    MFIOSelect    |= (MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);
    MFIODirection |= (MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);
    MFIOOutput    |=  MFIO_PIN_LCD_POWER;
    MFIOOutput    &= ~MFIO_PIN_BACKLIGHT;
    MFIOPowerDown &= ~(MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);
}

/* return previous video mode */
int turn_off_helio_lcd(void)
{
    /* Are we already off? Return immediate since the rest
     * of the function will wait indefinitly otherwise. */
    if (!(VidCtrl1 & ENVID))
	return 0;

#ifndef CONFIG_VTECH_HELIO_EMULATOR
    /* Freeze after end of frame */
    VidCtrl1 |= ENFREEZEFRAME;
    IntClear1 |= INT1_LCDINT;
    /*
     * Wait for end of frame: Poll Interrupt Status Register 1
     * (Barf! PM2000)
     */
    while (!(IntStatus1 & INT1_LCDINT))
	/*empty*/;
#endif

    VidCtrl1 &= ~ENVID;         /* Disable Video Logic */
    ClockControl &= ~CLK_ENVIDCLK;
    VidCtrl1 &= ~(DISPON | INVVID);  /* Display off, Invert off */

    MFIOSelect    |=  (MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);
    MFIODirection |=  (MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);
    MFIOOutput    &= ~(MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);
    MFIOPowerDown |=  (MFIO_PIN_LCD_POWER | MFIO_PIN_BACKLIGHT);

    VidCtrl1 &= ~ENFREEZEFRAME;
    return 1;
}

#ifdef CONFIG_PM
struct pm_dev* pmdev;

static
int pm_request(struct pm_dev* dev, pm_request_t req, void* data)
{
	static int state;

	switch (req) {
	 case PM_SUSPEND:
		state = turn_off_helio_lcd();
		break;
	 case PM_RESUME:
		if (state) turn_on_helio_lcd();
		else turn_off_helio_lcd();
		break;
	}
	return 0;
}

#endif

#endif /* CONFIG_VTECH_HELIO */


    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int r3912fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
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

    /*
     *  We don't support changing the graphics resolution
     */
static int r3912fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    return -EINVAL;
}

static struct fb_ops r3912fb_ops = {
    owner:	   THIS_MODULE,
    fb_check_var:  r3912fb_check_var,
    fb_setcolreg:  r3912fb_setcolreg,
    fb_fillrect:   cfb_fillrect,
    fb_copyarea:   cfb_copyarea,
    fb_imageblit:  cfb_imageblit,
};

    /*
     *  Initialization
     */

int __init r3912fb_init(void)
{
#if defined(CONFIG_SHARP_MOBILON) || defined(CONFIG_COMPAQ) || defined(CONFIG_PHILIPS_VELO)
    /*
     * TODO: check VidCtrl1 for monochrome/greyscale LCDs
     * and set up fb_info and friends accordingly
     */

#if 0
    if (!(VidCtrl1 & DISP8))
	return -EINVAL;
#endif

    /*
     * videomemory has been set in arch/mips/r39xx/prom/memory.c
     */
    /*
     * Stop video at end of frame
     */
#ifndef CONFIG_VTECH_HELIO_EMULATOR
    VidCtrl1 |= ENFREEZEFRAME;
    IntClear1 |= INT1_LCDINT;
    /*
     * Wait for end of frame: Poll Interrupt Status Register 1
     */
    while (!(IntStatus1 & INT1_LCDINT))
            /*empty*/;
#endif

    VidCtrl1 &= ~(DISPON | ENVID);
    udelay(100);
    VidCtrl3 = PHYSADDR(videomemory);
    /*
     * As a consequence of this DFVAL and FRAMEMASKVAL are set to 0.
     * This may not work on all R3912/Poseidons.
     */
    VidCtrl4 = PHYSADDR(videomemory + videomemorysize + 1) & VIDBASELO_MASK;
    VidCtrl1 &= ~(DFMODE | ENFREEZEFRAME);
    udelay(100);
    VidCtrl1 |= DISPON;
    VidCtrl1 |= ENVID;

#elif defined(CONFIG_VTECH_HELIO)

    VidCtrl1 = (0x6 << BAUDVAL_SHIFT) |   /* 4.5MHz LCD Clock */
	    (0x4 << VIDDONEVAL_SHIFT) |   /* Delay for VIDDONE signal */
#ifdef CONFIG_FB_R3912_FORCE_MONO
	    (0x0 << BITSEL_SHIFT) |       /* 1bpp monochrome mode */
#else
	    (0x2 << BITSEL_SHIFT) |       /* 4bpp greyscale mode */
#endif
	    DISPON;             /* Display on, but disable video logic */

    VidCtrl2 = ((LCD_VIDRATE << VIDRATE_SHIFT) & VIDRATE_MASK) |
	    (((FB_X_RES / 4 - 1) << HORZVAL_SHIFT) & HORZVAL_MASK) |
	    ((FB_Y_RES - 1) & LINEVAL_MASK);

    VidCtrl3 = PHYSADDR(videomemory);
    /*
     * As a consequence of this DFVAL and FRAMEMASKVAL are set to 0.
     * This may not work on all R3912/Poseidons.
     */
    VidCtrl4 = PHYSADDR(videomemory + videomemorysize + 1) & VIDBASELO_MASK;

    /* Setup LUT tables */
    VidCtrl7 = 0xFA50;
    VidCtrl8 = 0x7DA;
    VidCtrl9 = 0x7DBEA5A5;
    VidCtrl10 = 0x7DFBE;
    VidCtrl11 = 0x7A5AD;
    VidCtrl12 = 0xFBFDFE7;
    VidCtrl13 = 0x7B5ADEF;
    VidCtrl14 = 0xB9DC663;

    turn_on_helio_lcd();

#endif 

    r3912fb_fix.smem_start = videomemory;
    r3912fb_fix.smem_len   = videomemorysize;

    fb_info.node           = -1;
    fb_info.fbops          = &r3912fb_ops;
    fb_info.flags          = FBINFO_FLAG_DEFAULT;
    fb_info.var            = r3912fb_default;
    fb_info.fix            = r3912fb_fix;
    fb_info.pseudo_palette = pseudo_palette;
    
    switch (fb_info.var.bits_per_pixel) {
    	case 1:
	    fb_info.fix.visual = FB_VISUAL_MONO01;
	    break;
	case 2:
	case 4:
	    fb_info.fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
	    break;
    }

    if (register_framebuffer(&fb_info) < 0)
	return -EINVAL;

#ifdef CONFIG_PM
    pmdev = pm_register(PM_SYS_DEV,PM_SYS_VGA,pm_request);
    if (!pmdev)
	    printk(KERN_INFO "fb%d: unable to register by PM\n",
		 GET_FB_IDX(fb_info.node));
#endif

    return 0;
}

static void __exit r3912fb_exit(void)
{
#ifdef CONFIG_PM
	if (pmdev)
		pm_unregister_device(pmdev);
#endif

    unregister_framebuffer(&fb_info);
}

#ifdef MODULE
module_init(r3912fb_init);
module_exit(r3912fb_exit);
#endif

