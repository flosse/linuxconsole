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
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <video/fbcon.h>
#include <video/fbcon-vga-planes.h>
#include <video/fbcon-vga.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include "vga.h"

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
	FB_ACTIVATE_NOW,
	-1,-1,
	0,
	39721, 48, 16, 39, 8,
	96, 2, 0,	/* No sync info */
	FB_VMODE_NONINTERLACED,
	{0,0,0,0,0,0}
};

static struct fb_fix_screeninfo vga16fb_fix __initdata = {
    "VGA16 VGA", VGA_FB_PHYS, VGA_FB_PHYS_LEN, FB_TYPE_VGA_PLANES,  
    FB_AUX_VGA_PLANES_VGA4, FB_VISUAL_PSEUDOCOLOR, 8, 1, 0, 640/8, 
    (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static struct display disp;
static int currcon   = 0;

/* --------------------------------------------------------------------- */

static void vga16fb_pan_var(struct fb_info *info, 
			    struct fb_var_screeninfo *var,
			    struct display *p)
{
	u32 pos;
	u32 xoffset;

	xoffset = var->xoffset;
	if (var->bits_per_pixel == 8) {
		pos = (var->xres_virtual * var->yoffset + xoffset) >> 2;
	} else if (var->bits_per_pixel == 0) {
		int fh = fontheight(p);
		if (!fh) fh = 16;
		pos = (var->xres_virtual * (var->yoffset / fh) + xoffset) >> 3;
	} else {
		if (var->nonstd)
			xoffset--;
		pos = (var->xres_virtual * var->yoffset + xoffset) >> 3;
	}
	vga_io_wcrt(VGA_CRTC_START_HI, pos >> 8);
	vga_io_wcrt(VGA_CRTC_START_LO, pos & 0xFF);
	/* if we support CFB4, then we must! support xoffset with pixel 
	   granularity */
	/* if someone supports xoffset in bit resolution */
	vga_io_r(VGA_IS1_RC);		/* reset flip-flop */
	vga_io_w(VGA_ATT_IW, VGA_ATC_PEL);
	if (var->bits_per_pixel == 8)
		vga_io_w(VGA_ATT_IW, (xoffset & 3) << 1);
	else
		vga_io_w(VGA_ATT_IW, xoffset & 7);
	vga_io_r(VGA_IS1_RC);
	vga_io_w(VGA_ATT_IW, 0x20);
}

static int vga16fb_update_var(int con, struct fb_info *info)
{
	struct display* p;

	p = (con < 0) ? info->disp : fb_display + con;
	vga16fb_pan_var(info, &p->var, p);
	return 0;
}

static int vga16fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			   struct fb_info *info)
{
	*fix = info->fix;
	return 0;
}

static int vga16fb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info)
{	
	*var = info->var;
	return 0;
}

static void vga16fb_set_disp(int con, struct fb_info *info)
{
	struct vga_hw_state *par = (struct vga_hw_state *) info->par;
	struct display *display;

	display = (con < 0) ? info->disp : fb_display + con;
	
	display->screen_base = info->screen_base;
	display->visual = info->fix.visual;
	display->type = info->fix.type;
	display->type_aux = info->fix.type_aux;
	display->ypanstep = info->fix.ypanstep;
	display->ywrapstep = info->fix.ywrapstep;
	display->line_length = info->fix.line_length;
	display->next_line = info->fix.line_length;
	display->can_soft_blank = 1;
	display->inverse = 0;

	switch (display->type) {
#ifdef FBCON_HAS_VGA_PLANES
		case FB_TYPE_VGA_PLANES:
			if (display->type_aux == FB_AUX_VGA_PLANES_VGA4) {
				if (par->video_type==VIDEO_TYPE_VGAC)
					display->dispsw = &fbcon_vga_planes;
				else
					display->dispsw = &fbcon_ega_planes;
			} else
				display->dispsw = &fbcon_vga8_planes;
			break;
#endif
#ifdef FBCON_HAS_VGA
		case FB_TYPE_TEXT:
			display->dispsw = &fbcon_vga;
			break;
#endif
		default: /* only FB_TYPE_PACKED_PIXELS */
			switch (display->var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
				case 4:
					display->dispsw = &fbcon_cfb4;
					break;
#endif
#ifdef FBCON_HAS_CFB8
				case 8: 
					display->dispsw = &fbcon_cfb8;
					break;
#endif
				default:
					display->dispsw = &fbcon_dummy;
			}
			break;
	}
}

