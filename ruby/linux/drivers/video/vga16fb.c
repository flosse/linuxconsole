/*
 * linux/drivers/video/vga16fb.c -- VGA 16-color framebuffer driver
 * 
 * Copyright 1999 Ben Pfaff <pfaffben@debian.org> and Petr Vandrovec <VANDROVE@vc.cvut.cz>
 * Based on VGA info at http://www.goodnet.com/~tinara/FreeVGA/home.htm
 * Based on VESA framebuffer (c) 1998 Gerd Knorr <kraxel@goldbach.in-berlin.de>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.  */

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

#include <video/vga.h>

#define VGA_FB_PHYS 0xA0000
#define VGA_FB_PHYS_LEN 65536

/* --------------------------------------------------------------------- */

/*
 * card parameters
 */

struct vga_hw_state vgafb_state;

static struct fb_info vga16fb; 
static struct vga_hw_state __initdata default_par; 

static struct fb_var_screeninfo vga16fb_defined __initdata = {
	640,480,640,480,/* W,H, W, H (virtual) load xres,xres_virtual*/
	0,0,		/* virtual -> visible no offset */
	4,		/* depth -> load bits_per_pixel */
	0,		/* greyscale ? */
	{0,0,0},	/* R */
	{0,0,0},	/* G */
	{0,0,0},	/* B */
	{0,0,0},	/* transparency */
	0,		/* standard pixel format */
	FB_ACTIVATE_TEST,
	-1,-1,
	0,
	39721, 48, 16, 39, 8,
	96, 2, 0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};

/* name should not depend on EGA/VGA */
static struct fb_fix_screeninfo vga16fb_fix __initdata = {
    "VGA16 VGA", VGA_FB_PHYS, VGA_FB_PHYS_LEN, FB_TYPE_VGA_PLANES,  
    FB_AUX_VGA_PLANES_VGA4, FB_VISUAL_PSEUDOCOLOR, 8, 1, 0, 640/8, 
    (unsigned long) NULL, 0, FB_ACCEL_NONE
};

/* --------------------------------------------------------------------- */

static void vga16fb_pan_var(struct fb_info *info,struct fb_var_screeninfo *var) 
{
	struct vga_hw_state *par = (struct vga_hw_state *)info->par;
	caddr_t regs = par->regsbase;
	u32 xoffset, pos;

	xoffset = var->xoffset;
	if (var->bits_per_pixel == 8) {
		pos = (var->xres_virtual * var->yoffset + xoffset) >> 2;
	} else {
		if (var->nonstd)
			xoffset--;
		pos = (var->xres_virtual * var->yoffset + xoffset) >> 3;
	}
	vga_wcrt(regs, VGA_CRTC_START_HI, pos >> 8);
	vga_wcrt(regs, VGA_CRTC_START_LO, pos & 0xFF);
	/* if we support CFB4, then we must! support xoffset with pixel 
	   granularity */
	/* if someone supports xoffset in bit resolution */
	vga_r(regs, VGA_IS1_RC);		/* reset flip-flop */
	vga_w(regs, VGA_ATT_IW, VGA_ATC_PEL);
	if (var->bits_per_pixel == 8)
		vga_w(regs, VGA_ATT_IW, (xoffset & 3) << 1);
	else
		vga_w(regs, VGA_ATT_IW, xoffset & 7);
	vga_r(regs, VGA_IS1_RC);
	vga_w(regs, VGA_ATT_IW, 0x20);
}

#define FAIL(X) return -EINVAL

#define MODE_SKIP4	1
#define MODE_8BPP	2
#define MODE_CFB	4
#define MODE_TEXT	8
static int vga16fb_check_var(struct fb_var_screeninfo *var,  
			     struct fb_info *info)
{
	const struct vga_hw_state *par = (struct vga_hw_state *) info->par;
	u32 xres, right, hslen, left, yres, lower, vslen, upper;
	u32 vxres, xoffset, vyres, yoffset, maxmem;
	u8 shift = 0, double_scan = 0;

