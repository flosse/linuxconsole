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
#define fontwidthvalid(p,w) ((p)->dispsw->fontwidthmask & FONTWIDTH(w))

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

static inline void cursor_undrawn(struct fbvt_data *par)
{
    par->vbl_cursor_cnt = 0;
    par->cursor_drawn = 0;
}

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
    
    return display_desc;
}

static void fbcon_init(struct vc_data *vc)
{
}

static void fbcon_deinit(struct vc_data *vc)
{
}

static void fbcon_clear(struct vc_data *vc, int sy, int sx, int height,
			int width)
{
}

static void fbcon_putc(struct vc_data *vc, int c, int ypos, int xpos)
{
}

static void fbcon_putcs(struct vc_data *vc, const unsigned short *s, 
			int count, int ypos, int xpos)
{
}

static void fbcon_cursor(struct vc_data *vc, int mode)
{
}

static int fbcon_scroll_region(struct vc_data *vc, int t, int b, int dir, 
				int count)
{
    return 0;
}

static void fbcon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
			int height, int width)
{
}

static int fbcon_blank(struct vc_data *vc, int blank)
{
 //   (*info->fbops->fb_blank)(blank, info);
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
    struct fbvt_data *par = vc->display_fg->data_hook; 	
    struct display *p = par->fb_display[vc->vc_num];
    struct fb_info *info = par->fb_info;	
    struct fb_cmap palette_cmap;	
    int size, i, j, k;
    u8 val;

    if ((!p->can_soft_blank && vc->display_fg->vt_blanked) ||
	!vc->vc_can_do_color)
	return -EINVAL;
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

/*
 *  Visible symbols for modules
 */
EXPORT_SYMBOL(fb_con);
