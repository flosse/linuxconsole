/*
 *  linux/drivers/video/fbcon.c -- Low level frame buffer based console driver
 *
 *	Copyright (C) 1995 Geert Uytterhoeven
 *
 *
 *  This file is based on the original Amiga console driver (amicon.c):
 *
 *	Copyright (C) 1993 Hamish Macdonald
 *			   Greg Harp
 *	Copyright (C) 1994 David Carter [carter@compsci.bristol.ac.uk]
 *
 *	      with work by William Rucklidge (wjr@cs.cornell.edu)
 *			   Geert Uytterhoeven
 *			   Jes Sorensen (jds@kom.auc.dk)
 *			   Martin Apel
 *
 *  and on the original Atari console driver (atacon.c):
 *
 *	Copyright (C) 1993 Bjoern Brauel
 *			   Roman Hodek
 *
 *	      with work by Guenther Kelleter
 *			   Martin Schaller
 *			   Andreas Schwab
 *
 *  Hardware cursor support added by Emmanuel Marty (core@ggi-project.org)
 *  Smart redraw scrolling, arbitrary font width support, 512char font support
 *  and software scrollback added by 
 *                         Jakub Jelinek (jj@ultra.linux.cz)
 *
 *  Random hacking by Martin Mares <mj@ucw.cz>
 *
 *
 *  The low level operations for the various display memory organizations are
 *  now in separate source files.
 *
 *  Currently the following organizations are supported:
 *
 *    o afb			Amiga bitplanes
 *    o cfb{2,4,8,16,24,32}	Packed pixels
 *    o ilbm			Amiga interleaved bitplanes
 *    o iplan2p[248]		Atari interleaved bitplanes
 *    o mfb			Monochrome
 *    o vga			VGA characters/attributes
 *
 *  To do:
 *
 *    - Implement 16 plane mode (iplan2p16)
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#undef FBCONDEBUG

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/delay.h>	/* MSch: for IRQ probe */
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/smp.h>
#include <linux/init.h>

#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_ATARI
#include <asm/atariints.h>
#endif
#ifdef CONFIG_MAC
#include <asm/macints.h>
#endif
#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/machdep.h>
#include <asm/setup.h>
#endif
#ifdef CONFIG_FBCON_VGA_PLANES
#include <asm/io.h>
#endif
#define INCLUDE_LINUX_LOGO_DATA
#include <asm/linux_logo.h>

#include "fbcon.h"
#include "font.h"

#ifdef FBCONDEBUG
#  define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#  define DPRINTK(fmt, args...)
#endif

#define LOGO_H			80
#define LOGO_W			80
#define LOGO_LINE	(LOGO_W/8)

#define REFCOUNT(fd)	(((int *)(fd))[-1])
#define FNTSIZE(fd)	(((int *)(fd))[-2])
#define FNTCHARCNT(fd)	(((int *)(fd))[-3])
#define FNTSUM(fd)	(((int *)(fd))[-4])
#define FONT_EXTRA_WORDS 4

#define CM_SOFTBACK	(8)

#define advance_row(p, delta) (unsigned short *)((unsigned long)(p) + (delta) * vc->vc_size_row)

static char __initdata fontname[40] = { 0 };

/*
 * Emmanuel: fbcon will now use a hardware cursor if the
 * low-level driver provides a non-NULL dispsw->cursor pointer,
 * in which case the hardware should do blinking, etc.
 *
 * if dispsw->cursor is NULL, use Atari alike software cursor
 */

#define CURSOR_DRAW_DELAY		(1)

/* # VBL ints between cursor state changes */
#define ARM_CURSOR_BLINK_RATE		(10)
#define AMIGA_CURSOR_BLINK_RATE		(20)
#define ATARI_CURSOR_BLINK_RATE		(42)
#define MAC_CURSOR_BLINK_RATE		(32)
#define DEFAULT_CURSOR_BLINK_RATE	(20)

#define divides(a, b)	((!(a) || (b)%(a)) ? 0 : 1)

/*
 *  Interface used by the world
 */