	if (var->bits_per_pixel == 4) {
		if (var->nonstd) {
			if (par->video_type != VIDEO_TYPE_VGAC)
				return -EINVAL;
			shift = 3;
			maxmem = 16384;
		} else {
			shift = 3;
			maxmem = 65536;
		}
	} else if (var->bits_per_pixel == 8) {
		if (par->video_type != VIDEO_TYPE_VGAC)
			return -EINVAL;	/* only supported on VGA */
		shift = 2;
		if (var->nonstd) {
			maxmem = 65536;
		} else {
			maxmem = 16384;
			return -EINVAL;
		}
	} else
		return -EINVAL;

	xres = (var->xres + 7) & ~7;
	vxres = (var->xres_virtual + 0xF) & ~0xF;
	xoffset = (var->xoffset + 7) & ~7;
	left = (var->left_margin + 7) & ~7;
	right = (var->right_margin + 7) & ~7;
	hslen = (var->hsync_len + 7) & ~7;

	if (vxres < xres)
		vxres = xres;
	if (xres + xoffset > vxres)
		xoffset = vxres - xres;

	var->xres = xres;
	var->right_margin = right;
	var->hsync_len = hslen;
	var->left_margin = left;
	var->xres_virtual = vxres;
	var->xoffset = xoffset;

	yres = var->yres;
	lower = var->lower_margin;
	vslen = var->vsync_len;
	upper = var->upper_margin;
	vyres = var->yres_virtual;
	yoffset = var->yoffset;

	if (yres > vyres)
		vyres = yres;
	if ((vxres >>= shift) * vyres > maxmem) {
		vyres = maxmem / (vxres >>=shift);
		if (vyres < yres)
			return -ENOMEM;
	}
	if (yoffset + yres > vyres)
		yoffset = vyres - yres;

	var->yres = yres;
	var->lower_margin = lower;
	var->vsync_len = vslen;
	var->upper_margin = upper;
	var->yres_virtual = vyres;
	var->yoffset = yoffset;

	if (var->vmode & FB_VMODE_DOUBLE)
		double_scan = 1;

	xres  >>= shift;
        right >>= shift;
        hslen >>= shift;
        left  >>= shift;
        vxres >>= shift;
	
	vga_check_mode(xres, vxres, right, hslen, left, yres, lower, vslen,
		       upper, double_scan);

	var->red.offset = var->green.offset = var->blue.offset = 
		var->transp.offset = 0;
	var->red.length = var->green.length = var->blue.length =
		(par->video_type == VIDEO_TYPE_VGAC) ? 6 : 2;
	var->transp.length = 0;
	var->height = -1;
	var->width = -1;
	var->accel_flags = FB_ACCEL_NONE;
	return 0;
}
#undef FAIL