#define FAIL(X) return -EINVAL

#define MODE_SKIP4	1
#define MODE_8BPP	2
#define MODE_CFB	4
#define MODE_TEXT	8
static int vga16fb_check_var(struct fb_var_screeninfo *var, 
			      const struct fb_info *info,
			      struct display *p)
{
	const struct vga_hw_state *par = (struct vga_hw_state *) info->par;
	u32 xres, right, hslen, left, yres, lower, vslen, upper;
	u32 vxres, xoffset, vyres, yoffset, maxmem;
	u8 shift = 0, double_scan = 0;

	if (var->bits_per_pixel == 4) {
		if (var->nonstd) {
#ifdef FBCON_HAS_CFB4
			if (par->video_type != VIDEO_TYPE_VGAC)
				return -EINVAL;
			shift = 3;
			maxmem = 16384;
#else
			return -EINVAL;
#endif
		} else {
#ifdef FBCON_HAS_VGA_PLANES
			shift = 3;
			maxmem = 65536;
#else
			return -EINVAL;
#endif
		}
	} else if (var->bits_per_pixel == 8) {
		if (par->video_type != VIDEO_TYPE_VGAC)
			return -EINVAL;	/* only supported on VGA */
		shift = 2;
		if (var->nonstd) {
#ifdef FBCON_HAS_VGA_PLANES
			maxmem = 65536;
#else
			return -EINVAL;
#endif
		} else {
#ifdef FBCON_HAS_CFB8
			maxmem = 16384;
#else
			return -EINVAL;
#endif
		}
#ifdef FBCON_HAS_VGA	
	} else if (var->bits_per_pixel == 0) {
		int fh;

		fh = fontheight(p);
		if (!fh)
			fh = 16;
		maxmem = 32768 * fh;
		shift = 3;
#endif	
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

static void vga16fb_load_font(struct display* p) {
       int chars;
       unsigned char* font;
       unsigned char* dest;

       if (!p || !p->fontdata)
               return;
       chars = 256;
       font = p->fontdata;
       dest = p->fb_info->screen_base;

       vga_io_wseq(VGA_SEQ_RESET, 0x01);
       vga_io_wseq(VGA_SEQ_PLANE_WRITE, 0x04);
       vga_io_wseq(VGA_SEQ_MEMORY_MODE, 0x07);
       vga_io_wseq(VGA_SEQ_RESET, 0x03);
       vga_io_wgfx(VGA_GFX_MODE, 0x00);
       vga_io_wgfx(VGA_GFX_MISC, 0x04);
       while (chars--) {
               int i;

               for (i = fontheight(p); i > 0; i--)
                       writeb(*font++, dest++);
               dest += 32 - fontheight(p);
       }
       vga_io_wseq(VGA_SEQ_RESET, 0x01);
       vga_io_wseq(VGA_SEQ_PLANE_WRITE, 0x03);
       vga_io_wseq(VGA_SEQ_MEMORY_MODE, 0x03);
       vga_io_wseq(VGA_SEQ_RESET, 0x03);
       vga_io_wgfx(VGA_GFX_MODE, 0x10);
       vga_io_wgfx(VGA_GFX_MISC, 0x06);
}

static int vga16fb_set_par(struct fb_info *info, struct display *p)
{
	struct vga_hw_state *par = (struct vga_hw_state *) info->par;
	int shift, double_scan = 0;

	par->pel_mask = 0xFF;

	if (info->var.bits_per_pixel == 4) {
		if (info->var.nonstd) {
#ifdef FBCON_HAS_CFB4
			info->fix.type = FB_TYPE_PACKED_PIXELS;
			info->fix.line_length = info->var.xres_virtual / 2;
			par->mode = MODE_SKIP4 | MODE_CFB;
			par->pel_mask = 0x0F;
			par->shift = 3; 
#endif
		} else {
#ifdef FBCON_HAS_VGA_PLANES
			info->fix.type = FB_TYPE_VGA_PLANES;
			info->fix.type_aux = FB_AUX_VGA_PLANES_VGA4;
			info->fix.line_length = info->var.xres_virtual / 8;
                        par->mode = 0;
			par->shift = 3;
#endif	
		}
#ifdef FBCON_HAS_VGA
	} else if (info->var.bits_per_pixel == 0) {
		int fh;

		info->fix.type = FB_TYPE_TEXT;
		info->fix.type_aux = FB_AUX_TEXT_CGA;
		info->fix.line_length = info->var.xres_virtual / 4;
                par->mode = MODE_TEXT;
                fh = fontheight(p);
                if (!fh)
                       fh = 16;
		par->shift = 3;
#endif
	} else {	/* 8bpp */
                if (info->var.nonstd) {
#ifdef FBCON_HAS_VGA_PLANES
                        par->mode = MODE_8BPP | MODE_CFB;
			info->fix.type = FB_TYPE_VGA_PLANES;
                        info->fix.type_aux = FB_AUX_VGA_PLANES_CFB8;
                        info->fix.line_length = info->var.xres_virtual / 4;
			par->shift = 2;
#endif
                } else {
#ifdef FBCON_HAS_CFB8
                        par->mode = MODE_SKIP4 | MODE_8BPP | MODE_CFB;
			info->fix.type = FB_TYPE_PACKED_PIXELS;
                        info->fix.line_length = info->var.xres_virtual;
			par->shift = 2;
#endif
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
	if (par->mode & MODE_TEXT)
		vga16fb_load_font(p);
	return 0;
}

static void ega16_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	static unsigned char map[] = { 000, 001, 010, 011 };
	int val;
	
	if (regno >= 16)
		return;
	val = map[red>>14] | ((map[green>>14]) << 1) | ((map[blue>>14]) << 2);
	vga_io_r(VGA_IS1_RC);   /* ! 0x3BA */
	vga_io_wattr(regno, val);
	vga_io_r(VGA_IS1_RC);   /* some clones need it */
	vga_io_w(VGA_ATT_IW, 0x20); /* unblank screen */
}

static void vga16_setpalette(int regno, unsigned red, unsigned green, unsigned blue)
{
	vga_io_w(VGA_PEL_IW, regno);
	vga_io_w(VGA_PEL_D, red   >> 10);
	vga_io_w(VGA_PEL_D, green >> 10);
	vga_io_w(VGA_PEL_D, blue  >> 10);
}

static int vga16_setcolreg(unsigned regno, unsigned red, unsigned green,
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
		vga16_setpalette(regno,red,green,blue);
	else
		ega16_setpalette(regno,red,green,blue);
	return 0;
}

static int vga16fb_set_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
	struct display *display;
	int err;

	if (con < 0)
		display = info->disp;
	else
		display = fb_display + con;
	
	if (memcmp(&info->var, var, sizeof(var))) {
		if ((err = vga16fb_check_var(var, info, display)) != 0)
			return err;
	
		if ((var->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
			display->var = info->var = *var;
			vga16fb_set_par(info, display);
			vga16fb_set_disp(con, info);
			if (info->changevar)
				info->changevar(con);
			fb_set_cmap(&info->cmap, 1, vga16_setcolreg, info);
		}
	}
	return 0;
}

static int vga16fb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
	if (con == currcon) /* current console? */
		fb_copy_cmap(&info->cmap, cmap, kspc ? 0 : 2);
	else if (fb_display[con].cmap.len) /* non default colormap? */
		fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap(fb_default_cmap(16), cmap, kspc ? 0 : 2);
	return 0;
}

static int vga16fb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
	if (con == currcon)			/* current console? */
		return fb_set_cmap(cmap, kspc, vga16_setcolreg, info);
	else
		fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0 : 1);
	return 0;
}

