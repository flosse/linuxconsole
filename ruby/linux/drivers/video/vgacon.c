/*
 *  linux/drivers/video/vgacon.c -- Low level VGA based console driver
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *
 *  This file is based on the old console.c, vga.c and vesa_blank.c drivers.
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *	User definable mapping table and font loading by Eugene G. Crosser,
 *	<crosser@average.org>
 *
 *	Improved loadable font/UTF-8 support by H. Peter Anvin
 *	Feb-Sep 1995 <peter.anvin@linux.org>
 *
 *	Colour palette handling, by Simon Tatham
 *	17-Jun-95 <sgt20@cam.ac.uk>
 *
 *	if 512 char mode is already enabled don't re-enable it,
 *	because it causes screen to flicker, by Mitja Horvat
 *	5-May-96 <mitja.horvat@guest.arnes.si>
 *
 *	Use 2 outw instead of 4 outb_p to reduce erroneous text
 *	flashing on RHS of screen during heavy console scrolling .
 *	Oct 1996, Paul Gortmaker.
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/vt_buffer.h>
#include <linux/vt_kern.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/spinlock.h>

#include "vga.h"

#define BLANK 0x0020

#define CAN_LOAD_EGA_FONTS	/* undefine if the user must not do this */
#define CAN_LOAD_PALETTE	/* undefine if the user must not do this */
#define VGA_ADDR(vc, x, y) ((u16 *) vga_vram_base + (y)*vc->vc_cols + (x))

/* You really do _NOT_ want to define this, unless you have buggy
 * Trident VGA which will resize cursor when moving it between column
 * 15 & 16. If you define this and your VGA is OK, inverse bug will
 * appear.
 */
#undef TRIDENT_GLITCH

/*
 *  Interface used by the world
 */

static const char *vgacon_startup(struct vt_struct *vt, int init);
static void vgacon_init(struct vc_data *vc);
static void vgacon_deinit(struct vc_data *vc);
static void vgacon_clear(struct vc_data *vc,int x,int y,int height,int width);
static void vgacon_putc(struct vc_data *vc, int c, int y, int x);
static void vgacon_putcs(struct vc_data *vc, const unsigned short *s, int count,
                         int y, int x);
static void vgacon_cursor(struct vc_data *vc, int mode);
static int vgacon_scroll(struct vc_data *vc, int t, int b, int dir, int lines);
static void vgacon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
                         int height, int width);
static int vgacon_switch(struct vc_data *vc);
static int vgacon_blank(struct vc_data *vc, int blank);
static int vgacon_font_op(struct vc_data *vc, struct console_font_op *op);
static int vgacon_set_palette(struct vc_data *vc, unsigned char *table);
static int vgacon_resize(struct vc_data *vc, unsigned int rows, unsigned int cols);
static int vgacon_scrolldelta(struct vc_data *vc, int lines);
static int vgacon_set_origin(struct vc_data *vc);
static void vgacon_save_screen(struct vc_data *vc);
static u8 vgacon_build_attr(struct vc_data *vc, u8 color, u8 intensity, u8 blink, u8 underline, u8 reverse);
static void vgacon_invert_region(struct vc_data *vc, u16 *p, int count);

static unsigned long vgacon_uni_pagedir[2];

/* Description of the hardware situation */
static unsigned long   vga_vram_base;		/* Base of video memory */
static unsigned long   vga_vram_end;		/* End of video memory */
static unsigned long   vga_origin;
static u16             vga_video_port_reg;	/* Video register select port */
static u16             vga_video_port_val;	/* Video register value port */
static unsigned char   vga_hardscroll_enabled;
#ifdef CONFIG_IA64_SOFTSDV_HACKS
/*
 * SoftSDV doesn't have hardware assist VGA scrolling 
 */