static const char *fbcon_startup(struct vt_struct *vt, int init);
static void fbcon_init(struct vc_data *vc);
static void fbcon_deinit(struct vc_data *vc);
static void fbcon_clear(struct vc_data *vc,int sx,int sy,int width,int height);
static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos);
static void fbcon_putcs(struct vc_data *vc,const unsigned short *s, int count,
			int ypos, int xpos);
static void fbcon_cursor(struct vc_data *vc, int mode);
static int fbcon_scroll_region(struct vc_data *vc, int t, int b, int dir,
			 	int count);
static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width);
static int fbcon_blank(struct vc_data *vc, int blank);
static int fbcon_font_op(struct vc_data *vc, struct console_font_op *op);
static int fbcon_resize(struct vc_data *vc,unsigned int cols,unsigned int rows);
static int fbcon_set_palette(struct vc_data *vc, unsigned char *table);
static int fbcon_scroll(struct vc_data *vc, int lines);
static int fbcon_set_origin(struct vc_data *vc);
static void fbcon_invert_region(struct vc_data *vc, u16 *p, int cnt);

/*
 *  Internal routines
 */

static int __init fbcon_setup(char *options);

static int __init fbcon_setup(char *options)
{
    if (!options || !*options)
            return 0;

    if (!strncmp(options, "font:", 5))
        strcpy(fontname, options+5);
    return 0;
}

__setup("fbcon=", fbcon_setup);

/*
 *  Low Level Operations
 */

/* NOTE: fbcon cannot be __init: it may be called from take_over_console later */
static const char *fbcon_startup(struct vt_struct *vt, int init)
{
    const char *display_desc = "frame buffer device";
    struct fbcon_font_desc *font = NULL;
    struct fb_info *info;
    struct module *owner;
    struct vc_data *vc;		
    int logo;	

    /*
     *  If num_registered_fb is zero, this is a call for the dummy part.
     *  The frame buffer devices weren't initialized yet.
     */
    if (!num_registered_fb)
        return NULL;

    info = registered_fb[num_registered_fb-1];
    if (!info)	return NULL;	
    owner = info->fbops->owner;	

    if (owner)
        __MOD_INC_USE_COUNT(owner);
    if (info->fbops->fb_open && info->fbops->fb_open(info, 0) && owner)
         __MOD_DEC_USE_COUNT(owner);

    if (info->flags & FBINFO_FLAG_MODULE)
        logo = 0;

    if (!fontname[0] || !(font = fbcon_find_font(fontname))) 
	font = fbcon_get_default_font(info->var.xres, info->var.yres);

    if (!font) {
	printk(KERN_ERR "fbcon_startup: No default font detect.\n");	
	return NULL;
    }

#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
    if (font->width%8) {
        /* ++Geert: changed from panic() to `correct and continue' */
        printk(KERN_ERR "fbcon_startup: No support for fontwidth %d\n", font->width);
    }
#endif

    vc = (struct vc_data *) kmalloc(sizeof(struct vc_data), GFP_KERNEL);	
    vt->default_mode = vc;	

    vc->vc_font.width = font->width;
    vc->vc_font.height = font->height;
    vc->vc_font.charcount = 256; /* Gross hack :-( */ 
    vc->vc_font.data = font->data;

    info->var.xoffset = info->var.yoffset = 0;  /* reset wrap/pan */
    info->var.activate = FB_ACTIVATE_NOW;	

    /* We trust the driver supplied us with a vaild resolution */	
    if (info->fbops->fb_set_par)
	info->fbops->fb_set_par(info);	

    /* clear out the cmap so we don't have dangling pointers */
    info->cmap.red = info->cmap.green = NULL;
    info->cmap.blue = info->cmap.transp = NULL;
    info->cmap.len = 0;
 
    /* Create the default cursor 	
    info->cursor.set = FB_CUR_SETALL;
    info->cursor.enable = 1;
    info->cursor.pos.x = vc->vc_x * vc->vc_font.width;
    info->cursor.pos.y = vc->vc_y * vc->vc_font.height;
    info->cursor.hot.x = info->cursor.pos.x;
    info->cursor.hot.y = info->cursor.pos.y;
    info->cursor.cmap.len = 2;
    info->cursor.cmap.start = 0;
    fb_alloc_cmap(&info->cursor.cmap, 0, 0);
    info->cursor.size.x = vc->vc_font.width;
    info->cursor.size.y = vc->vc_font.height;
    info->fbops->fb_cursor(info, &info->cursor); 
*/
    DPRINTK("mode:   %s\n", info->fix.id);
    DPRINTK("visual: %d\n", info->fix.visual);
    DPRINTK("res:    %dx%d-%d\n",info->var.xres, info->var.yres, info->var.bits_per_pixel); 
    DPRINTK("Using %dx%d resolution\n", vc->vc_cols, vc->vc_rows);
    DPRINTK("With %dx%d font set\n", vc->vc_font.width, vc->vc_font.height);
	
    vc->vc_cols = info->var.xres/vc->vc_font.width;
    vc->vc_rows = info->var.yres/vc->vc_font.height;
    //vc->vc_scrollback = info->var.yres_virtual/info->var.yres;
    vc->vc_scrollback = 1;	

    vc->vc_can_do_color = (info->var.bits_per_pixel != 1);
    vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
    
    if (vc->vc_font.charcount == 256) 
        vc->vc_hi_font_mask = 0;
    else 
	vc->vc_hi_font_mask = 0x100;

    if (logo) {
	/* Need to make room for the logo */
    }
    if (info->pm_fb)	
    	vt->pm_con = info->pm_fb;	
    vt->data_hook = info;
    return display_desc;
}

