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
#include <linux/malloc.h>
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
static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
		       int width);
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
static int fbcon_resize(struct vc_data *vc,unsigned int rows,unsigned int cols);
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
    struct vc_data *vc = vt->default_mode;
    struct fbcon_font_desc *font = NULL;
    struct fb_info *info;
    struct module *owner;	
    int logo, index;	

    /*
     *  If num_registered_fb is zero, this is a call for the dummy part.
     *  The frame buffer devices weren't initialized yet.
     */
    if (!num_registered_fb)
        return display_desc;

    index = num_registered_fb--;
//    info = kmalloc(sizeof(struct fb_info), GFP_KERNEL);
    info = registered_fb[index]; 			   
    owner = info->fbops->owner;	

    if (owner)
        __MOD_INC_USE_COUNT(owner);
    if (info->fbops->fb_open && info->fbops->fb_open(info, 0) && owner)
         __MOD_DEC_USE_COUNT(owner);

    info->fbops->fb_set_par(info);
    if (fb_set_cmap(&info->cmap, 1, info))
    	return NULL;

    DPRINTK("mode:   %s\n", info->fix.id);
    DPRINTK("visual: %d\n", info->fix.visual);
    DPRINTK("res:    %dx%d-%d\n",info->var.xres, info->var.yres, info->var.bits_per_pixel); 

    if (info->flags & FBINFO_FLAG_MODULE)
        logo = 0;

    info->var.xoffset = info->var.yoffset = 0;  /* reset wrap/pan */

    if (!fontname[0] || !(font = fbcon_find_font(fontname))) 
	font = fbcon_get_default_font(info->var.xres, info->var.yres);

    if (!font) {
	printk(KERN_ERR "fbcon_startup: No default font detect.\n");	
	return NULL;
    }

#ifdef CONFIG_FBCON_FONTWIDTH8_ONLY
    if (font->width%8) {
        /* ++Geert: changed from panic() to `correct and continue' */
        printk(KERN_ERR "fbcon_startup: No support for fontwidth %d\n", vc->vc_font.width);
    }
#endif
	
    vc->vc_font.width = font->width;
    vc->vc_font.height = font->height;
    vc->vc_font.data = font->data;
	
    vc->vc_cols = info->var.xres/vc->vc_font.width;
    vc->vc_rows = info->var.yres/vc->vc_font.height;
    	
    vc->vc_scrollback = info->var.yres_virtual/info->var.yres;

    if (logo) {
	/* Need to make room for the logo */
    }	

    vt->data_hook = info;  

    return display_desc;
}

static void fbcon_init(struct vc_data *vc)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;

    vc->vc_can_do_color = info->var.bits_per_pixel != 1;
    vc->vc_complement_mask = vc->vc_can_do_color ? 0x7700 : 0x0800;
    if (vc->vc_font.charcount == 256) 
        vc->vc_hi_font_mask = 0;
    else {
	vc->vc_hi_font_mask = 0x100;
	if (vc->vc_can_do_color)
		vc->vc_complement_mask <<= 1; 
    }	
}

static void fbcon_deinit(struct vc_data *vc)
{
/*
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;

    fbcon_free_font(info);
*/
}

static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
			int width)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    unsigned long color = attr_bgcol_ec(vc);	

    info->fbops->fb_fillrect(info, sx, sy, width, height, color, ROP_COPY);	
}

static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    unsigned short charmask = (vc->vc_font.charcount > 256) ? 0xff: 0x1ff;
    unsigned int dy = ypos * vc->vc_font.height;	 	
    unsigned int dx = xpos * vc->vc_font.width;
    void *image; 	
   
    if (vc->vc_font.width <= 8)
        image = vc->vc_font.data + (c & charmask) * vc->vc_font.height;
    else
        image = vc->vc_font.data + ((c & charmask)*vc->vc_font.height << 1);

    info->fbops->fb_imageblit(info, vc->vc_font.width, vc->vc_font.height,
                              image, 1, dx, dy);
}