static unsigned char   vga_hardscroll_user_enable;
#else
static unsigned char   vga_hardscroll_user_enable = 1;
#endif
static int	       vga_is_gfx;
static int	       vga_512_chars;
static unsigned int    vga_rolled_over;
static char 	       vga_fonts[8192*4];
static struct vga_hw_state vgacon_state = {
	80, 80, 2, 12, 2, 400, 8, 2, 39, 0, 0, 0, 0, 0, 0xFF, 0,
       	0, 0xE3, MODE_TEXT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

extern spinlock_t vga_lock;

static int __init no_scroll(char *str)
{
	/*
	 * Disabling scrollback is required for the Braillex ib80-piezo
	 * Braille reader made by F.H. Papenmeier (Germany).
	 * Use the "no-scroll" bootflag.
	 */
	vga_hardscroll_user_enable = vga_hardscroll_enabled = 0;
	return 1;
}

__setup("no-scroll", no_scroll);

/*
 * PIO_FONT support.
 *
 * The font loading code goes back to the codepage package by
 * Joel Hoffman (joel@wam.umd.edu). (He reports that the original
 * reference is: "From: p. 307 of _Programmer's Guide to PC & PS/2
 * Video Systems_ by Richard Wilton. 1987.  Microsoft Press".)
 *
 * Change for certain monochrome monitors by Yury Shevchuck
 * (sizif@botik.yaroslavl.su).
 */

#ifdef CAN_LOAD_EGA_FONTS

#define colourmap 0xa0000
/* Pauline Middelink <middelin@polyware.iaf.nl> reports that we
   should use 0xA0000 for the bwmap as well.. */
#define blackwmap 0xa0000
#define cmapsz 8192

int vga_do_font_op(struct vc_data *vc, char *arg, int set, int ch512)
{
        int beg, i, font_select = 0x00;
        char *charmap;

        if (vgacon_state.video_type != VIDEO_TYPE_EGAM) {
                charmap = (char *)VGA_MAP_MEM(colourmap);
                beg = 0x0e;
#ifdef VGA_CAN_DO_64KB
                if (vgacon_state.video_type == VIDEO_TYPE_VGAC)
                        beg = 0x06;
#endif
        } else {
                charmap = (char *)VGA_MAP_MEM(blackwmap);
                beg = 0x0a;
        }

        /*
         * The default font is kept in slot 0 and is never touched.
         * A custom font is loaded in slot 2 (256 ch) or 2:3 (512 ch)
         */

        if (set) {
                if (!arg)
                        ch512 = 0;              /* Default font is always 256 */                font_select = arg ? (ch512 ? 0x0e : 0x0a) : 0x00;
        }

        if (set && arg)
                charmap += 4*cmapsz;

        spin_lock_irq(&vga_lock);
        /* First, the Sequencer */
        vga_wseq(NULL, VGA_SEQ_RESET, 0x1);
        /* CPU writes only to map 2 */
        vga_wseq(NULL, VGA_SEQ_PLANE_WRITE, 0x04);
        /* Sequential addressing */
        vga_wseq(NULL, VGA_SEQ_MEMORY_MODE, 0x07);
        /* Clear synchronous reset */
        vga_wseq(NULL, VGA_SEQ_RESET, 0x03);
        /* Now, the graphics controller */
        /* select map 2 */
        vga_wgfx(NULL, VGA_GFX_PLANE_READ, 0x02);
        /* disable odd-even addressing */
        vga_wgfx(NULL, VGA_GFX_MODE, 0x00);
        /* map start at A000:0000 */
        vga_wgfx(NULL, VGA_GFX_MISC, 0x00);
        spin_unlock_irq(&vga_lock);

        if (arg) {
                if (set)
                        for (i=0; i<cmapsz ; i++)
                                vga_writeb(arg[i], charmap + i);
                else
                        for (i=0; i<cmapsz ; i++)
                                arg[i] = vga_readb(charmap + i);

                /*
                 * In 512-character mode, the character map is not contiguous if                 * we want to remain EGA compatible -- which we do
                 */

                if (ch512) {
                        charmap += 2*cmapsz;
                        arg += cmapsz;
                        if (set)
                                for (i=0; i<cmapsz ; i++)
                                        vga_writeb(arg[i], charmap+i);
                        else
                                for (i=0; i<cmapsz ; i++)
                                        arg[i] = vga_readb(charmap+i);
                }
        }

        spin_lock_irq(&vga_lock);
        /* First, the squencer. Synchronous reset */
        vga_wseq(NULL, VGA_SEQ_RESET, 0x01);
        /* CPU writes to maps 0 and 1 */
        vga_wseq(NULL, VGA_SEQ_PLANE_WRITE, 0x03);
        /* odd-even addressing */
        vga_wseq(NULL, VGA_SEQ_MEMORY_MODE, 0x03);

	/* Character Map Select */
        if (set) 
                vga_wseq(NULL, VGA_SEQ_CHARACTER_MAP, font_select);
        /* clear synchronous reset */
        vga_wseq(NULL, VGA_SEQ_RESET, 0x03);

        /* Now, the graphics controller */
        /* select map 0 for CPU */
        vga_wgfx(NULL, VGA_GFX_PLANE_READ, 0x00);
        /* enable even-odd addressing */
        vga_wgfx(NULL, VGA_GFX_MODE, 0x10);
        /* map starts at b800:0 or b000:0 */
        vga_wgfx(NULL, VGA_GFX_MISC, beg);

        /* if 512 char mode is already enabled don't re-enable it. */
        if ((set)&&(ch512!=vga_512_chars)) {    /* attribute controller */
                /* 256-char: enable intensity bit
		   512-char: disable intensity bit */
		vga_512_chars=ch512;
                /* clear address flip-flop */
                vga_r(NULL, vc->vc_can_do_color ? VGA_IS1_RC : VGA_IS1_RM);
                /* color plane enable register */
                vga_wattr(NULL, VGA_ATC_PLANE_ENABLE, ch512 ? 0x07 : 0x0f);
                /* Wilton (1987) mentions the following; I don't know what
                   it means, but it works, and it appears necessary */
                vga_r(NULL, vc->vc_can_do_color ? VGA_IS1_RC : VGA_IS1_RM);
                vga_wattr(NULL, VGA_AR_ENABLE_DISPLAY, 0);
                vga_w(NULL, VGA_ATT_W, VGA_AR_ENABLE_DISPLAY);
        }
        spin_unlock_irq(&vga_lock);
        return 0;
}
	
/*
 * Adjust the screen to fit a font of a certain height
 */
static int
vgacon_adjust_height(struct vc_data *vc, unsigned fontheight)
{
	unsigned char ovr, vde, fsr;
	int rows, maxscan;

	if (fontheight == vc->vc_font.height)
		return 0;

	vc->vc_font.height = fontheight;

	rows = vc->vc_scan_lines/fontheight;	/* Number of video rows we end up with */
	maxscan = rows*fontheight - 1;		/* Scan lines to actually display-1 */

	/* Reprogram the CRTC for the new font size
	   Note: the attempt to read the overflow register will fail
	   on an EGA, but using 0xff for the previous value appears to
	   be OK for EGA text modes in the range 257-512 scan lines, so I
	   guess we don't need to worry about it.

	   The same applies for the spill bits in the font size and cursor
	   registers; they are write-only on EGA, but it appears that they
	   are all don't care bits on EGA, so I guess it doesn't matter. */

	spin_lock_irq(&vga_lock);
        ovr = vga_rcrt(NULL, VGA_CRTC_OVERFLOW);   /* CRTC overflow register */
        fsr = vga_rcrt(NULL, VGA_CRTC_MAX_SCAN);   /* Font size register */
        spin_unlock_irq(&vga_lock);
	
	vde = maxscan & 0xff;			/* Vertical display end reg */
	ovr = (ovr & 0xbd) +			/* Overflow register */
	      ((maxscan & 0x100) >> 7) +
	      ((maxscan & 0x200) >> 3);
	fsr = (fsr & 0xe0) + (fontheight-1);    /*  Font size register */

	spin_lock_irq(&vga_lock);
        vga_wcrt(NULL, 0x07, ovr);               /* CRTC overflow register */
        vga_wcrt(NULL, 0x09, fsr);               /* Font size */
        vga_wcrt(NULL, 0x12, vde);               /* Vertical display limit */
        spin_unlock_irq(&vga_lock);
	return 0;
}

static int vgacon_font_op(struct vc_data *vc, struct console_font_op *op)
{
	int rc;

	if (vgacon_state.video_type < VIDEO_TYPE_EGAM)
		return -EINVAL;

	if (op->op == KD_FONT_OP_SET) {
		if (op->width != 8 || (op->charcount != 256 && op->charcount != 512))
			return -EINVAL;
		rc = vga_do_font_op(vc, op->data, 1, op->charcount == 512);
		if (!rc && !(op->flags & KD_FONT_FLAG_DONT_RECALC))
			rc = vgacon_adjust_height(vc, op->height);
	} else if (op->op == KD_FONT_OP_GET) {
		op->width = 8;
		op->height = vc->vc_font.height;
		op->charcount = vga_512_chars ? 512 : 256;
		if (!op->data) return 0;
		rc = vga_do_font_op(vc, op->data, 0, vga_512_chars);
	} else
		rc = -ENOSYS;
	return rc;
}

#else

static int vgacon_font_op(struct vc_data *vc, struct console_font_op *op)
{
	return -ENOSYS;
}

#endif

static const char __init *vgacon_startup(struct vt_struct *vt, int init)
{
	struct vc_data *vc = vt->default_mode;
	const char *display_desc = NULL;
	u16 saved1, saved2;
	volatile u16 *p;

	if (ORIG_VIDEO_ISVGA == VIDEO_TYPE_VLFB) {
	no_vga:
#ifdef CONFIG_DUMMY_CONSOLE
		vt->vt_sw = &dummy_con;
		return vt->vt_sw->con_startup(vt, init);
#else
		return NULL;
#endif
	}

	/* VGA16 modes are not handled by VGACON */
        if ((ORIG_VIDEO_MODE == 0x0D) || /* 320x200/4 */
           (ORIG_VIDEO_MODE == 0x0E) || /* 640x200/4 */
           (ORIG_VIDEO_MODE == 0x10) || /* 640x350/4 */
           (ORIG_VIDEO_MODE == 0x12) || /* 640x480/4 */
           (ORIG_VIDEO_MODE == 0x6A))   /* 800x600/4, 0x6A is very common */
               goto no_vga;

	vc->vc_rows = ORIG_VIDEO_LINES;
	vc->vc_cols = ORIG_VIDEO_COLS;

	if (ORIG_VIDEO_MODE == 7) {	/* Is this a monochrome display? */
		vga_vram_base = 0xb0000;
		vga_video_port_reg = 0x3b4;
		vga_video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
			static struct resource ega_console_resource = { "ega", 0x3B0, 0x3BC };
			vgacon_state.video_type = VIDEO_TYPE_EGAM;
			vga_vram_end = 0xb8000;
			display_desc = "EGA+";
			request_resource(&ioport_resource, &ega_console_resource);
		} else {
			static struct resource mda1_console_resource = { "mda", 0x3B0, 0x3BB };
			static struct resource mda2_console_resource = { "mda", 0x3BF, 0x3BF };
			vgacon_state.video_type = VIDEO_TYPE_MDA;
			vga_vram_end = 0xb2000;
			display_desc = "*MDA";
			request_resource(&ioport_resource, &mda1_console_resource);
			request_resource(&ioport_resource, &mda2_console_resource);
			vc->vc_font.height = 14;
		}
	} else {
		/* If not, it is color. */
		vc->vc_can_do_color = 1;
		vga_vram_base = 0xb8000;
		vga_video_port_reg = 0x3d4;
		vga_video_port_val = 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10) {
			int i; 

			vga_vram_end = 0xc0000;

			if (!ORIG_VIDEO_ISVGA) {
				static struct resource ega_console_resource = { "ega", 0x3C0, 0x3DF };
				vgacon_state.video_type = VIDEO_TYPE_EGAC;
				display_desc = "EGA";
				request_resource(&ioport_resource, &ega_console_resource);
			} else {
				static struct resource vga_console_resource = { "vga+", 0x3C0, 0x3DF };
				vgacon_state.video_type = VIDEO_TYPE_VGAC;
				display_desc = "VGA+";
				request_resource(&ioport_resource, &vga_console_resource);

#ifdef VGA_CAN_DO_64KB
				/*
				 * get 64K rather than 32K of video RAM.
				 * This doesn't actually work on all "VGA"
				 * controllers (it seems like setting MM=01
				 * and COE=1 isn't necessarily a good idea)
				 */
				vga_vram_base = 0xa0000;
				vga_vram_end = 0xb0000;
				outb_p (6, 0x3ce) ;
				outb_p (6, 0x3cf) ;
#endif

				/*
				 * Normalise the palette registers, to point
				 * the 16 screen colours to the first 16
				 * DAC entries.
				 */

				for (i=0; i<16; i++) {
					inb_p (0x3da) ;
					outb_p (i, 0x3c0) ;
					outb_p (i, 0x3c0) ;
				}
				outb_p (0x20, 0x3c0) ;

				/* now set the DAC registers back to their
				 * default values */

				for (i=0; i<16; i++) {
					outb_p (color_table[i], 0x3c8) ;
					outb_p (default_red[i], 0x3c9) ;
					outb_p (default_grn[i], 0x3c9) ;
					outb_p (default_blu[i], 0x3c9) ;
				}
			}
		} else {
			static struct resource cga_console_resource = { "cga", 0x3D4, 0x3D5 };
			vgacon_state.video_type = VIDEO_TYPE_CGA;
			vga_vram_end = 0xba000;
			display_desc = "*CGA";
			request_resource(&ioport_resource, &cga_console_resource);
			vc->vc_font.height = 8;
		}
	}

	vga_origin = vga_vram_base = VGA_MAP_MEM(vga_vram_base);
	vga_vram_end = VGA_MAP_MEM(vga_vram_end);

	/*
	 *	Find out if there is a graphics card present.
	 *	Are there smarter methods around?
	 */
	p = (volatile u16 *)vga_vram_base;
	saved1 = scr_readw(p);
	saved2 = scr_readw(p + 1);
	scr_writew(0xAA55, p);
	scr_writew(0x55AA, p + 1);
	if (scr_readw(p) != 0xAA55 || scr_readw(p + 1) != 0x55AA) {
		scr_writew(saved1, p);
		scr_writew(saved2, p + 1);
		goto no_vga;
	}
	scr_writew(0x55AA, p);
	scr_writew(0xAA55, p + 1);
	if (scr_readw(p) != 0x55AA || scr_readw(p + 1) != 0xAA55) {
		scr_writew(saved1, p);
		scr_writew(saved2, p + 1);
		goto no_vga;
	}
	scr_writew(saved1, p);
	scr_writew(saved2, p + 1);

	if (vgacon_state.video_type == VIDEO_TYPE_EGAC
	    || vgacon_state.video_type == VIDEO_TYPE_VGAC
	    || vgacon_state.video_type == VIDEO_TYPE_EGAM) {
		vga_hardscroll_enabled = vga_hardscroll_user_enable;
		vc->vc_font.height = ORIG_VIDEO_POINTS;
	}
	vgacon_state.mode = MODE_TEXT;

	if (init) {
		if (vga_512_chars)
			vga_do_font_op(vc, vga_fonts, 0, 0);
		else 
			vga_do_font_op(vc, vga_fonts, 0, 1);
	} else {
        	int i;

               	vgacon_state.misc = 0xE3;
               	vgacon_state.misc &= ~0x40;
                vgacon_state.misc &= ~0x80;

               	vga_clock_chip(&vgacon_state, 0, 1, 1);
               	vga_set_mode(&vgacon_state, 0);
               	if (vga_512_chars)
                	vga_do_font_op(vc, vga_fonts, 1, 1);
                else
                        vga_do_font_op(vc, vga_fonts, 1, 0);
               	/* now set the DAC registers back to their
                   default values */
              	for (i=0; i<16; i++) {
                	outb_p (color_table[i], 0x3c8) ;
                       	outb_p (default_red[i], 0x3c9) ;
                        outb_p (default_grn[i], 0x3c9) ;
                       	outb_p (default_blu[i], 0x3c9) ;
               	}
       	}
 	vc->vc_font.data = vga_fonts;	
	vc->vc_x = ORIG_X;
	vc->vc_y = ORIG_Y;
	/* This maybe be suboptimal but is a safe bet - go with it */
	vc->vc_scan_lines = vc->vc_font.height * vc->vc_rows; 
	return display_desc;
}