static void fbcon_init(struct vc_data *vc)
{
    vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
}

static void fbcon_deinit(struct vc_data *vc)
{
/*
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;

    fbcon_free_font(info);
*/
}

static void fbcon_clear(struct vc_data *vc,int sx,int sy, int width,int height)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    unsigned long color;

    if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR) 
	color = attr_bgcol_ec(vc);
    else {
	if (info->var.bits_per_pixel > 16)
		color = ((u32*)info->pseudo_palette)[attr_bgcol_ec(vc)];
	else
		color = ((u16*)info->pseudo_palette)[attr_bgcol_ec(vc)];
    }
    sx *= vc->vc_font.width;
    sy *= vc->vc_font.height;
    width *= vc->vc_font.width;
    height *= vc->vc_font.height;		

    DPRINTK("Calling clear screen with width %d, height %d at %d,%d\n", 
	     width, height, sx, sy);

    info->fbops->fb_fillrect(info, sx, sy, width, height, color, ROP_COPY);	
}

static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    unsigned short charmask = (vc->vc_font.charcount > 256) ? 0x1ff: 0xff;
    unsigned int width = ((vc->vc_font.width+7)>>3);	
    struct fb_image image; 	
  
    if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
	image.fg_color = attr_fgcol(vc, c);
	image.bg_color = attr_bgcol(vc, c);
    } else {
	if (info->var.bits_per_pixel > 16) {
		image.fg_color = ((u32*)info->pseudo_palette)[attr_fgcol(vc,c)];
		image.bg_color = ((u32*)info->pseudo_palette)[attr_bgcol(vc,c)];
	} else {
		image.fg_color = ((u16*)info->pseudo_palette)[attr_fgcol(vc,c)];
		image.bg_color = ((u16*)info->pseudo_palette)[attr_bgcol(vc,c)];
    	}
    }

    image.x = xpos * vc->vc_font.width;
    image.y = ypos * vc->vc_font.height;		 
    image.width = vc->vc_font.width;
    image.height = vc->vc_font.height;
    image.depth = 1;	
    image.data = vc->vc_font.data + (c & charmask)*vc->vc_font.height*width;

    DPRINTK("Drawing a character with width %d, height %d at %d,%d\n", 
	     image.width, image.height, image.x, image.y);

    info->fbops->fb_imageblit(info, &image);
}