static void fbcon_putcs(struct vc_data *vc, const unsigned short *s, 
			int count, int ypos, int xpos)
{
   unsigned int charmask = (vc->vc_font.charcount > 256) ? 0xff: 0x1ff;
   int c; 	

   while (count--) {
        c = scr_readw(s++) & charmask;
	fbcon_putc(vc, c, ypos, xpos); 	
   }	
}

static void fbcon_cursor(struct vc_data *vc, int mode)
{
}

static int fbcon_scroll_region(struct vc_data *vc, int t, int b, int dir, 
				int count)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;
    int sy = 0, dy = 0, height = 0;	

    info->fbops->fb_copyarea(info, 0, sy, info->var.xres, height, 0, dy);
    return 0;
}

static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width)
{
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

static int fbcon_resize(struct vc_data *vc,unsigned int rows,unsigned int cols)
{
    return 0;
}

static int fbcon_set_palette(struct vc_data *vc, unsigned char *table)
{
    struct fb_info *info = (struct fb_info *) vc->display_fg->data_hook;	
    struct fb_cmap palette_cmap;	
    int size, i, j, k;
    u8 val;

    if (info->var.bits_per_pixel <= 4)
    	palette_cmap.len = 1<<info->var.bits_per_pixel;
    else
        palette_cmap.len = 16;
    size = palette_cmap.len * sizeof(u16);
    palette_cmap.start = 0;
    if (!(palette_cmap.red = kmalloc(size, GFP_ATOMIC)))
    	return -1;
    if (!(palette_cmap.green = kmalloc(size, GFP_ATOMIC)))
        return -1;
    if (!(palette_cmap.blue = kmalloc(size, GFP_ATOMIC)))
        return -1;
    palette_cmap.transp = NULL;
	
    for (i = j = 0; i < palette_cmap.len; i++) {
    	k = table[i];
	val = vc->vc_palette[j++];
	palette_cmap.red[k] = (val<<8)|val;
	val = vc->vc_palette[j++];
	palette_cmap.green[k] = (val<<8)|val;
	val = vc->vc_palette[j++];
	palette_cmap.blue[k] = (val<<8)|val;
    }
    return fb_set_cmap(&palette_cmap, 1, info);
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
    con_set_origin: 	fbcon_set_origin,
    con_invert_region:	fbcon_invert_region,
};

void __init fb_console_init(void)
{
   const char *display_desc = NULL;
   struct vt_struct *vt;
   struct vc_data *vc;	
   long q;

#ifdef CONFIG_DUMMY_CONSOLE
   take_over_console(admin_vt, &fb_con); 
#else 
   /* Allocate the memory we need for this VT */
   vt = (struct vt_struct *) kmalloc(sizeof(struct vt_struct),GFP_KERNEL);
   if (!vt) return;
   memset(vt, 0, sizeof(struct vt_struct));

   vt->default_mode = (struct vc_data *) kmalloc(sizeof(struct vc_data), GFP_KERNEL);
   if (!vt->default_mode) {
        kfree(vt);
        return;
   }

   vc = (struct vc_data *) kmalloc(sizeof(struct vc_data), GFP_KERNEL);
   if (!vc) {
       kfree(vt->default_mode);
       kfree(vt);
       return;
   }
   vt->kmalloced = 1;
   vt->vc_cons[0] = vc;
   
   vt->vt_sw = &fb_con;
   display_desc = create_vt(vt, 0);
 
   q = (long) kmalloc(vc->vc_screenbuf_size, GFP_KERNEL);
   if (!display_desc || !q) {
       kfree(vt->vc_cons[0]);
       kfree(vt->default_mode);
       kfree(vt);
       if (q)
           kfree((char *) q);
           return;
       }
   vc->vc_screenbuf = (unsigned short *) q;
   vc_init(vc, 1);
   tasklet_enable(&vt->vt_tasklet);
   tasklet_schedule(&vt->vt_tasklet);

   printk("Console: %s %s %dx%d", vc->vc_can_do_color ? "colour" : "mono",
           display_desc, vc->vc_cols, vc->vc_rows);
#endif
}

/*
 *  Visible symbols for modules
 */
EXPORT_SYMBOL(fb_con);
