/*
 *	HP300 Topcat framebuffer support (derived from macfb of all things)
 *	Phil Blundell <philb@gnu.org> 1998
 * 
 * Should this be moved to drivers/dio/video/ ? -- Peter Maydell
 * No! -- Jes
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/fb.h>
#include <linux/dio.h>
#include <asm/io.h>
#include <asm/blinken.h>
#include <asm/hwtest.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>

#define arraysize(x)    (sizeof(x)/sizeof(*(x)))

static struct display disp;
static struct fb_info fb_info;

unsigned long fb_start;
unsigned long fb_regs;
unsigned char fb_bitmask;

#define TC_WEN		0x4088
#define TC_REN		0x408c
#define TC_FBEN		0x4090
#define TC_NBLANK	0x4080

/* blitter regs */
#define BUSY		0x4044
#define WMRR		0x40ef
#define SOURCE_X	0x40f2
#define SOURCE_Y	0x40f6
#define DEST_X		0x40fa
#define DEST_Y		0x40fe
#define WHEIGHT		0x4106
#define WWIDTH		0x4102
#define WMOVE		0x409c

static int currcon = 0;

static struct fb_fix_screeninfo hpfb_fix __initdata = {
    "HP300 Topcat", (unsigned long) NULL, 1024*768, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_PSEUDOCOLOR, 0, 0, 0, 1024, (unsigned long) NULL, 0, FB_ACCEL_NONE};

static struct fb_var_screeninfo hpfb_var = {
	1024,768,1024,768,	/* W,H, W, H (virtual) load xres,xres_virtual*/
	0,0,			/* virtual -> visible no offset */
	8,			/* depth -> load bits_per_pixel */
	0,			/* greyscale ? */
	{0,2,0},		/* R */
	{0,2,0},		/* G */
	{0,2,0},		/* B */
	{0,0,0},		/* transparency */
	0,			/* standard pixel format */
	FB_ACTIVATE_NOW,
	274,195,		/* 14" monitor */
	FB_ACCEL_NONE,
	0L,0L,0L,0L,0L,
	0L,0L,0,		/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};

struct hpfb_par
{
};

/* frame buffer operations */
int hpfb_init(void);

static int hpfb_open(struct fb_info *info, int user);
static int hpfb_release(struct fb_info *info, int user);
static int hpfb_set_var(struct fb_var_screeninfo *var, int con,
                        struct fb_info *info);
static int hpfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                          u_int transp, struct fb_info *info);

    /*
     *  Interface to the low level console driver
     */

static int hpfb_switch(int con, struct fb_info *info);
static int hpfb_update_var(int con, struct fb_info *info);
		
static struct fb_ops hpfb_ops = {
        fb_open:        hpfb_open,
        fb_release:     hpfb_release,
        fb_get_fix:     fbgen_get_fix,
        fb_get_var:     fbgen_get_var,
        fb_set_var:     hpfb_set_var,
        fb_get_cmap:    fbgen_get_cmap,
        fb_set_cmap:    fbgen_set_cmap,
	fb_setcolreg:	hpfb_setcolreg
};

static int hpfb_open(struct fb_info *info, int user)
{
        /*
         * Nothing, only a usage count for the moment
         */
        MOD_INC_USE_COUNT;
        return(0);
}

static int hpfb_release(struct fb_info *info, int user)
{
        MOD_DEC_USE_COUNT;
        return(0);
}

static int hpfb_set_var(struct fb_var_screeninfo *var, int con,
                         struct fb_info *info)
{
	return -EINVAL;        
}

/*
 * Set the palette.  This may not work on all boards but only experimentation
 * will tell. XXX Doesn't work at all.
 */

static int hpfb_setcolreg(unsigned regno, unsigned red, unsigned green,
                           unsigned blue, unsigned transp,
                           struct fb_info *info)
{
	while (readw(fb_regs + 0x6002) & 0x4) udelay(1);
        writew(0, fb_regs + 0x60f0);
        writew(regno, fb_regs + 0x60b8);
        writew(red, fb_regs + 0x60b2);
        writew(green, fb_regs + 0x60b4);
        writew(blue, fb_regs + 0x60b6);
        writew(0xff, fb_regs + 0x60f0);
        udelay(100);
	writew(0xffff, fb_regs + 0x60ba);	
	return 0; 
}

static void hpfb_set_disp(int con, struct fb_info *info)
{
	struct display *display;
	
	if (con >= 0)
		display = &fb_display[con];
	else
		display = &disp;	/* used during initialization */

	display->screen_base = (char *) info->fix.smem_start;
	display->visual = info->fix.visual;
	display->type = info->fix.type;
	display->type_aux = info->fix.type_aux;
	display->ypanstep = info->fix.ypanstep;
	display->ywrapstep = info->fix.ywrapstep;
	display->line_length = info->fix.line_length;
	display->next_line = info->fix.line_length;
	display->can_soft_blank = 0;
	display->inverse = 0;

	display->dispsw = &fbcon_cfb8;
}

