/*
 *  linux/drivers/video/offb.c -- Open Firmware based frame buffer device
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
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
#include <linux/selection.h>
#include <linux/init.h>
#include <linux/ioport.h>
#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/bootx.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>
#include <video/macmodes.h>

static int currcon = 0;

struct offb_par {
    volatile unsigned char *cmap_adr;
    volatile unsigned char *cmap_data;
    int is_rage_128;
};	

static struct fb_info info;
static struct display disp;
static struct offb_par par;

#ifdef __powerpc__
#define mach_eieio()	eieio()
#else
#define mach_eieio()	do {} while (0)
#endif

static int ofonly = 0;

    /*
     *  Interface used by the world
     */

int offb_init(void);
int offb_setup(char*);

static int offb_open(struct fb_info *info, int user);
static int offb_release(struct fb_info *info, int user);
static int offb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int offb_pan_display(struct fb_var_screeninfo *var, int con,
			struct fb_info *info);
static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);
static int offb_blank(int blank, struct fb_info *info);

extern boot_infos_t *boot_infos;

static int offb_init_driver(struct device_node *);
static void offb_init_nodriver(struct device_node *);
static void offb_init_fb(const char *name, const char *full_name, int width,
		      int height, int depth, int pitch, unsigned long address,
		      struct device_node *dp);

    /*
     *  Interface to the low level console driver
     */

static int offbcon_switch(int con, struct fb_info *info);
static int offbcon_updatevar(int con, struct fb_info *info);

static struct fb_ops offb_ops = {
    fb_open:		offb_open, 
    fb_release:		offb_release, 
    fb_get_fix:		fbgen_get_fix, 
    fb_get_var:		fbgen_get_var, 
    fb_set_var:		offb_set_var,
    fb_get_cmap:	fbgen_get_cmap, 
    fb_set_cmap:	fbgen_set_cmap, 
    fb_setcolreg:	offb_setcolreg,
    fb_blank:		offb_blank,	
    fb_pan_display:	offb_pan_display, 
};

    /*
     *  Open/Release the frame buffer device
     */

static int offb_open(struct fb_info *info, int user)
{
    /*
     *  Nothing, only a usage count for the moment
     */

    MOD_INC_USE_COUNT;
    return(0);
}

static int offb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return(0);
}

    /*
     *  Set the User Defined Part of the Display
     */

static int offb_set_var(struct fb_var_screeninfo *var, int con,
			struct fb_info *info)
{
    struct display *display;
    unsigned int oldbpp = 0;
    int err;
    int activate = var->activate;

    if (con >= 0)
	display = &fb_display[con];
    else
	display = disp;	/* used during initialization */

    if (var->xres > info->var.xres || var->yres > info->var.yres ||
	var->xres_virtual > info->var.xres_virtual ||
	var->yres_virtual > info->var.yres_virtual ||
	var->bits_per_pixel > info->var.bits_per_pixel ||
	var->nonstd ||
	(var->vmode & FB_VMODE_MASK) != FB_VMODE_NONINTERLACED)
	return -EINVAL;

    if ((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	oldbpp = info->var.bits_per_pixel;
	memcpy(var, &info->var, sizeof(struct fb_var_screeninfo));
	display->var = *var;
    }
    if ((oldbpp != var->bits_per_pixel) || (info->cmap.len == 0)) {
	if ((err = fb_set_cmap(&info->cmap, 1, info))) 
	    return err;
    }
    return 0;
}

    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int offb_pan_display(struct fb_var_screeninfo *var, int con,
			    struct fb_info *info)
{
    if (var->xoffset || var->yoffset)
	return -EINVAL;
    else
	return 0;
}

#ifdef CONFIG_FB_S3TRIO
extern void s3triofb_init_of(struct device_node *dp);
#endif /* CONFIG_FB_S3TRIO */
#ifdef CONFIG_FB_IMSTT
extern void imsttfb_of_init(struct device_node *dp);
#endif
#ifdef CONFIG_FB_CT65550
extern void chips_of_init(struct device_node *dp);
#endif /* CONFIG_FB_CT65550 */
#ifdef CONFIG_FB_MATROX
extern int matrox_of_init(struct device_node *dp);
#endif /* CONFIG_FB_MATROX */
#ifdef CONFIG_FB_CONTROL
extern void control_of_init(struct device_node *dp);
#endif /* CONFIG_FB_CONTROL */
#ifdef CONFIG_FB_VALKYRIE
extern void valkyrie_of_init(struct device_node *dp);
#endif /* CONFIG_FB_VALKYRIE */
#ifdef CONFIG_FB_PLATINUM
extern void platinum_of_init(struct device_node *dp);
#endif /* CONFIG_FB_PLATINUM */
#ifdef CONFIG_FB_CLGEN
extern void clgen_of_init(struct device_node *dp);
#endif /* CONFIG_FB_CLGEN */