static int vga16fb_set_par(struct fb_info *info)
{
	struct vga_hw_state *par = (struct vga_hw_state *) info->par;
	int double_scan = 0;

	par->pel_mask = 0xFF;

	if (info->var.bits_per_pixel == 4) {
		if (info->var.nonstd) {
			info->fix.type = FB_TYPE_PACKED_PIXELS;
			info->fix.line_length = info->var.xres_virtual / 2;
			par->mode = MODE_SKIP4 | MODE_CFB;
			par->pel_mask = 0x0F;
			par->shift = 3; 
		} else {
			info->fix.type = FB_TYPE_VGA_PLANES;
			info->fix.type_aux = FB_AUX_VGA_PLANES_VGA4;
			info->fix.line_length = info->var.xres_virtual / 8;
                        par->mode = 0;
			par->shift = 3;
		}
	} else {	/* 8bpp */
                if (info->var.nonstd) {
                        par->mode = MODE_8BPP | MODE_CFB;
			info->fix.type = FB_TYPE_VGA_PLANES;
                        info->fix.type_aux = FB_AUX_VGA_PLANES_CFB8;
                        info->fix.line_length = info->var.xres_virtual / 4;
			par->shift = 2;
                } else {
                        par->mode = MODE_SKIP4 | MODE_8BPP | MODE_CFB;
			info->fix.type = FB_TYPE_PACKED_PIXELS;
                        info->fix.line_length = info->var.xres_virtual;
			par->shift = 2;
                }
        }
	par->xres  = info->var.xres >> par->shift;
	par->right = info->var.right_margin >> par->shift;
	par->hslen = info->var.hsync_len >> par->shift;
	par->left  = info->var.left_margin >> par->shift;
	par->vxres = info->var.xres_virtual >> par->shift;
	par->xoffset = info->var.xoffset >> par->shift;

	par->yres  = info->var.yres;
	par->lower = info->var.lower_margin;
	par->vslen = info->var.vsync_len;
	par->upper = info->var.upper_margin;
	par->yoffset = info->var.yoffset;

	if (info->var.vmode & FB_VMODE_DOUBLE) {
                par->yres <<= 1;
                par->lower <<= 1;
                par->vslen <<= 1;
                par->upper <<= 1;
      		double_scan = 1; 
	}
	
	par->misc = 0xE3;
	if (info->var.sync & FB_SYNC_HOR_HIGH_ACT)
                par->misc &= ~0x40;
        if (info->var.sync & FB_SYNC_VERT_HIGH_ACT)
                par->misc &= ~0x80;

	if (par->mode & MODE_8BPP)
                /* pixel clock == vga clock / 2 */
                vga_clock_chip(par, info->var.pixclock, 1, 2);
        else
                /* pixel clock == vga clock */
                vga_clock_chip(par, info->var.pixclock, 1, 1);

        vga_set_mode(par, double_scan);
	return 0;
}

static void ega16_setpalette(struct vga_hw_state *par, int regno, unsigned red, 
			     unsigned green, unsigned blue)
{
	static unsigned char map[] = { 000, 001, 010, 011 };
	caddr_t regs = par->regsbase;
	int val;
	
	if (regno >= 16)
		return;
	val = map[red>>14] | ((map[green>>14]) << 1) | ((map[blue>>14]) << 2);
	vga_r(regs, VGA_IS1_RC);   /* ! 0x3BA */
	vga_wattr(regs, regno, val);
	vga_r(regs, VGA_IS1_RC);   /* some clones need it */
	vga_w(regs, VGA_ATT_IW, 0x20); /* unblank screen */
}

static void vga16_setpalette(struct vga_hw_state *par, int regno, unsigned red,
			     unsigned green, unsigned blue)
{
	caddr_t regs = par->regsbase;

	vga_w(regs, VGA_PEL_IW, regno);
	vga_w(regs, VGA_PEL_D, red   >> 10);
	vga_w(regs, VGA_PEL_D, green >> 10);
	vga_w(regs, VGA_PEL_D, blue  >> 10);
}

static int vga16fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			     unsigned blue, unsigned transp,
			     struct fb_info *info)
{
	struct vga_hw_state *par = (struct vga_hw_state *) info->par;

	/*
	 *  Set a single color register. The values supplied are
	 *  already rounded down to the hardware's capabilities
	 *  (according to the entries in the `var' structure). Return
	 *  != 0 for invalid regno.
	 */
	
	if (regno >= 256)
		return 1;

