/*
 * framebuffer driver for VBE 2.0 compliant graphic boards
 *
 * switching to graphics mode happens at boot time (while
 * running in real mode, see arch/i386/boot/video.S).
 *
 * (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/mtrr.h>

#define dac_reg	(0x3c8)
#define dac_val	(0x3c9)

/* --------------------------------------------------------------------- */

static struct fb_var_screeninfo vesafb_defined = {
	0,0,0,0,	/* W,H, W, H (virtual) load xres,xres_virtual*/
	0,0,		/* virtual -> visible no offset */
	8,		/* depth -> load bits_per_pixel */
	0,		/* greyscale ? */
	{0,0,0},	/* R */
	{0,0,0},	/* G */
	{0,0,0},	/* B */
	{0,0,0},	/* transparency */
	0,		/* standard pixel format */
	FB_ACTIVATE_NOW,
	-1,-1,
	0,
	0L,0L,0L,0L,0L,
	0L,0L,0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};

static struct fb_fix_screeninfo vesafb_fix __initdata = {
    "VESA VGA", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_PSEUDOCOLOR, 0, 0, 0, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static struct fb_info fb_info;
static u32 pseudo_palette[17];

static int             inverse   = 0;
static int             mtrr      = 0;
static int             pmi_setpal = 0;	/* pmi for palette changes ??? */
static int             ypan       = 0;  /* 0..nothing, 1..ypan, 2..ywrap */
static unsigned short  *pmi_base  = 0;
static void            (*pmi_start)(void);
static void            (*pmi_pal)(void);

/* --------------------------------------------------------------------- */

static int vesafb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
/*
	if (var->yres_virtual < video_height_virtual ||
	    var->yres_virtual > info->var.yres) 
		return 0;
	printk(KERN_ERR "Vesafb does not support changing the video mode\n");
	return -EINVAL;	
*/
	return 0;
}

static int vesafb_pan_display(struct fb_var_screeninfo *var, 
                              struct fb_info *info)
{
	int offset;

	if (!ypan)
		return -EINVAL;
	if (var->xoffset)
		return -EINVAL;
	if (var->yoffset > var->yres_virtual)
		return -EINVAL;
	if ((ypan==1) && var->yoffset+var->yres > var->yres_virtual)
		return -EINVAL;

	offset = (var->yoffset * info->fix.line_length + var->xoffset) / 4;

        __asm__ __volatile__(
                "call *(%%edi)"
                : /* no return value */
                : "a" (0x4f07),         /* EAX */
                  "b" (0),              /* EBX */
                  "c" (offset),         /* ECX */
                  "d" (offset >> 16),   /* EDX */
                  "D" (&pmi_start));    /* EDI */
	return 0;
}

static void vesa_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	struct { u_char blue, green, red, pad; } entry;

	if (pmi_setpal) {
		entry.red   = red   >> 10;
		entry.green = green >> 10;
		entry.blue  = blue  >> 10;
		entry.pad   = 0;
	        __asm__ __volatile__(
                "call *(%%esi)"
                : /* no return value */
                : "a" (0x4f09),         /* EAX */
                  "b" (0),              /* EBX */
                  "c" (1),              /* ECX */
                  "d" (regno),          /* EDX */
                  "D" (&entry),         /* EDI */
                  "S" (&pmi_pal));      /* ESI */
	} else {
		/* without protected mode interface, try VGA registers... */
		outb_p(regno,       dac_reg);
		outb_p(red   >> 10, dac_val);
		outb_p(green >> 10, dac_val);
		outb_p(blue  >> 10, dac_val);
	}
}

static int vesafb_setcolreg(unsigned regno, unsigned red, unsigned green,
			    unsigned blue, unsigned transp,
			    struct fb_info *info)
{
	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	if (regno >= info->cmap.len)
		return 1;