static void fbcon_putcs(struct vc_data *vc, const unsigned short *s, 
			int count, int ypos, int xpos)
{
   struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
   unsigned short charmask = (vc->vc_font.charcount > 256) ? 0x1ff: 0xff;
   unsigned int width = ((vc->vc_font.width+7)>>3);	
   struct fb_image image; 	
  
   if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
	image.fg_color = attr_fgcol(vc, *s);
	image.bg_color = attr_bgcol(vc, *s);
   } else {
	if (info->var.bits_per_pixel > 16) {
		image.fg_color = ((u32*)info->pseudo_palette)[attr_fgcol(vc,*s)];
		image.bg_color = ((u32*)info->pseudo_palette)[attr_bgcol(vc,*s)];
	} else {
		image.fg_color = ((u16*)info->pseudo_palette)[attr_fgcol(vc,*s)];
		image.bg_color = ((u16*)info->pseudo_palette)[attr_bgcol(vc,*s)];
    	}
   }
   image.x = xpos * vc->vc_font.width;
   image.y = ypos * vc->vc_font.height;		 
   image.width = vc->vc_font.width;
   image.height = vc->vc_font.height;
   image.depth = 1;	
 
   while (count--) {
    	image.data = vc->vc_font.data + 
			(scr_readw(s++) & charmask)*vc->vc_font.height*width;
    	info->fbops->fb_imageblit(info, &image);
	image.x += vc->vc_font.width;
   }	
}

static void fbcon_cursor(struct vc_data *vc, int mode)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    struct fbcursor cursor;	
    int i;	
 
    fbcon_set_origin(vc);

    /* Avoid flickering if there's no real change. */
    if (info->cursor.pos.x == vc->vc_x * vc->vc_font.width && 
	info->cursor.pos.y == vc->vc_y * vc->vc_font.height &&
        (mode == CM_ERASE) == !cursor.enable)
        return;

    switch (mode) {
        case CM_ERASE:
		if (info->fbops->fb_cursor) {
			info->cursor.enable = 0;
			info->cursor.set = FB_CUR_SETCUR;
			info->fbops->fb_cursor(info, &info->cursor);
		} else {
			unsigned int sx = vc->vc_x * vc->vc_font.width;
			unsigned int sy = vc->vc_y * vc->vc_font.height; 
			unsigned long color = -1;

    			info->fbops->fb_fillrect(info, sx, sy,vc->vc_font.width,
					vc->vc_font.height, color, ROP_XOR);	
		}
            	break;
        case CM_MOVE:
        case CM_DRAW:
		if (info->fbops->fb_cursor) {
			info->cursor.set = FB_CUR_SETCUR|FB_CUR_SETPOS|FB_CUR_SETHOT;
			info->cursor.enable = 1;
			info->cursor.pos.x = vc->vc_x * vc->vc_font.width;
			info->cursor.pos.y = vc->vc_y * vc->vc_font.height;
			info->cursor.hot.x = info->cursor.pos.x;
			info->cursor.hot.y = info->cursor.pos.y;
			info->fbops->fb_cursor(info, &info->cursor); 
		} else {
			unsigned int sx = vc->vc_x * vc->vc_font.width;
			unsigned int sy = vc->vc_y * vc->vc_font.height; 
			unsigned long color = -1;

    			info->fbops->fb_fillrect(info, sx, sy,vc->vc_font.width,
					vc->vc_font.height, color, ROP_XOR);	
		}
		break;
	case CM_CHANGE: {
		int top_scanline, bottom_scanline = vc->vc_font.height;

		if (bottom_scanline >= 10) bottom_scanline--;
		switch (vc->vc_cursor_type & CUR_HWMASK) {
			case CUR_NONE:
				top_scanline = bottom_scanline;
				break;
			case CUR_BLOCK:
				top_scanline = 0;
				bottom_scanline = vc->vc_font.height;
				break;
			case CUR_TWO_THIRDS:
				top_scanline = bottom_scanline/3;
				break;
			case CUR_LOWER_THIRD:
				top_scanline = (bottom_scanline*2)/3;
				break;
			case CUR_LOWER_HALF:
				top_scanline = bottom_scanline/2;
				break;
                	case CUR_UNDERLINE:
			default:
				top_scanline = bottom_scanline - 2;
				break;
		}

		if (info->fbops->fb_cursor) {
			char *pixmap, *mask;

			cursor.size.x = vc->vc_font.width;
    			cursor.size.y = vc->vc_font.height;
			cursor.cmap.len = 2;
			cursor.cmap.start = 0;
			fb_alloc_cmap(&cursor.cmap, 0, 0);

			pixmap = kmalloc(((cursor.size.x * cursor.size.y) >> 3),
						GFP_KERNEL);
			mask = kmalloc(((cursor.size.x * cursor.size.y) >> 3),
						GFP_KERNEL);

			for (i = 0; i < top_scanline; i++) 
				pixmap[i] = 0; mask[i] = -1;
			for (;i < bottom_scanline; i++) 
				pixmap[i] = -1; mask[i] = -1;
		
			cursor.image = pixmap;
			cursor.mask = mask;
			info->fbops->fb_cursor(info, &cursor); 
		} else {
			unsigned int sx = vc->vc_x * vc->vc_font.width;
			unsigned int sy = vc->vc_y * vc->vc_font.height; 
			unsigned long color = -1;

    			info->fbops->fb_fillrect(info, sx, sy,vc->vc_font.width,
					vc->vc_font.height, color, ROP_XOR);	
		}
            	break;
        }
    }
}