	if (info->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}
	if (par->video_type == VIDEO_TYPE_VGAC) 
		vga16_setpalette(par, regno, red, green, blue);
	else
		ega16_setpalette(par, regno, red, green, blue);
	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static int vga16fb_blank(int blank, struct fb_info *info)
{
	struct vga_hw_state *par = (struct vga_hw_state *) info->par;

	switch (blank) {
	case 0:				/* Unblank */
		if (par->vesa_blanked) {
			vga_vesa_unblank(par);
			par->vesa_blanked = 0;
		}
		if (par->palette_blanked) {
			fb_set_cmap(&info->cmap, 1, info);
			par->palette_blanked = 0;
		}
		break;
	case 1:				/* blank */
		vga_pal_blank();
		par->palette_blanked = 1;
		break;
	default:			/* VESA blanking */
		vga_vesa_blank(par, blank-1);
		par->vesa_blanked = 1;
		break;
	}
	return 0;
}

static int vga16fb_pan_display(struct fb_var_screeninfo *var,
			       struct fb_info *info) 
{
	if (var->xoffset + info->var.xres > info->var.xres_virtual ||
	    var->yoffset + info->var.yres > info->var.yres_virtual)
		return -EINVAL;
	vga16fb_pan_var(info, var);
	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	info->var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

static void vga16fb_fillrect(struct fb_info *info, int x1, int y1,
	 		     unsigned int width, unsigned int height, 
			     unsigned long color, int rop)
{
	struct vga_hw_state *par = (struct vga_hw_state *)info->par;
        int line_ofs = info->fix.line_length - width;
	caddr_t regs = par->regsbase;
	char *where;
        int x;

	vga_wgfx(regs, VGA_GFX_MODE, 2);	
	vga_wgfx(regs, VGA_GFX_DATA_ROTATE, 0);
	vga_wgfx(regs, VGA_GFX_BIT_MASK, 0xff);

        where = info->screen_base + x1 + y1 * info->fix.line_length;
       
	wmb(); 
	while (height--) {
		fb_memset(where, color, width);
                where += line_ofs;
        }
	return;
}

static void vga16fb_copyarea(struct fb_info *info, int sx, int sy, 
			     unsigned int width, unsigned int height, 
			     int dx, int dy)
{
	struct vga_hw_state *par = (struct vga_hw_state *)info->par;
	caddr_t regs = par->regsbase;
        char *dest, *src;
        int line_ofs, x;

	vga_wgfx(regs, VGA_GFX_MODE, 1);
        vga_wgfx(regs, VGA_GFX_DATA_ROTATE, 0);
        vga_wgfx(regs, VGA_GFX_SR_ENABLE, 0xf);
        
        if (dy < sy || (dy == sy && dx < sx)) {
                line_ofs = info->fix.line_length - width;
                dest = info->screen_base + dx + dy * info->fix.line_length;
                src = info->screen_base + sx + sy * info->fix.line_length;
                while (height--) {
                        for (x = 0; x < width; x++) {
			       	fb_readb(src);
                                fb_writeb(0, dest);
                                dest++;
                                src++;
                        }
                        src += line_ofs;
                        dest += line_ofs;
                }
        } else {
                line_ofs = info->fix.line_length - width;
                dest = info->screen_base + dx + width + 
				(dy + height - 1) * info->fix.line_length;
                src = info->screen_base + sx + width + 
				(sy + height - 1) * info->fix.line_length;
                while (height--) {
                        for (x = 0; x < width; x++) {
                                dest--;
                                src--;
                                fb_readb(src);
                                fb_writeb(0, dest);
                        }
                        src -= line_ofs;
                        dest -= line_ofs;
                }
        }
	return;
}

static void vga16fb_imageblit(struct fb_info *info, struct fb_image *image)
{
	struct vga_hw_state *par = (struct vga_hw_state *)info->par;
	unsigned int height = image->height, width = image->width;
	int x1, y1, dx = image->x, dy = image->y;
	char *dest, *pic = image->data;
	caddr_t regs = par->regsbase;
	
	dest = info->screen_base+dx + dy * info->fix.line_length * height;

	if (image->depth == 1) {
		vga_wgfx(regs, VGA_GFX_MODE, 2);
	        vga_wgfx(regs, VGA_GFX_DATA_ROTATE, 0);
        	vga_wgfx(regs, VGA_GFX_SR_ENABLE, 0xf);
        	vga_wgfx(regs, VGA_GFX_SR_VALUE, image->fg_color);
       	 	vga_wgfx(regs, VGA_GFX_BIT_MASK, 0xff);

        	writeb(image->bg_color, dest);
        	rmb();
        	fb_readb(dest); /* fill latches */
		vga_wgfx(regs, VGA_GFX_MODE, 3);
        	wmb();
        	for (y1=dy;y1<(dy + height);y1++,dest += info->fix.line_length)
                	fb_writeb(*pic, dest);
        	wmb();
	} else {
        	if (info->var.bits_per_pixel == 4 && 
		    info->fix.type == FB_TYPE_VGA_PLANES) {
			vga_wgfx(regs, VGA_GFX_SR_ENABLE, 0xf);                
			vga_wgfx(regs, VGA_GFX_DATA_ROTATE, 0);	
			vga_wgfx(regs, VGA_GFX_MODE, 0);
                	
                	for (y1 = dy; y1 < (dy + height); y1++) {
                        	for (x1 = dx; x1 < (dx + width); x1++) {
                                	dest = info->screen_base + y1*info->fix.line_length + x1/4 + dx/8;
					vga_wgfx(regs,VGA_GFX_SR_VALUE,*pic>>4);
                                	vga_wgfx(regs,VGA_GFX_BIT_MASK, 1 << (7 - x1%4*2));
                                	fb_readb(dest);
                                	fb_writeb(0, dest);

					vga_wgfx(regs,VGA_GFX_SR_VALUE, *pic&0xf);
                                	vga_wgfx(regs,VGA_GFX_BIT_MASK, 1 << (7 - x1%4*2));
                                	fb_readb(dest);
                                	fb_writeb(0, dest);
                                	pic++;
                       	 	}
                	}
		}
	}
	return;
}

static struct fb_ops vga16fb_ops = {
	owner:		THIS_MODULE,
	fb_check_var:	vga16fb_check_var,
	fb_set_par:	vga16fb_set_par,
	fb_setcolreg:	vga16fb_setcolreg,
	fb_blank:	vga16fb_blank,
	fb_pan_display:	vga16fb_pan_display,
        fb_fillrect:    vga16fb_fillrect,
        fb_copyarea:    vga16fb_copyarea,
        fb_imageblit:   vga16fb_imageblit,
};

int __init vga16fb_setup(char *options)
{
	return 0;
}

int __init vga16fb_init(void)
{
	int length, err;

	printk(KERN_DEBUG "vga16fb: initializing\n");

	/* XXX share VGA_FB_PHYS region with vgacon */
        vga16fb.screen_base = ioremap(VGA_FB_PHYS, VGA_FB_PHYS_LEN);
	if (!vga16fb.screen_base) {
                printk(KERN_ERR "vga16fb: unable to map device\n");
                return -ENOMEM;
        }  
	printk(KERN_INFO "vga16fb: mapped to 0x%p\n", vga16fb.screen_base);
	default_par.regsbase = NULL;
	default_par.palette_blanked = 0;
	default_par.vesa_blanked = 0;

	if (!ORIG_VIDEO_ISVGA) {
		default_par.video_type = VIDEO_TYPE_EGAC;
		length = 2;
	} else {
		default_par.video_type = VIDEO_TYPE_VGAC;
		length = 6; 	
	}
	
	vga16fb_defined.red.length   = length;
	vga16fb_defined.green.length = length;
	vga16fb_defined.blue.length  = length;	

	/* XXX share VGA I/O region with vgacon and others */
	vga16fb.par = &default_par;

	vga16fb.node = -1;
	vga16fb.fix = vga16fb_fix;
	vga16fb.var = vga16fb_defined;
	vga16fb.fbops = &vga16fb_ops;
	vga16fb.flags=FBINFO_FLAG_DEFAULT;
	err = fb_alloc_cmap(&vga16fb.cmap, 16, 0);
	if (err)
		return err;
		
	if (register_framebuffer(&vga16fb) < 0) {
		iounmap(vga16fb.screen_base);
		return -EINVAL;
	}

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(vga16fb.node), vga16fb.fix.id);
	
	return 0;
}

static void __exit vga16fb_exit(void)
{
    unregister_framebuffer(&vga16fb);	
    iounmap(vga16fb.screen_base);
}

#ifdef MODULE
module_init(vga16fb_init);
module_exit(vga16fb_exit);
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