static int vga16fb_pan_display(struct fb_var_screeninfo *var, int con,
			       struct fb_info *info) 
{
	if (var->xoffset + fb_display[con].var.xres > fb_display[con].var.xres_virtual ||
	    var->yoffset + fb_display[con].var.yres > fb_display[con].var.yres_virtual)
		return -EINVAL;
	if (con == currcon) {
		vga16fb_pan_var(info, var, fb_display+con);
		info->var.xoffset = var->xoffset;
		info->var.yoffset = var->yoffset;
		info->var.vmode &= ~FB_VMODE_YWRAP;
	}
	fb_display[con].var.xoffset = var->xoffset;
	fb_display[con].var.yoffset = var->yoffset;
	fb_display[con].var.vmode &= ~FB_VMODE_YWRAP;
	return 0;
}

static struct fb_ops vga16fb_ops = {
	owner:		THIS_MODULE,
	fb_get_fix:	vga16fb_get_fix,
	fb_get_var:	vga16fb_get_var,
	fb_set_var:	vga16fb_set_var,
	fb_get_cmap:	vga16fb_get_cmap,
	fb_set_cmap:	vga16fb_set_cmap,
	fb_pan_display:	vga16fb_pan_display,
};

int __init vga16fb_setup(char *options)
{
	char *this_opt;
	
	vga16fb.fontname[0] = '\0';
	
	if (!options || !*options)
		return 0;
	
	for(this_opt=strtok(options,","); this_opt; this_opt=strtok(NULL,",")) {
		if (!*this_opt) continue;
		
		if (!strncmp(this_opt, "font:", 5))
			strcpy(vga16fb.fontname, this_opt+5);
	}
	return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static void vga16fb_blank(int blank, struct fb_info *info)
{
	struct vga_hw_state *par = (struct vga_hw_state *) info->par;

	switch (blank) {
	case 0:				/* Unblank */
		if (par->vesa_blanked) {
			vga_vesa_unblank(par);
			par->vesa_blanked = 0;
		}
		if (par->palette_blanked) {
			fb_set_cmap(&info->cmap, 1, vga16_setcolreg, info);
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
}

int __init vga16fb_init(void)
{
	int length, err;

	printk(KERN_DEBUG "vga16fb: initializing\n");

	/* XXX share VGA_FB_PHYS region with vgacon */

        vga16fb.screen_base = ioremap(VGA_FB_PHYS, VGA_FB_PHYS_LEN);
	printk(KERN_INFO "vga16fb: mapped to 0x%p\n", vga16fb.screen_base);
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
	disp.var = vga16fb.var = vga16fb_defined;

	/* name should not depend on EGA/VGA */
	strcpy(vga16fb.modename, "VGA16 VGA");
	vga16fb.node = -1;
	vga16fb.fix = vga16fb_fix;
	vga16fb.fbops = &vga16fb_ops;
	vga16fb.blank=&vga16fb_blank;
	vga16fb.flags=FBINFO_FLAG_DEFAULT;
	err = fb_alloc_cmap(&vga16fb.cmap, 16, 0);
	if (err)
		return err;
	vga16fb_set_disp(-1, &vga16fb);
	
	if (register_framebuffer(&vga16fb)<0)
		return -EINVAL;

	printk(KERN_INFO "fb%d: %s frame buffer device\n",
	       GET_FB_IDX(vga16fb.node), vga16fb.modename);
	
	return 0;
}

static void __exit vga16fb_exit(void)
{
    /* XXX unshare VGA regions 
    release_vt(&fb_con); */
    unregister_framebuffer(&vga16fb);	
    iounmap(vga16fb.screen_base);
    /* take_over_console(&some_vt, &vga_con); */
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