#define TOPCAT_FBOMSB	0x5d
#define TOPCAT_FBOLSB	0x5f

int __init hpfb_init_one(unsigned long base)
{
	unsigned long fboff;

	fboff = (readb(base + TOPCAT_FBOMSB) << 8) 
		| readb(base + TOPCAT_FBOLSB);

	hpfb_fix.smem_start = 0xf0000000 | (readb(base + fboff) << 16);
	fb_regs = base;

#if 0
	/* This is the magic incantation NetBSD uses to make Catseye boards work. */
	writeb(0, base+0x4800);
	writeb(0, base+0x4510);
	writeb(0, base+0x4512);
	writeb(0, base+0x4514);
	writeb(0, base+0x4516);
	writeb(0x90, base+0x4206);
#endif

	/* 
	 *	Give the hardware a bit of a prod and work out how many bits per
	 *	pixel are supported.
	 */
	
	writeb(0xff, base + TC_WEN);
	writeb(0xff, base + TC_FBEN);
	writeb(0xff, hpfb_fix.smem_start);
	fb_bitmask = readb(hpfb_fix.smem_start);

	/*
	 *	Enable reading/writing of all the planes.
	 */
	writeb(fb_bitmask, base + TC_WEN);
	writeb(fb_bitmask, base + TC_REN);
	writeb(fb_bitmask, base + TC_FBEN);
	writeb(0x1, base + TC_NBLANK);

	/*
	 *	Let there be consoles..
	 */
	strcpy(fb_info.modename, "Topcat");
	fb_info.changevar = NULL;
	fb_info.node = -1;
	fb_info.fbops = &hpfb_ops;
	fb_info.disp = &disp;
	fb_info.var = hpfb_var;
	fb_info.fix = hpfb_fix;
	fb_info.switch_con = &fbgen_switch;
	fb_info.updatevar = &fbgen_update_var;
	fb_info.flags = FBINFO_FLAG_DEFAULT;

	fb_copy_cmap(fb_default_cmap(1<<fb_info.var.bits_per_pixel),
                        &fb_info.cmap, 0);
        fb_set_cmap(&fb_info.cmap, 1, &fb_info);
	hpfb_set_var(&fb_info.var, -1, &fb_info);
	hpfb_set_disp(-1, &fb_info);

	if (register_framebuffer(&fb_info) < 0)
		return 1;

	return 0;
}

/* 
 * Check that the secondary ID indicates that we have some hope of working with this
 * framebuffer.  The catseye boards are pretty much like topcats and we can muddle through.
 */

#define topcat_sid_ok(x)  (((x) == DIO_ID2_LRCATSEYE) || ((x) == DIO_ID2_HRCCATSEYE)    \
			   || ((x) == DIO_ID2_HRMCATSEYE) || ((x) == DIO_ID2_TOPCAT))

/* 
 * Initialise the framebuffer
 */

int __init hpfb_init(void)
{
	unsigned int sid;

	/* Topcats can be on the internal IO bus or real DIO devices.
	 * The internal variant sits at 0xf0560000; it has primary
	 * and secondary ID registers just like the DIO version.
	 * So we merge the two detection routines.
	 *
	 * Perhaps this #define should be in a global header file:
	 * I believe it's common to all internal fbs, not just topcat.
	 */
#define INTFBADDR 0xf0560000

	if (hwreg_present((void *)INTFBADDR) && (DIO_ID(INTFBADDR) == DIO_ID_FBUFFER)
		&& topcat_sid_ok(sid = DIO_SECID(INTFBADDR)))
	{
		printk("Internal Topcat found (secondary id %02x)\n", sid); 
		hpfb_init_one(INTFBADDR);
	}
	else
	{
		int sc = dio_find(DIO_ID_FBUFFER);
		if (sc)
		{
			unsigned long addr = (unsigned long)dio_scodetoviraddr(sc);
			unsigned int sid = DIO_SECID(addr);

			if (topcat_sid_ok(sid))
			{
				printk("Topcat found at DIO select code %02x "
				       "(secondary id %02x)\n", sc, sid);
				hpfb_init_one(addr);
			}
		}
	}

	return 0;
}

int __init hpfb_setup(char *options)
{
	return 0;
}

static void topcat_blit(int x0, int y0, int x1, int y1, int w, int h)
{
        while (readb(fb_regs + BUSY) & fb_bitmask);
        writeb(0x3, fb_regs + WMRR);
        writew(x0, fb_regs + SOURCE_X);
        writew(y0, fb_regs + SOURCE_Y);
        writew(x1, fb_regs + DEST_X);
        writew(y1, fb_regs + DEST_Y);
        writew(h, fb_regs + WHEIGHT);
        writew(w, fb_regs + WWIDTH);
        writeb(fb_bitmask, fb_regs + WMOVE);
}