static void vgacon_init(struct vc_data *vc)
{
	unsigned long p;

	vc->vc_complement_mask = 0x7700;
	p = *vc->vc_uni_pagedir_loc;
	if (vc->vc_uni_pagedir_loc == &vc->vc_uni_pagedir ||
	    !--vc->vc_uni_pagedir_loc[1])
		con_free_unimap(vc);
	vc->vc_uni_pagedir_loc = vgacon_uni_pagedir;
	vgacon_uni_pagedir[1]++;
	if (!vgacon_uni_pagedir[0] && p)
		con_set_default_unimap(vc);
}

static inline void vga_set_mem_top(struct vc_data *vc)
{
	int val = (vga_origin - vga_vram_base)/2;
	unsigned long flags;
	unsigned int v1, v2;
	
	spin_lock_irqsave(&vga_lock, flags);
	v2 = val >> 8;
	vga_wcrt(NULL, VGA_CRTC_START_HI, v2);	
	v1 = val & 0xff;
	vga_wcrt(NULL, VGA_CRTC_START_LO, v1);
	spin_unlock_irqrestore(&vga_lock, flags);
}

static void vgacon_deinit(struct vc_data *c)
{
	/* When closing the last console, reset video origin */
	if (!--vgacon_uni_pagedir[1]) {
		vga_origin = vga_vram_base;
		vga_set_mem_top(c);
		con_free_unimap(c);
	}
	c->vc_uni_pagedir_loc = &c->vc_uni_pagedir;
	con_set_default_unimap(c);
}