	switch (info->var.bits_per_pixel) {
		case 8:
			vesa_setpalette(regno, red, green, blue);
			break;
		case 15:
		case 16:
			if (info->var.red.offset == 10) {
				/* 1:5:5:5 */
				((u16*) (info->pseudo_palette))[regno] =
					((red   & 0xf800) >>  1) |
					((green & 0xf800) >>  6) |
					((blue  & 0xf800) >> 11);
			} else {
				/* 0:5:6:5 */
				((u16*) (info->pseudo_palette))[regno] =
					((red   & 0xf800)      ) |
					((green & 0xfc00) >>  5) |
					((blue  & 0xf800) >> 11);
			}
			break;
		case 24:
			red   >>= 8;
			green >>= 8;
			blue  >>= 8;
			((u32 *)(info->pseudo_palette))[regno] =
				(red   << info->var.red.offset)   |
				(green << info->var.green.offset) |
				(blue  << info->var.blue.offset);
			break;
		case 32:
			red   >>= 8;
			green >>= 8;
			blue  >>= 8;
			((u32 *)(info->pseudo_palette))[regno] =
				(red   << info->var.red.offset)   |
				(green << info->var.green.offset) |
				(blue  << info->var.blue.offset);
			break;
	}
    	return 0;
}

static struct fb_ops vesafb_ops = {
	owner:		THIS_MODULE,
	fb_check_var:	vesafb_check_var,
	fb_setcolreg:	vesafb_setcolreg,
	fb_pan_display:	vesafb_pan_display,
	fb_fillrect:	cfb_fillrect,
	fb_copyarea:	cfb_copyarea,
	fb_imageblit:	cfb_imageblit,
};

int __init vesafb_setup(char *options)
{
	char *this_opt;
	
	if (!options || !*options)
		return 0;
	
	for(this_opt=strtok(options,","); this_opt; this_opt=strtok(NULL,",")) {
		if (!*this_opt) continue;
		
		if (! strcmp(this_opt, "inverse"))
			inverse=1;
		else if (! strcmp(this_opt, "redraw"))
			ypan=0;
		else if (! strcmp(this_opt, "ypan"))
			ypan=1;
		else if (! strcmp(this_opt, "ywrap"))
			ypan=2;
		else if (! strcmp(this_opt, "vgapal"))
			pmi_setpal=0;
		else if (! strcmp(this_opt, "pmipal"))
			pmi_setpal=1;
		else if (! strcmp(this_opt, "mtrr"))
			mtrr=1;
	}
	return 0;
}