static int fbcon_scroll_region(struct vc_data *vc, int t, int b, int dir, 
				int count)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    unsigned int height = (b-t-count) * vc->vc_font.height;
    unsigned int sy = 0, dy = 0;	 	

    switch (dir) {
	case SM_UP:
		sy = (t + count) * vc->vc_font.height;
		dy = t * vc->vc_font.height;
		break;
	case SM_DOWN:
		sy = t * vc->vc_font.height; 
		dy = (t + count) * vc->vc_font.height;
		break;
    }		

    info->fbops->fb_copyarea(info, 0, sy, info->var.xres, height, 0, dy);
    return 0;
}

static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    
    sx *= vc->vc_font.width;
    sy *= vc->vc_font.height;
    dx *= vc->vc_font.width;
    dy *= vc->vc_font.height;
    height *= vc->vc_font.height;
    width *= vc->vc_font.width;		
        	
    DPRINTK("Calling bmove to move a region of width %d and height %d to go from %d,%d to %d,%d\n", width, height, sx, sy, dx, dy); 
    info->fbops->fb_copyarea(info, sx, sy, width, height, dx, dy);
}

static int fbcon_blank(struct vc_data *vc, int blank)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;

    if (info->fbops->fb_blank) {
	info->fbops->fb_blank(blank, info);
    } else {
	if (info->var.accel_flags != FB_ACCEL_NONE) {
		unsigned long color = 0;		

		info->fbops->fb_fillrect(info, 0, 0, info->var.xres, 
					 info->var.yres, color, ROP_COPY);	
	} else {
		if ((info->fix.visual == FB_VISUAL_PSEUDOCOLOR || 
            	    info->fix.visual == FB_VISUAL_DIRECTCOLOR) && blank) {
			struct fb_cmap cmap;
    			u16 black[16];

       			memset(black, 0, 16*sizeof(u16));
       			cmap.red = black;
       			cmap.green = black;
       			cmap.blue = black;
       			cmap.transp = NULL;
       			cmap.start = 0;
      			cmap.len = 16;
       			fb_set_cmap(&cmap, 1, info);
		}
	}
    }			
    return 0;
}

static int fbcon_font_op(struct vc_data *vc, struct console_font_op *op)
{
    switch (op->op) {
/*
	case KD_FONT_OP_SET:
	    return fbcon_set_font(vc, op);
	case KD_FONT_OP_GET:
	    return fbcon_get_font(vc, op);
	case KD_FONT_OP_SET_DEFAULT:
	    return fbcon_set_def_font(vc, op);
	case KD_FONT_OP_COPY:
	    return fbcon_copy_font(vc, op);
*/
	default:
	    return -ENOSYS;
    }
}