    /*
     *  Initialisation
     */

int __init offb_init(void)
{
    struct device_node *dp;
    unsigned int dpy;
    struct device_node *displays = find_type_devices("display");
    struct device_node *macos_display = NULL;

    /* If we're booted from BootX... */
    if (prom_num_displays == 0 && boot_infos != 0) {
	unsigned long addr = (unsigned long) boot_infos->dispDeviceBase;
	/* find the device node corresponding to the macos display */
	for (dp = displays; dp != NULL; dp = dp->next) {
	    int i;
	    /*
	     * Grrr...  It looks like the MacOS ATI driver
	     * munges the assigned-addresses property (but
	     * the AAPL,address value is OK).
	     */
	    if (strncmp(dp->name, "ATY,", 4) == 0 && dp->n_addrs == 1) {
		unsigned int *ap = (unsigned int *)
		    get_property(dp, "AAPL,address", NULL);
		if (ap != NULL) {
		    dp->addrs[0].address = *ap;
		    dp->addrs[0].size = 0x01000000;
		}
	    }

	    /*
	     * The LTPro on the Lombard powerbook has no addresses
	     * on the display nodes, they are on their parent.
	     */
	    if (dp->n_addrs == 0 && device_is_compatible(dp, "ATY,264LTPro")) {
		int na;
		unsigned int *ap = (unsigned int *)
		    get_property(dp, "AAPL,address", &na);
		if (ap != 0)
		    for (na /= sizeof(unsigned int); na > 0; --na, ++ap)
			if (*ap <= addr && addr < *ap + 0x1000000)
			    goto foundit;
	    }

	    /*
	     * See if the display address is in one of the address
	     * ranges for this display.
	     */
	    for (i = 0; i < dp->n_addrs; ++i) {
		if (dp->addrs[i].address <= addr
		    && addr < dp->addrs[i].address + dp->addrs[i].size)
		    break;
	    }
	    if (i < dp->n_addrs) {
	    foundit:
		printk(KERN_INFO "MacOS display is %s\n", dp->full_name);
		macos_display = dp;
		break;
	    }
	}

	/* initialize it */
	if (ofonly || macos_display == NULL 
	    || !offb_init_driver(macos_display)) {
	    offb_init_fb(macos_display? macos_display->name: "MacOS display",
			 macos_display? macos_display->full_name: "MacOS display",
			 boot_infos->dispDeviceRect[2],
			 boot_infos->dispDeviceRect[3],
			 boot_infos->dispDeviceDepth,
			 boot_infos->dispDeviceRowBytes, addr, NULL);
	}
    }

    for (dpy = 0; dpy < prom_num_displays; dpy++) {
	if ((dp = find_path_device(prom_display_paths[dpy])))
	    if (ofonly || !offb_init_driver(dp))
		offb_init_nodriver(dp);
    }

    if (!ofonly) {
	for (dp = find_type_devices("display"); dp != NULL; dp = dp->next) {
	    for (dpy = 0; dpy < prom_num_displays; dpy++)
		if (strcmp(dp->full_name, prom_display_paths[dpy]) == 0)
		    break;
	    if (dpy >= prom_num_displays && dp != macos_display)
		offb_init_driver(dp);
	}
    }
    return 0;
}