static void vgacon_clear(struct vc_data *vc, int x, int y, int height,
                         int width)
{
	u16 *dest = VGA_ADDR(vc, x, y);
        u16 eattr = vc->vc_video_erase_char;

        if (width <= 0 || height <= 0)
        	return;

        if (x == 0 && width == vc->vc_cols) {
                scr_memsetw(dest, eattr, height*width);
        } else {
                for (; height > 0; height--, dest += vc->vc_cols)
                        scr_memsetw(dest, eattr, width);
        }
}

static void vgacon_putc(struct vc_data *vc, int c, int y, int x)
{
        scr_writew(c, VGA_ADDR(vc, x, y));
}

static void vgacon_putcs(struct vc_data *vc, const unsigned short *s, int count,
                         int y, int x)
{
        u16 *dest = VGA_ADDR(vc, x, y);

        for (; count > 0; count--)
                scr_writew(scr_readw(s++), dest++);
}

static void vgacon_set_cursor_size(int xpos, int from, int to)
{
	unsigned long flags;
	int curs, cure;
	static int lastfrom, lastto;

#ifdef TRIDENT_GLITCH
	if (xpos<16) from--, to--;
#endif

	if ((from == lastfrom) && (to == lastto)) return;
	lastfrom = from; lastto = to;

	spin_lock_irqsave(&vga_lock, flags);
        curs = vga_rcrt(NULL, VGA_CRTC_CURSOR_START);      /* Cursor start */
        cure = vga_rcrt(NULL, VGA_CRTC_CURSOR_END);        /* Cursor end */

	curs = (curs & 0xc0) | from;
	cure = (cure & 0xe0) | to;

	vga_wcrt(NULL, VGA_CRTC_CURSOR_START, curs);       /* Cursor start */
        vga_wcrt(NULL, VGA_CRTC_CURSOR_END, cure);         /* Cursor end */
        spin_unlock_irqrestore(&vga_lock, flags);
}