int __init vesafb_init(void)
{
	/* Mode */
	int  video_width, video_height, video_height_virtual, video_bpp, i;
	int  video_visual, video_linelength, video_cmap_len, video_size;
	unsigned long video_base; /* physical addr */
	char *video_vbase;        /* mapped */

	if (screen_info.orig_video_isVGA != VIDEO_TYPE_VLFB)
		return -ENXIO;

	printk("It is a linear framebuffer\n");

	video_base          = screen_info.lfb_base;
	video_bpp           = screen_info.lfb_depth;
	if (15 == video_bpp)
		video_bpp = 16;
	video_width         = screen_info.lfb_width;
	video_height        = screen_info.lfb_height;
	video_linelength    = screen_info.lfb_linelength;
	video_size          = screen_info.lfb_size * 65536;
	video_visual = (video_bpp == 8) ?
		FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;

	if (!request_mem_region(video_base, video_size, "vesafb")) {
		printk(KERN_ERR
		       "vesafb: abort, cannot reserve video memory at 0x%lx\n",
			video_base);
		return -EBUSY;
	}

	printk("Got requested region (framebuffer)\n");

        fb_info.screen_base = video_vbase = ioremap(video_base, video_size);
	if (!video_vbase) {
		release_mem_region(video_base, video_size);
		printk(KERN_ERR
		       "vesafb: abort, cannot ioremap video memory 0x%x @ 0x%lx\n",
			video_size, video_base);
		return -EIO;
	}

	printk(KERN_INFO "vesafb: framebuffer at 0x%lx, mapped to 0x%p, size %dk\n",
	       video_base, video_vbase, video_size/1024);
	printk(KERN_INFO "vesafb: mode is %dx%dx%d, linelength=%d, pages=%d\n",
	       video_width, video_height, video_bpp, video_linelength, screen_info.pages);

	if (screen_info.vesapm_seg) {
		printk(KERN_INFO "vesafb: protected mode interface info at %04x:%04x\n",
		       screen_info.vesapm_seg,screen_info.vesapm_off);
	}

	if (screen_info.vesapm_seg < 0xc000)
		ypan = pmi_setpal = 0; /* not available or some DOS TSR ... */

	if (ypan || pmi_setpal) {
		pmi_base  = (unsigned short*)bus_to_virt(((unsigned long)screen_info.vesapm_seg << 4) + screen_info.vesapm_off);
		pmi_start = (void*)((char*)pmi_base + pmi_base[1]);
		pmi_pal   = (void*)((char*)pmi_base + pmi_base[2]);
		printk(KERN_INFO "vesafb: pmi: set display start = %p, set palette = %p\n",pmi_start,pmi_pal);
		if (pmi_base[3]) {
			printk(KERN_INFO "vesafb: pmi: ports = ");
				for (i = pmi_base[3]/2; pmi_base[i] != 0xffff; i++)
					printk("%x ",pmi_base[i]);
			printk("\n");
			if (pmi_base[i] != 0xffff) {
				/*
				 * memory areas not supported (yet?)
				 *
				 * Rules are: we have to set up a descriptor for the requested
				 * memory area and pass it in the ES register to the BIOS function.
				 */
				printk(KERN_INFO "vesafb: can't handle memory requests, pmi disabled\n");
				ypan = pmi_setpal = 0;
			}
		}
	}

	vesafb_defined.xres=video_width;
	vesafb_defined.yres=video_height;
	vesafb_defined.xres_virtual=video_width;
	vesafb_defined.yres_virtual=video_size / video_linelength;
	vesafb_defined.bits_per_pixel=video_bpp;

	if (ypan && vesafb_defined.yres_virtual > video_height) {
		printk(KERN_INFO "vesafb: scrolling: %s using protected mode interface, yres_virtual=%d\n", (ypan > 1) ? "ywrap" : "ypan", vesafb_defined.yres_virtual);
	} else {
		printk(KERN_INFO "vesafb: scrolling: redraw\n");
		vesafb_defined.yres_virtual = video_height;
		ypan = 0;
	}
	video_height_virtual = vesafb_defined.yres_virtual;

	/* some dummy values for timing to make fbset happy */
	vesafb_defined.pixclock     = 10000000 / video_width * 1000 / video_height;
	vesafb_defined.left_margin  = (video_width / 8) & 0xf8;
	vesafb_defined.right_margin = 32;
	vesafb_defined.upper_margin = 16;
	vesafb_defined.lower_margin = 4;
	vesafb_defined.hsync_len    = (video_width / 8) & 0xf8;
	vesafb_defined.vsync_len    = 4;

	if (video_bpp > 8) {
		vesafb_defined.red.offset    = screen_info.red_pos;
		vesafb_defined.red.length    = screen_info.red_size;
		vesafb_defined.green.offset  = screen_info.green_pos;
		vesafb_defined.green.length  = screen_info.green_size;
		vesafb_defined.blue.offset   = screen_info.blue_pos;
		vesafb_defined.blue.length   = screen_info.blue_size;
		vesafb_defined.transp.offset = screen_info.rsvd_pos;
		vesafb_defined.transp.length = screen_info.rsvd_size;
		printk(KERN_INFO "vesafb: directcolor: "
		       "size=%d:%d:%d:%d, shift=%d:%d:%d:%d\n",
		       screen_info.rsvd_size,
		       screen_info.red_size,
		       screen_info.green_size,
		       screen_info.blue_size,
		       screen_info.rsvd_pos,
		       screen_info.red_pos,
		       screen_info.green_pos,
		       screen_info.blue_pos);
		video_cmap_len = 16;
	} else {
		vesafb_defined.red.length   = 6;
		vesafb_defined.green.length = 6;
		vesafb_defined.blue.length  = 6;
		video_cmap_len = 256;
	}

	vesafb_fix.smem_start = video_base;
        vesafb_fix.smem_len = video_size;
        vesafb_fix.visual = video_visual;
        vesafb_fix.ypanstep  = ypan     ? 1 : 0;
        vesafb_fix.ywrapstep = (ypan>1) ? 1 : 0;
        vesafb_fix.line_length = video_linelength;

	/* request failure does not faze us, as vgacon probably has this
	 * region already (FIXME) */
	request_region(0x3c0, 32, "vesafb");

	if (mtrr) {
		int temp_size = video_size;
		while (mtrr_add(video_base, temp_size, MTRR_TYPE_WRCOMB, 1)==-EINVAL) {
			temp_size >>= 1;
		}
	}
	
	fb_info.node = -1;
	fb_info.fbops = &vesafb_ops;
	fb_info.var = vesafb_defined;
	fb_info.fix = vesafb_fix;
	fb_info.flags=FBINFO_FLAG_DEFAULT;
	fb_info.pseudo_palette = pseudo_palette;

	if (register_framebuffer(&fb_info)<0)
		return -EINVAL;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(fb_info.node), fb_info.fix.id);
	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