    /*
     *  This function is intended to go away as soon as all OF-aware frame
     *  buffer device drivers have been converted to use PCI probing and PCI
     *  resources. [ Geert ]
     */

static int __init offb_init_driver(struct device_node *dp)
{
#ifdef CONFIG_FB_S3TRIO
    if (!strncmp(dp->name, "S3Trio", 6)) {
    	s3triofb_init_of(dp);
	return 1;
    }
#endif /* CONFIG_FB_S3TRIO */
#ifdef CONFIG_FB_IMSTT
    if (!strncmp(dp->name, "IMS,tt", 6)) {
	imsttfb_of_init(dp);
	return 1;
    }
#endif
#ifdef CONFIG_FB_CT65550
    if (!strcmp(dp->name, "chips65550")) {
	chips_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_CT65550 */
#ifdef CONFIG_FB_MATROX
    if (!strncmp(dp->name, "MTRX", 4)) {
	matrox_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_MATROX */
#ifdef CONFIG_FB_CONTROL
    if(!strcmp(dp->name, "control")) {
	control_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_CONTROL */
#ifdef CONFIG_FB_VALKYRIE
    if(!strcmp(dp->name, "valkyrie")) {
	valkyrie_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_VALKYRIE */
#ifdef CONFIG_FB_PLATINUM
    if (!strncmp(dp->name, "platinum",8)) {
	platinum_of_init(dp);
	return 1;
    }
#endif /* CONFIG_FB_PLATINUM */
#ifdef CONFIG_FB_CLGEN
    if (!strncmp(dp->name, "MacPicasso",10) || !strncmp(dp->name, "54m30",5)) {
       clgen_of_init(dp);
       return 1;
    }
#endif /* CONFIG_FB_CLGEN */
    return 0;
}

static void __init offb_init_nodriver(struct device_node *dp)
{
    int *pp, i;
    unsigned int len;
    int width = 640, height = 480, depth = 8, pitch;
    unsigned *up, address;

    if ((pp = (int *)get_property(dp, "depth", &len)) != NULL
	&& len == sizeof(int))
	depth = *pp;
    if ((pp = (int *)get_property(dp, "width", &len)) != NULL
	&& len == sizeof(int))
	width = *pp;
    if ((pp = (int *)get_property(dp, "height", &len)) != NULL
	&& len == sizeof(int))
	height = *pp;
    if ((pp = (int *)get_property(dp, "linebytes", &len)) != NULL
	&& len == sizeof(int))
	pitch = *pp;
    else
	pitch = width;
    if ((up = (unsigned *)get_property(dp, "address", &len)) != NULL
	&& len == sizeof(unsigned))
	address = (u_long)*up;
    else {
	for (i = 0; i < dp->n_addrs; ++i)
	    if (dp->addrs[i].size >= pitch*height*depth/8)
		break;
	if (i >= dp->n_addrs) {
	    printk(KERN_ERR "no framebuffer address found for %s\n", dp->full_name);
	    return;
	}

	address = (u_long)dp->addrs[i].address;

	/* kludge for valkyrie */
	if (strcmp(dp->name, "valkyrie") == 0) 
	    address += 0x1000;
    }
    offb_init_fb(dp->name, dp->full_name, width, height, depth,
		 pitch, address, dp);
    
}

static void offb_init_fb(const char *name, const char *full_name,
				    int width, int height, int depth,
				    int pitch, unsigned long address,
				    struct device_node *dp)
{
    unsigned long res_start = address;
    unsigned long res_size = pitch*height*depth/8;
    int i;	

    if (!request_mem_region(res_start, res_size, "offb"))
	return;

    printk(KERN_INFO "Using unsupported %dx%d %s at %lx, depth=%d, pitch=%d\n",
	   width, height, name, address, depth, pitch);
    if (depth != 8 && depth != 16 && depth != 32) {
	printk(KERN_ERR "%s: can't use depth = %d\n", full_name, depth);
	release_mem_region(res_start, res_size);
	return;
    }

    fix = &info->fix;
    var = &info->var;
    disp = &info->disp;

    strcpy(fix->id, "OFfb ");
    strncat(fix->id, name, sizeof(fix->id));
    fix->id[sizeof(fix->id)-1] = '\0';

    var->xres = var->xres_virtual = width;
    var->yres = var->yres_virtual = height;
    fix->line_length = pitch;

    fix->smem_start = address;
    fix->smem_len = pitch * height;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->type_aux = 0;

    par->is_rage_128 = 0;
    if (depth == 8)
    {
    	/* XXX kludge for ati */
	if (strncmp(name, "ATY,Rage128", 11) == 0) {
	    if (dp) {
		unsigned long regbase = dp->addrs[2].address;
		par->cmap_adr = ioremap(regbase, 0x1FFF) + 0x00b0;
		par->cmap_data = info->cmap_adr + 4;
		par->is_rage_128 = 1;
	    }
	} else if (strncmp(name, "ATY,", 4) == 0) {
		unsigned long base = address & 0xff000000UL;
		par->cmap_adr = ioremap(base + 0x7ff000, 0x1000) + 0xcc0;
		par->cmap_data = par->cmap_adr + 1;
	}
        fix->visual = par->cmap_adr ? FB_VISUAL_PSEUDOCOLOR
				     : FB_VISUAL_STATIC_PSEUDOCOLOR;
    }
    else
	fix->visual = /* par->cmap_adr ? FB_VISUAL_DIRECTCOLOR
				     : */FB_VISUAL_TRUECOLOR;

    var->xoffset = var->yoffset = 0;
    var->bits_per_pixel = depth;
    switch (depth) {
	case 8:
	    var->bits_per_pixel = 8;
	    var->red.offset = 0;
	    var->red.length = 8;
	    var->green.offset = 0;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 16:	/* RGB 555 */
	    var->bits_per_pixel = 16;
	    var->red.offset = 10;
	    var->red.length = 5;
	    var->green.offset = 5;
	    var->green.length = 5;
	    var->blue.offset = 0;
	    var->blue.length = 5;
	    var->transp.offset = 0;
	    var->transp.length = 0;
	    break;
	case 32:	/* RGB 888 */
	    var->bits_per_pixel = 32;
	    var->red.offset = 16;
	    var->red.length = 8;
	    var->green.offset = 8;
	    var->green.length = 8;
	    var->blue.offset = 0;
	    var->blue.length = 8;
	    var->transp.offset = 24;
	    var->transp.length = 8;
	    break;
    }
    var->red.msb_right = var->green.msb_right = var->blue.msb_right = var->transp.msb_right = 0;
    var->grayscale = 0;
    var->nonstd = 0;
    var->activate = 0;
    var->height = var->width = -1;
    var->pixclock = 10000;
    var->left_margin = var->right_margin = 16;
    var->upper_margin = var->lower_margin = 16;
    var->hsync_len = var->vsync_len = 8;
    var->sync = 0;
    var->vmode = FB_VMODE_NONINTERLACED;

    disp->var = *var;
    disp->cmap.start = 0;
    disp->cmap.len = 0;
    disp->cmap.red = NULL;
    disp->cmap.green = NULL;
    disp->cmap.blue = NULL;
    disp->cmap.transp = NULL;
    disp->screen_base = ioremap(address, fix->smem_len);
    disp->visual = fix->visual;
    disp->type = fix->type;
    disp->type_aux = fix->type_aux;
    disp->ypanstep = 0;
    disp->ywrapstep = 0;
    disp->line_length = fix->line_length;
    disp->can_soft_blank = info->cmap_adr ? 1 : 0;
    disp->inverse = 0;
    switch (depth) {
#ifdef FBCON_HAS_CFB8
        case 8:
            disp->dispsw = &fbcon_cfb8;
            break;
#endif
#ifdef FBCON_HAS_CFB16
        case 16:
            disp->dispsw = &fbcon_cfb16;
            disp->dispsw_data = &pseudo_palette;
            for (i = 0; i < 16; i++)
            	if (fix->visual == FB_VISUAL_TRUECOLOR)
		    info->pseudo_palette[i] =
			    (((default_blu[i] >> 3) & 0x1f) << 10) |
			    (((default_grn[i] >> 3) & 0x1f) << 5) |
			    ((default_red[i] >> 3) & 0x1f);
		else
		    info->pseudo_palette[i] =
			    (i << 10) | (i << 5) | i;
            break;
#endif
#ifdef FBCON_HAS_CFB32
        case 32:
            disp->dispsw = &fbcon_cfb32;
            disp->dispsw_data = info->fbcon_cmap.cfb32;
            for (i = 0; i < 16; i++)
            	if (fix->visual == FB_VISUAL_TRUECOLOR)
		    info->fbcon_cmap.cfb32[i] =
			(default_blu[i] << 16) |
			(default_grn[i] << 8) |
			default_red[i];
		else
		    info->fbcon_cmap.cfb32[i] =
			    (i << 16) | (i << 8) | i;
            break;
#endif
        default:
            disp->dispsw = &fbcon_dummy;
    }

    disp->scrollmode = SCROLL_YREDRAW;

    strcpy(info->info.modename, "OFfb ");
    strncat(info->info.modename, full_name, sizeof(info->info.modename));
    info->info.node = -1;
    info->info.fbops = &offb_ops;
    info->info.disp = disp;
    info->info.changevar = NULL;
    info->info.switch_con = &fbgen_switch;
    info->info.updatevar = &fbgen_updatevar;
    info->info.flags = FBINFO_FLAG_DEFAULT;

    /* Set color map */
    offb_set_var(var, -1, &info->info);

    if (register_framebuffer(&info->info) < 0) {
	kfree(info);
	release_mem_region(res_start, res_size);
	return;
    }

    printk(KERN_INFO "fb%d: Open Firmware frame buffer device on %s\n",
	   GET_FB_IDX(info->info.node), full_name);

#ifdef CONFIG_FB_COMPAT_XPMAC
    if (!console_fb_info) {
	display_info.height = var->yres;
	display_info.width = var->xres;
	display_info.depth = depth;
	display_info.pitch = fix->line_length;
	display_info.mode = 0;
	strncpy(display_info.name, name, sizeof(display_info.name));
	display_info.fb_address = address;
	display_info.cmap_adr_address = 0;
	display_info.cmap_data_address = 0;
	display_info.disp_reg_address = 0;
	/* XXX kludge for ati */
	if (strncmp(name, "ATY,", 4) == 0) {
	    unsigned long base = address & 0xff000000UL;
	    display_info.disp_reg_address = base + 0x7ffc00;
	    display_info.cmap_adr_address = base + 0x7ffcc0;
	    display_info.cmap_data_address = base + 0x7ffcc1;
	}
	console_fb_info = &info->info;
    }
#endif /* CONFIG_FB_COMPAT_XPMAC) */
}


    /*
     *  Setup: parse used options
     */

int offb_setup(char *options)
{
    if (!options || !*options)
	return 0;

    if (!strcmp(options, "ofonly"))
	ofonly = 1;
    return 0;
}

    /* 
     *  Blank the display.
     */

static int offb_blank(int blank, struct fb_info *info)
{
    struct offb_par *ofpar = (struct offb_par *)info->par;
    int i, j;

    if (!par->cmap_adr)
	return;

    if (blank)
	for (i = 0; i < 256; i++) {
	    *par->cmap_adr = i;
	    mach_eieio();
	    for (j = 0; j < 3; j++) {
		*par->cmap_data = 0;
		mach_eieio();
	    }
	}
    else
	do_install_cmap(currcon, info);
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int offb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			 u_int transp, struct fb_info *info)
{
    struct fb_info_offb *par = (struct fb_info_offb *)info;
    int i;

    if (!par->cmap_adr)
        return -ENOSYS;
	
    if (!par->cmap_adr || regno > 255)
	return 1;

    red >>= 8;
    green >>= 8;
    blue >>= 8;

    *par->cmap_adr = regno;  /* On some chipsets, add << 3 in 15 bits */
    mach_eieio();
    if (par->is_rage_128) {
    	out_le32((unsigned int *)par->cmap_data,
    		(red << 16 | green << 8 | blue));
    } else {
	*par->cmap_data = red;
    	mach_eieio();
    	*par->cmap_data = green;
    	mach_eieio();
    	*par->cmap_data = blue;
    	mach_eieio();
    }

    if (regno < 16)
	switch (info->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB16
	    case 16:
		((u16*)(info->pseudo_palette))[regno] = (regno << 10) | (regno << 5) | regno;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	    case 32:
		i = (regno << 8) | regno;
		((u32*)(info->pseudo_palette))[regno] = (i << 16) | i;
		break;
#endif
       }
    return 0;
}