static void vgacon_cursor(struct vc_data *vc, int mode)
{
    unsigned long flags;
    unsigned int v1, v2;	
    int val;

    if (vc->vc_origin != vga_origin)
	vgacon_scrolldelta(vc, 0);
    switch (mode) {
	case CM_ERASE:
	    val = (vga_vram_end - vga_vram_base-1)/2;
	    spin_lock_irqsave(&vga_lock, flags);
	    v2 = val >> 8;
	    vga_wcrt(NULL, VGA_CRTC_CURSOR_HI, v2);	
	    v1 = val & 0xff;
	    vga_wcrt(NULL, VGA_CRTC_CURSOR_LO, v1);
	    spin_unlock_irqrestore(&vga_lock, flags);
	    break;

	case CM_MOVE:
	case CM_DRAW:
	    val = (vc->vc_pos - vga_vram_base)/2;
	    spin_lock_irqsave(&vga_lock, flags);
	    v2 = val >> 8;
	    vga_wcrt(NULL, VGA_CRTC_CURSOR_HI, v2);	
	    v1 = val & 0xff;
	    vga_wcrt(NULL, VGA_CRTC_CURSOR_LO, v1);
	    spin_unlock_irqrestore(&vga_lock, flags);
	    break;
 
	    switch (vc->vc_cursor_type & 0x0f) {
		case CUR_UNDERLINE:
			vgacon_set_cursor_size(vc->vc_x, 
					vc->vc_font.height - (vc->vc_font.height < 10 ? 2 : 3),
					vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_TWO_THIRDS:
			vgacon_set_cursor_size(vc->vc_x, vc->vc_font.height/3,
					 vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_THIRD:
			vgacon_set_cursor_size(vc->vc_x, 
					 (vc->vc_font.height*2) / 3,
					 vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_HALF:
			vgacon_set_cursor_size(vc->vc_x, vc->vc_font.height/2,
					 vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_NONE:
			vgacon_set_cursor_size(vc->vc_x, 31, 30);
			break;
          	default:
			vgacon_set_cursor_size(vc->vc_x, 1,vc->vc_font.height);
			break;
		}
	    break;
    }
}

static int vgacon_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	unsigned long oldo;
	unsigned int delta;
	
	if (t || b != c->vc_rows || vga_is_gfx)
		return 0;

	if (c->vc_origin != vga_origin)
		vgacon_scrolldelta(c, 0);

	if (!vga_hardscroll_enabled || lines >= c->vc_rows/2)
		return 0;

	oldo = c->vc_origin;
	delta = lines * c->vc_size_row;
	if (dir == SM_UP) {
		if (c->vc_scr_end + delta >= vga_vram_end) {
			scr_memcpyw((u16 *)vga_vram_base,
				    (u16 *)(oldo + delta),
				    c->vc_screenbuf_size - delta);
			c->vc_origin = vga_vram_base;
			vga_rolled_over = oldo - vga_vram_base;
		} else
			c->vc_origin += delta;
		scr_memsetw((u16 *)(c->vc_origin + c->vc_screenbuf_size - delta), c->vc_video_erase_char, delta);
	} else {
		if (oldo - delta < vga_vram_base) {
			scr_memmovew((u16 *)(vga_vram_end - c->vc_screenbuf_size + delta),
				     (u16 *)oldo,
				     c->vc_screenbuf_size - delta);
			c->vc_origin = vga_vram_end - c->vc_screenbuf_size;
			vga_rolled_over = 0;
		} else
			c->vc_origin -= delta;
		scr_memsetw((u16 *)(c->vc_origin), c->vc_video_erase_char, delta);
	}
	c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
	vga_origin = c->vc_origin;
	vga_set_mem_top(c);
	c->vc_pos = (c->vc_pos - oldo) + c->vc_origin;
	return 1;
}

static void vgacon_bmove(struct vc_data *vc, int sy, int sx, int dy, int dx,
                        int height, int width)
{
         u16 *src, *dest;

         if (width <=0 || height <= 0)
                 return;

         if (sx == 0 && dx == 0 && width == vc->vc_cols) {
                 scr_memmovew(VGA_ADDR(vc, 0, dy), VGA_ADDR(vc, 0, sy),
                              height*width);
         } else if (dy < sy || (dy == sy && dx < sx)) {
                 src = VGA_ADDR(vc, sx, sy);
                 dest = VGA_ADDR(vc, dx, dy);

                 for (; height > 0; height--) {
                         scr_memmovew(dest, src, width);
                         src  += vc->vc_cols;
                         dest += vc->vc_cols;
                 }
         } else {
                 src =  VGA_ADDR(vc, sx, sy+height-1);
                 dest = VGA_ADDR(vc, dx, dy+height-1);
 
                 for (; height > 0; height--) {
                         scr_memmovew(dest, src, width);
                         src  -= vc->vc_cols;
                         dest -= vc->vc_cols;
                 }
         }
}

static int vgacon_switch(struct vc_data *vc)
{
	if (!vga_is_gfx)
		scr_memcpyw_to((u16 *) vc->vc_origin, (u16 *) vc->vc_screenbuf,
				 vc->vc_screenbuf_size);
	return 0;	/* Redrawing not needed */
}

static void vga_set_palette(struct vc_data *vc, unsigned char *table)
{
	int i, j ;

	for (i=j=0; i<16; i++) {
	 	vga_w(NULL, VGA_PEL_IW, table[i]);
                vga_w(NULL, VGA_PEL_D, vc->vc_palette[j++]>>2);
                vga_w(NULL, VGA_PEL_D, vc->vc_palette[j++]>>2);
                vga_w(NULL, VGA_PEL_D, vc->vc_palette[j++]>>2);
	}
}

static int vgacon_blank(struct vc_data *vc, int blank)
{
	switch (blank) {
	case 0:				/* Unblank */
		if (vgacon_state.vesa_blanked) {
			vga_vesa_unblank(&vgacon_state);
			vgacon_state.vesa_blanked = 0;
		}
		if (vgacon_state.palette_blanked) {
			vga_set_palette(vc, color_table);
			vgacon_state.palette_blanked = 0;
			return 0;
		}
		vga_is_gfx = 0;
		/* Tell console.c that it has to restore the screen itself */
		return 1;
	case 1:				/* Normal blanking */
		if (vgacon_state.video_type == VIDEO_TYPE_VGAC) {
			vga_pal_blank();
			vgacon_state.palette_blanked = 1;
			return 0;
		}
		vgacon_set_origin(vc);
		scr_memsetw((void *)vga_vram_base, BLANK,vc->vc_screenbuf_size);
		return 1;
	case -1:			/* Entering graphic mode */
		scr_memsetw((void *)vga_vram_base, BLANK,vc->vc_screenbuf_size);
		vga_is_gfx = 1;
		return 1;
	default:			/* VESA blanking */
		if (vgacon_state.video_type == VIDEO_TYPE_VGAC) {
			vga_vesa_blank(&vgacon_state, blank-1);
			vgacon_state.vesa_blanked = blank;
		}
		return 0;
	}
}

static int vgacon_set_palette(struct vc_data *vc, unsigned char *table)
{
#ifdef CAN_LOAD_PALETTE
	if (vgacon_state.video_type != VIDEO_TYPE_VGAC || vgacon_state.palette_blanked)
		return -EINVAL;
	vga_set_palette(vc, table);
	return 0;
#else
	return -EINVAL;
#endif
}

static int vgacon_resize(struct vc_data *vc, unsigned int rows, 
			 unsigned int cols)
{
	struct vga_hw_state state = vgacon_state;
	int err = 0;
	
	err = vga_check_mode(cols * vc->vc_font.width, state.right, state.hslen, 			     state.left, cols * vc->vc_font.width, 
			     rows * vc->vc_font.height, state.lower, 
			     state.vslen, state.upper, 0);
	if (err) return err; 		
	
	state.xres = state.vxres = cols;
	state.yres = rows * vc->vc_font.height;
	
	vga_set_mode(&state, 0);	
	vgacon_state = state;
	return err;
}

static int vgacon_scrolldelta(struct vc_data *c, int lines)
{
	if (!lines)			/* Turn scrollback off */
		vga_origin = c->vc_origin;
	else {
		int vram_size = vga_vram_end - vga_vram_base;
		int margin = c->vc_size_row * 4;
		int ul, we, p, st;

		if (vga_rolled_over > (c->vc_scr_end - vga_vram_base) + margin) {
			ul = c->vc_scr_end - vga_vram_base;
			we = vga_rolled_over + c->vc_size_row;
		} else {
			ul = 0;
			we = vram_size;
		}
		p = (vga_origin - vga_vram_base - ul + we) % we + lines * c->vc_size_row;
		st = (c->vc_origin - vga_vram_base - ul + we) % we;
		if (p < margin)
			p = 0;
		if (p > st - margin)
			p = st;
		vga_origin = vga_vram_base + (p + ul) % we;
	}
	vga_set_mem_top(c);
	return 1;
}

static int vgacon_set_origin(struct vc_data *c)
{
        /*
         * We don't play origin tricks in graphic modes,
         * Nor we write to blanked screens
         */
        if (vga_is_gfx || (c->display_fg->vt_blanked && !vgacon_state.palette_blanked))
		return 0;
	c->vc_origin = vga_origin = vga_vram_base;
	vga_set_mem_top(c);
	vga_rolled_over = 0;
	return 1;
}

static void vgacon_save_screen(struct vc_data *c)
{
	if (!vga_is_gfx)
		scr_memcpyw_from((u16 *) c->vc_screenbuf, (u16 *) c->vc_origin, c->vc_screenbuf_size);
}

static u8 vgacon_build_attr(struct vc_data *vc, u8 color, u8 intensity, 
			    u8 blink, u8 underline, u8 reverse)
{
	u8 attr = color;

	if (vc->vc_can_do_color) {
		if (underline)
			attr = (attr & 0xf0) | vc->vc_ulcolor;
		else if (intensity == 0)
			attr = (attr & 0xf0) | vc->vc_halfcolor;
	}
	if (reverse)
		attr = ((attr) & 0x88) | ((((attr) >> 4) | ((attr) << 4)) & 0x77);
	if (blink)
		attr ^= 0x80;
	if (intensity == 2)
		attr ^= 0x08;
	if (!vc->vc_can_do_color) {
		if (underline)
			attr = (attr & 0xf8) | 0x01;
		else if (intensity == 0)
			attr = (attr & 0xf0) | 0x08;
	}
	return attr;
}

static void vgacon_invert_region(struct vc_data *vc, u16 *p, int count)
{
	while (count--) {
		u16 a = scr_readw(p);
		if (vc->vc_can_do_color)
			a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
		else
			a ^= ((a & 0x0700) == 0x0100) ? 0x7000 : 0x7700;
		scr_writew(a, p++);
	}
}

/*
 *  The console `switch' structure for the VGA based console
 */

static int vgacon_dummy(struct vc_data *c)
{
	return 0;
}

#define DUMMY (void *) vgacon_dummy

const struct consw vga_con = {
	con_startup:		vgacon_startup,
	con_init:		vgacon_init,
	con_deinit:		vgacon_deinit,
	con_clear:		DUMMY,
	con_putc:		DUMMY,
	con_putcs:		DUMMY,
	con_cursor:		vgacon_cursor,
	con_scroll:		vgacon_scroll,
	con_bmove:		DUMMY,
	con_switch:		vgacon_switch,
	con_blank:		vgacon_blank,
	con_font_op:		vgacon_font_op,
	con_set_palette:	vgacon_set_palette,
	con_resize:		vgacon_resize,
	con_scrolldelta:	vgacon_scrolldelta,
	con_set_origin:		vgacon_set_origin,
	con_save_screen:	vgacon_save_screen,
	con_build_attr:		vgacon_build_attr,
	con_invert_region:	vgacon_invert_region,
};

#ifdef MODULE

void module_init(void)
{
       take_over_console(&vga_con, 0, MAX_NR_USER_CONSOLES-1, 0);
}

void module_exit(void)
{
       give_up_console(&vga_con);
}
#endif

EXPORT_SYMBOL(vga_con);