static int fbcon_resize(struct vc_data *vc,unsigned int cols,unsigned int rows)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;	
    struct fb_var_screeninfo var;
    int err;   

    /* We grab what works and then modify the resolution only. */
    memcpy(&var, &info->var, sizeof(struct fb_var_screeninfo));		 
    var.xres = var.xres_virtual = cols * vc->vc_font.width;
    var.yres = rows * vc->vc_font.height;
    var.activate = FB_ACTIVATE_NOW;		

    DPRINTK("attempting to set mode to %d rows x cols%d\n", rows, cols);
    
    err = fb_set_var(&var, info);
    if (err)
	return err;
    DPRINTK("Graphics mode is now set at %dx%d depth %d\n", var.xres, var.yres, 		var.bits_per_pixel);	
    vc->vc_can_do_color = var.bits_per_pixel != 1;
    vc->vc_scrollback = 1;
    return 0;
}

static int fbcon_set_palette(struct vc_data *vc, unsigned char *table)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;	
    int size, i, j, k;
    u8 val;

    if (info->var.bits_per_pixel <= 4)
    	size = 1<<info->var.bits_per_pixel;
    else
        size = 16;
     
    if (fb_alloc_cmap(&info->cmap, size, 0))
	return -1;  

    for (i = j = 0; i < info->cmap.len; i++) {
    	k = table[i];
	val = vc->vc_palette[j++];
	info->cmap.red[k] = (val<<8)|val;
	val = vc->vc_palette[j++];
	info->cmap.green[k] = (val<<8)|val;
	val = vc->vc_palette[j++];
	info->cmap.blue[k] = (val<<8)|val;
    }
    info->cmap.transp = NULL;
    return fb_set_cmap(&info->cmap, 1, info);
}

static int fbcon_scroll(struct vc_data *vc, int lines)
{
   return 0;
}

static int fbcon_set_origin(struct vc_data *vc)
{
   return 0;
}

/* As we might be inside of softback, we may work with non-contiguous buffer,
   that's why we have to use a separate routine. */
static void fbcon_invert_region(struct vc_data *vc, u16 *p, int cnt)
{
}

/*
 *  The console `switch' structure for the frame buffer based console
 */
 
const struct consw fb_con = {
    con_startup: 	fbcon_startup, 
    con_init: 		fbcon_init,
    con_deinit: 	fbcon_deinit,
    con_clear: 		fbcon_clear,
    con_putc: 		fbcon_putc,
    con_putcs: 		fbcon_putcs,
    con_cursor: 	fbcon_cursor,
    con_scroll_region: 	fbcon_scroll_region,
    con_bmove: 		fbcon_bmove,
    con_blank: 		fbcon_blank,
    con_font_op:	fbcon_font_op,
    con_resize:		fbcon_resize,
    con_set_palette: 	fbcon_set_palette,
    con_scroll: 	fbcon_scroll,
 //   con_set_origin: 	fbcon_set_origin,
 //   con_invert_region:	fbcon_invert_region,
};

int __init fb_console_init(void)
{
   const char *display_desc = NULL;
   struct vt_struct *vt;
  
   //take_over_console(admin_vt, &fb_con);  
   
   /* Allocate the memory we need for this VT */  
   vt = (struct vt_struct *) kmalloc(sizeof(struct vt_struct),GFP_KERNEL);
   if (!vt) return -ENOMEM;
   memset(vt, 0, sizeof(struct vt_struct));

   vt->kmalloced = 1;
   vt->vt_sw = &fb_con;
   display_desc = create_vt(vt, 0);
 
   if (!display_desc) return -ENODEV; 
   printk("Console: %s %s %dx%d\n", vt->default_mode->vc_can_do_color ? "colour" : "mono",display_desc, vt->default_mode->vc_cols, vt->default_mode->vc_rows);
   return 0;
}
