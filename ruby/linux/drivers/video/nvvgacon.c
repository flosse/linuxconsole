/*
 *  nvvgacon.c -- Low level NVIDIA VGA based console driver
 *
 *  Written by Matan Ziv-Av <matan@svgalib.org>
 *
 *  This file is based on the VGA console driver (vgacon.c) and the MDA
 *      console driver (mdacon.c):
 *	
 *	(c) 1998 Andrew Apted <ajapted@netspace.net.au>
 *
 *      including portions (c) 1995-1998 Patrick Caulfield.
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *
 *  and on the old console.c, vga.c and vesa_blank.c drivers:
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#define VT_BUF_HAVE_MEMSETW
#define VT_BUF_HAVE_MEMCPYW
#define VT_BUF_HAVE_MEMMOVEW

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/slab.h>
#include <linux/console.h>
#include <linux/vt_buffer.h>
#include <linux/vt_kern.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

#include "vga.h"

#define BLANK 0x0020

static spinlock_t nvvgacon_lock = SPIN_LOCK_UNLOCKED;

/*
 *  Interface used by the world
 */

static const char *nvvgacon_startup(struct vt_struct *vt, int init);
static void nvvgacon_init(struct vc_data *c);
static void nvvgacon_deinit(struct vc_data *c);
static void nvvgacon_cursor(struct vc_data *c, int mode);
static int nvvgacon_switch(struct vc_data *c);
static int nvvgacon_blank(struct vc_data *c, int blank);
static int nvvgacon_font_op(struct vc_data *c, struct console_font_op *op);
static int nvvgacon_set_palette(struct vc_data *c, unsigned char *table);
static int nvvgacon_scrolldelta(struct vc_data *c, int lines);
static int nvvgacon_set_origin(struct vc_data *c);
static int nvvgacon_scroll(struct vc_data *c, int t, int b, int dir, int lines);
static u8 nvvgacon_build_attr(struct vc_data *c, u8 color, u8 intensity, u8 blink, u8 underline, u8 reverse);
static void nvvgacon_invert_region(struct vc_data *c, u16 *p, int count);
static void nvvgacon_bmove(struct vc_data *c, int sy, int sx, 
			 int dy, int dx, int height, int width);
static void nvvgacon_clear(struct vc_data *c, int y, int x, 
			  int height, int width);

//static unsigned long nvvgacon_uni_pagedir[2];

const struct consw nvvga_con;

/* Description of the hardware situation */
static char *nvvga_vram_base;		/*  */
static char *nvvga_mmio_base0;		/* misc, seq and graphics*/
static char *nvvga_mmio_base1;		/* crt and attribute */
static char *nvvga_mmio_base2;		/* dac */

static unsigned char   nvvga_font_is_default = 1;
static int	       nvvga_vesa_blanked;
static int	       nvvga_palette_blanked;
static int	       nvvga_is_gfx;
static int	       nvvga_512_chars;
static char *nvvga_video_port_reg, *seq_port_reg;
static char *nvvga_video_port_val, *seq_port_val;
static char *video_misc_wr, *video_misc_rd;
static char *attrib_port;
static char *dac_reg, *dac_val;

static int	vram     =  0;

static struct vc_data nvvga_default;

#ifdef MODULE_PARM
MODULE_PARM(vram, "i");
#endif

static inline void write_vga(unsigned char reg, unsigned int val)
{
	unsigned int v1, v2;

	v1 = reg + (val & 0xff00);
	v2 = reg + 1 + ((val << 8) & 0xff00);
	writew(v1, nvvga_video_port_reg);
        writew(v2, nvvga_video_port_reg);
}

#undef scr_memsetw
#undef scr_memcpyw
#undef scr_memmovew

static inline void scr_memsetw(u16 *dest, u16 val, unsigned int length) {
    for(length >>= 1;length>0;length--) {
        scr_writew(val,dest);
        dest+=4;
    }
}

static inline void scr_memcpyw(u16 *dest, u16 *src, unsigned int length) {
    for(length >>= 1;length>0;length--) {
        scr_writew(scr_readw(src),dest);
        dest+=4;
        src+=4;
    }
}

static inline void scr_memmovew(u16 *dest, u16 *src, unsigned int length) {
    if(dest<src)scr_memcpyw(dest,src,length); 
    else {
        length >>= 1;
        dest+=4*length-4;
        src+=4*length-4;
        for(;length>0;length--) {
            scr_writew(scr_readw(src),dest);
            dest-=4;
            src-=4;
        }
    }
}

int nvvgacon_detect(void)
{
	unsigned long vram_base, mmio_base;
    	struct pci_dev *dev =  NULL;
        
       	while ((dev = pci_find_device(PCI_VENDOR_ID_NVIDIA,PCI_ANY_ID,dev))) { 
	        vram_base = dev->resource[1].start;
        	mmio_base = dev->resource[0].start;
        	nvvga_vram_base=ioremap(vram_base,8*64*1024);
        	nvvga_mmio_base0=ioremap(mmio_base+0x0c0000,1024);
        	nvvga_mmio_base1=ioremap(mmio_base+0x601000,1024);
        	nvvga_mmio_base2=ioremap(mmio_base+0x681000,1024);

        	nvvga_video_port_reg=nvvga_mmio_base1+0x3d4;
        	nvvga_video_port_val=nvvga_mmio_base1+0x3d5;
        	seq_port_reg=nvvga_mmio_base0+0x3c4;
        	seq_port_val=nvvga_mmio_base0+0x3c5;
        	video_misc_wr=nvvga_mmio_base0+0x3c2;
        	video_misc_rd=nvvga_mmio_base0+0x3cc;
        	dac_reg=nvvga_mmio_base2+0x3c8;
       	 	dac_val=nvvga_mmio_base2+0x3c9;
        	attrib_port=nvvga_mmio_base1+0x3c0;
	}
	if (!dev)
		return 1;
	return 0;
}

#ifdef MODULE
static const char *nvvgacon_startup(struct vt_struct *vt, int init)
#else
static const char __init *nvvgacon_startup(struct vt_struct *vt, int init)
#endif
{
	struct vc_data *vc = &nvvga_default;
        int i;
       
	if (!nvvgacon_detect()) {
		printk("nvvgacon: NVIDIA card not detected\n");
		return NULL;
	}

	vt->default_mode = vc;
 
	for (i=0; i<16; i++) {
            	readb(nvvga_mmio_base1+0x3da);
            	writeb(i, attrib_port);
            	writeb(i, attrib_port);
	}
        writeb(0x20, attrib_port);
	
	for (i=0; i<16; i++) {
		writeb (color_table[i], dac_reg) ;
                writeb (default_red[i], dac_val) ;
                writeb (default_grn[i], dac_val) ;
                writeb (default_blu[i], dac_val) ;
        }
	
	vc->vc_cols = 80;
	vc->vc_rows = 25;	
	vc->vc_font.height = 16;
	MOD_INC_USE_COUNT;
	return "NVVGACON";
}

static void nvvgacon_init(struct vc_data *vc)
{
	vc->vc_can_do_color = 1;
	vc->vc_complement_mask = 0x7700;

	vc->vc_cols = vc->display_fg->default_mode->vc_cols;
	vc->vc_rows = vc->display_fg->default_mode->vc_rows;
}

static inline void nvvga_set_mem_top(struct vc_data *c)
{
	write_vga(12, (c->vc_visible_origin)/2);
}

static void nvvgacon_deinit(struct vc_data *c)
{
	/* MOD_DEC_USE_COUNT; */
}

static u8 nvvgacon_build_attr(struct vc_data *c, u8 color, u8 intensity, u8 blink, u8 underline, u8 reverse)
{
	u8 attr = color;

	if (underline)
		attr = (attr & 0xf0) | c->vc_ulcolor;
	else if (intensity == 0)
		attr = (attr & 0xf0) | c->vc_halfcolor;
	if (reverse)
		attr = ((attr) & 0x88) | ((((attr) >> 4) | ((attr) << 4)) & 0x77);
	if (blink)
		attr ^= 0x80;
	if (intensity == 2)
		attr ^= 0x08;
	return attr;
}

static void nvvgacon_invert_region(struct vc_data *c, u16 *p, int count)
{
	while (count--) {
		u16 a = scr_readw(p);
			a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
		scr_writew(a, p);
		p++;
	}
}

static void nvvgacon_set_cursor_size(int xpos, int from, int to)
{
	unsigned long flags;
	int curs, cure;
	static int lastfrom, lastto;

	if ((from == lastfrom) && (to == lastto)) return;
	lastfrom = from; lastto = to;

	spin_lock_irqsave(&nvvgacon_lock, flags);
	writeb(0x0a, nvvga_video_port_reg);		/* Cursor start */
	curs = readb(nvvga_video_port_val);
	writeb(0x0b, nvvga_video_port_reg);		/* Cursor end */
	cure = readb(nvvga_video_port_val);

	curs = (curs & 0xc0) | from;
	cure = (cure & 0xe0) | to;

        writeb(0x0a, nvvga_video_port_reg);		/* Cursor start */
        writeb(curs, nvvga_video_port_val);
        writeb(0x0b, nvvga_video_port_reg);		/* Cursor end */
        writeb(cure, nvvga_video_port_val);
	spin_unlock_irqrestore(&nvvgacon_lock, flags);
}

static void nvvgacon_cursor(struct vc_data *vc, int mode)
{
    if (vc->vc_origin != vc->vc_visible_origin)
	nvvgacon_scrolldelta(vc, 0);
    switch (mode) {
	case CM_ERASE:
	    write_vga(14, (64*1024 - 1));
	    break;

	case CM_MOVE:
	case CM_DRAW:
	    write_vga(14, vc->vc_x + vc->vc_cols * vc->vc_y);
	    switch (vc->vc_cursor_type & 0x0f) {
		case CUR_UNDERLINE:
			nvvgacon_set_cursor_size(vc->vc_x,  
					vc->vc_font.height - (vc->vc_font.height < 10 ? 2 : 3),
					vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_TWO_THIRDS:
			nvvgacon_set_cursor_size(vc->vc_x, vc->vc_font.height/3, 
					 vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_THIRD:
			nvvgacon_set_cursor_size(vc->vc_x, 
					 (vc->vc_font.height*2) / 3,
					  vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_HALF:
			nvvgacon_set_cursor_size(vc->vc_x,vc->vc_font.height/2,
					 vc->vc_font.height - (vc->vc_font.height < 10 ? 1 : 2));
			break;
		case CUR_NONE:
			nvvgacon_set_cursor_size(vc->vc_x, 31, 30);
			break;
          	default:
			nvvgacon_set_cursor_size(vc->vc_x,1,vc->vc_font.height);
			break;
		}
	    break;
    }
}

static int nvvgacon_switch(struct vc_data *vc)
{
	/*
	 * We need to save screen size here as it's the only way
	 * we can spot the screen has been resized and we need to
	 * set size of freshly allocated screens ourselves.
	 */
        return 1;
}

static void nvvga_set_palette(struct vc_data *vc, unsigned char *table)
{
	int i, j ;

	for (i=j=0; i<16; i++) {
		writeb (table[i], dac_reg) ;
                writeb (vc->vc_palette[j++]>>2, dac_val) ;
                writeb (vc->vc_palette[j++]>>2, dac_val) ;
                writeb (vc->vc_palette[j++]>>2, dac_val) ;
	}
}

static int nvvgacon_set_palette(struct vc_data *vc, unsigned char *table)
{
	if (nvvga_palette_blanked)
		return -EINVAL;
	nvvga_set_palette(vc, table);
	return 0;
}

/* structure holding original VGA register settings */
static struct {
	unsigned char	SeqCtrlIndex;		/* Sequencer Index reg.   */
	unsigned char	CrtCtrlIndex;		/* CRT-Contr. Index reg.  */
	unsigned char	CrtMiscIO;		/* Miscellaneous register */
	unsigned char	HorizontalTotal;	/* CRT-Controller:00h */
	unsigned char	HorizDisplayEnd;	/* CRT-Controller:01h */
	unsigned char	StartHorizRetrace;	/* CRT-Controller:04h */
	unsigned char	EndHorizRetrace;	/* CRT-Controller:05h */
	unsigned char	Overflow;		/* CRT-Controller:07h */
	unsigned char	StartVertRetrace;	/* CRT-Controller:10h */
	unsigned char	EndVertRetrace;		/* CRT-Controller:11h */
	unsigned char	ModeControl;		/* CRT-Controller:17h */
	unsigned char	ClockingMode;		/* Seq-Controller:01h */
} nvvga_state;

static void nvvga_vesa_blank(int mode)
{
	/* save original values of VGA controller registers */
	if(!nvvga_vesa_blanked) {
		spin_lock_irq(&nvvgacon_lock);
		nvvga_state.SeqCtrlIndex = readb(seq_port_reg);
		nvvga_state.CrtCtrlIndex = readb(nvvga_video_port_reg);
		nvvga_state.CrtMiscIO = readb(video_misc_rd);
		spin_unlock_irq(&nvvgacon_lock);

		writeb(0x00,nvvga_video_port_reg);	/* HorizontalTotal */
		nvvga_state.HorizontalTotal = readb(nvvga_video_port_val);
		writeb(0x01,nvvga_video_port_reg);	/* HorizDisplayEnd */
		nvvga_state.HorizDisplayEnd = readb(nvvga_video_port_val);
		writeb(0x04,nvvga_video_port_reg);	/* StartHorizRetrace */
		nvvga_state.StartHorizRetrace = readb(nvvga_video_port_val);
		writeb(0x05,nvvga_video_port_reg);	/* EndHorizRetrace */
		nvvga_state.EndHorizRetrace = readb(nvvga_video_port_val);
		writeb(0x07,nvvga_video_port_reg);	/* Overflow */
		nvvga_state.Overflow = readb(nvvga_video_port_val);
		writeb(0x10,nvvga_video_port_reg);	/* StartVertRetrace */
		nvvga_state.StartVertRetrace = readb(nvvga_video_port_val);
		writeb(0x11,nvvga_video_port_reg);	/* EndVertRetrace */
		nvvga_state.EndVertRetrace = readb(nvvga_video_port_val);
		writeb(0x17,nvvga_video_port_reg);	/* ModeControl */
		nvvga_state.ModeControl = readb(nvvga_video_port_val);
		writeb(0x01,seq_port_reg);		/* ClockingMode */
		nvvga_state.ClockingMode = readb(seq_port_val);
	}

	/* assure that video is enabled */
	/* "0x20" is VIDEO_ENABLE_bit in register 01 of sequencer */
	spin_lock_irq(&nvvgacon_lock);
	writeb(0x01,seq_port_reg);
	writeb(nvvga_state.ClockingMode | 0x20,seq_port_val);

	/* test for vertical retrace in process.... */
	if ((nvvga_state.CrtMiscIO & 0x80) == 0x80)
		writeb(nvvga_state.CrtMiscIO & 0xef,video_misc_wr);

	/*
	 * Set <End of vertical retrace> to minimum (0) and
	 * <Start of vertical Retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	if (mode & VESA_VSYNC_SUSPEND) {
		writeb(0x10,nvvga_video_port_reg);	/* StartVertRetrace */
		writeb(0xff,nvvga_video_port_val); 	/* maximum value */
		writeb(0x11,nvvga_video_port_reg);	/* EndVertRetrace */
		writeb(0x40,nvvga_video_port_val);	/* minimum (bits 0..3)  */
		writeb(0x07,nvvga_video_port_reg);	/* Overflow */
		writeb(nvvga_state.Overflow | 0x84,nvvga_video_port_val); /* bits 9,10 of vert. retrace */
	}

	if (mode & VESA_HSYNC_SUSPEND) {
		/*
		 * Set <End of horizontal retrace> to minimum (0) and
		 *  <Start of horizontal Retrace> to maximum
		 * Result: turn off horizontal sync (HSync) pulse.
		 */
		writeb(0x04,nvvga_video_port_reg);	/* StartHorizRetrace */
		writeb(0xff,nvvga_video_port_val);	/* maximum */
		writeb(0x05,nvvga_video_port_reg);	/* EndHorizRetrace */
		writeb(0x00,nvvga_video_port_val);	/* minimum (0) */
	}

	/* restore both index registers */
	writeb(nvvga_state.SeqCtrlIndex,seq_port_reg);
	writeb(nvvga_state.CrtCtrlIndex,nvvga_video_port_reg);
	spin_unlock_irq(&nvvgacon);
}

static void nvvga_vesa_unblank(void)
{
	/* restore original values of VGA controller registers */
	spin_lock_irq(&nvvgacon_lock);
	writeb(nvvga_state.CrtMiscIO,video_misc_wr);

	writeb(0x00,nvvga_video_port_reg);		/* HorizontalTotal */
	writeb(nvvga_state.HorizontalTotal,nvvga_video_port_val);
	writeb(0x01,nvvga_video_port_reg);		/* HorizDisplayEnd */
	writeb(nvvga_state.HorizDisplayEnd,nvvga_video_port_val);
	writeb(0x04,nvvga_video_port_reg);		/* StartHorizRetrace */
	writeb(nvvga_state.StartHorizRetrace,nvvga_video_port_val);
	writeb(0x05,nvvga_video_port_reg);		/* EndHorizRetrace */
	writeb(nvvga_state.EndHorizRetrace,nvvga_video_port_val);
	writeb(0x07,nvvga_video_port_reg);		/* Overflow */
	writeb(nvvga_state.Overflow,nvvga_video_port_val);
	writeb(0x10,nvvga_video_port_reg);		/* StartVertRetrace */
	writeb(nvvga_state.StartVertRetrace,nvvga_video_port_val);
	writeb(0x11,nvvga_video_port_reg);		/* EndVertRetrace */
	writeb(nvvga_state.EndVertRetrace,nvvga_video_port_val);
	writeb(0x17,nvvga_video_port_reg);		/* ModeControl */
	writeb(nvvga_state.ModeControl,nvvga_video_port_val);
	writeb(0x01,seq_port_reg);		/* ClockingMode */
	writeb(nvvga_state.ClockingMode,seq_port_val);

	/* restore index/control registers */
	writeb(nvvga_state.SeqCtrlIndex,seq_port_reg);
	writeb(nvvga_state.CrtCtrlIndex,nvvga_video_port_reg);
	spin_unlock_irq(&nvvgacon_lock);
}

static void nvvga_pal_blank(void)
{
	int i;

	for (i=0; i<16; i++) {
		writeb (i, dac_reg) ;
		writeb (0, dac_val) ;
		writeb (0, dac_val) ;
		writeb (0, dac_val) ;
	}
}

static int nvvgacon_blank(struct vc_data *vc, int blank)
{
	switch (blank) {
	case 0:				/* Unblank */
		if (nvvga_vesa_blanked) {
			nvvga_vesa_unblank();
			nvvga_vesa_blanked = 0;
		}
		if (nvvga_palette_blanked) {
			nvvga_set_palette(vc, color_table);
			nvvga_palette_blanked = 0;
			return 0;
		}
		nvvga_is_gfx = 0;
		/* Tell console.c that it has to restore the screen itself */
		return 1;
	case 1:				/* Normal blanking */
            	nvvga_pal_blank();
                nvvga_palette_blanked = 1;
		nvvgacon_set_origin(vc);
		scr_memsetw((void *)nvvga_vram_base, BLANK, vc->vc_screenbuf_size);
		return 1;
	case -1:			/* Entering graphic mode */
		scr_memsetw((void *)nvvga_vram_base, BLANK, vc->vc_screenbuf_size);
		nvvga_is_gfx = 1;
		return 1;
	default:			/* VESA blanking */
            	nvvga_vesa_blank(blank-1);
                nvvga_vesa_blanked = blank;
		return 0;
	}
}

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

#define cmapsz 8192

static int
nvvgacon_do_font_op(char *arg, int set, int ch512)
{
	int i;
	char *charmap;
	int beg;
//	char * video_port_status = nvvga_video_port_reg + 6;
	int font_select = 0x00;

        charmap = nvvga_vram_base+3;
        beg = 0x06;
	
#ifdef BROKEN_GRAPHICS_PROGRAMS
	/*
	 * All fonts are loaded in slot 0 (0:1 for 512 ch)
	 */

	if (!arg)
		return -EINVAL;		/* Return to default font not supported */

	nvvga_font_is_default = 0;
	font_select = ch512 ? 0x04 : 0x00;
#else	
	/*
	 * The default font is kept in slot 0 and is never touched.
	 * A custom font is loaded in slot 2 (256 ch) or 2:3 (512 ch)
	 */

	if (set) {
		nvvga_font_is_default = !arg;
		if (!arg)
			ch512 = 0;		/* Default font is always 256 */
		font_select = arg ? (ch512 ? 0x0e : 0x0a) : 0x00;
	}

	if ( !nvvga_font_is_default )
		charmap += 4*cmapsz * 8;
#endif

	if (arg) {
		if (set)
			for (i=0; i<cmapsz ; i++)
				writeb(arg[i], charmap + i * 8);
		else
			for (i=0; i<cmapsz ; i++)
				arg[i] = readb(charmap + i * 8);

		/*
		 * In 512-character mode, the character map is not contiguous if
		 * we want to remain EGA compatible -- which we do
		 */

		if (ch512) {
			charmap += 2*cmapsz * 8;
			arg += cmapsz;
			if (set)
				for (i=0; i<cmapsz ; i++)
					writeb(arg[i], charmap + i * 8);
			else
				for (i=0; i<cmapsz ; i++)
					arg[i] = readb(charmap + i * 8);
		}
	}
	
	spin_lock_irq(&nvvgacon_lock);
	writeb( 0x00, seq_port_reg );   /* First, the sequencer */
	writeb( 0x01, seq_port_val );   /* Synchronous reset */
	if (set) {
		writeb( 0x03, seq_port_reg ); /* Character Map Select */
		writeb( font_select, seq_port_val );
	}
	writeb( 0x00, seq_port_reg );
	writeb( 0x03, seq_port_val );   /* clear synchronous reset */
	spin_unlock_irq(&nvvgacon_lock);	
	return 0;
}

/*
 * Adjust the screen to fit a font of a certain height
 */
static int
nvvgacon_adjust_height(struct vc_data *vc, unsigned fontheight)
{
	int rows, maxscan;
	unsigned char ovr, vde, fsr;

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

	spin_lock_irq(&nvvgacon_lock);
	writeb( 0x07, nvvga_video_port_reg );		/* CRTC overflow register */
	ovr = readb(nvvga_video_port_val);
	writeb( 0x09, nvvga_video_port_reg );		/* Font size register */
	fsr = readb(nvvga_video_port_val);
	spin_unlock_irq(&nvvgacon_lock);

	vde = maxscan & 0xff;			/* Vertical display end reg */
	ovr = (ovr & 0xbd) +			/* Overflow register */
	      ((maxscan & 0x100) >> 7) +
	      ((maxscan & 0x200) >> 3);
	fsr = (fsr & 0xe0) + (fontheight-1);    /*  Font size register */

	spin_lock_irq(&nvvgacon_lock);
	writeb( 0x07, nvvga_video_port_reg );		/* CRTC overflow register */
	writeb( ovr, nvvga_video_port_val );
	writeb( 0x09, nvvga_video_port_reg );		/* Font size */
	writeb( fsr, nvvga_video_port_val );
	writeb( 0x12, nvvga_video_port_reg );		/* Vertical display limit */
	writeb( vde, nvvga_video_port_val );
	spin_unlock_irq(&nvvgacon_lock);

        vc_resize(vc, rows, 0);   /* Adjust console size */
	return 0;
}

static int nvvgacon_font_op(struct vc_data *vc, struct console_font_op *op)
{
	int rc;

	if (op->op == KD_FONT_OP_SET) {
		if (op->width != 8 || (op->charcount != 256 && op->charcount != 512))
			return -EINVAL;
		rc = nvvgacon_do_font_op(op->data, 1, op->charcount == 512);
		if (!rc && !(op->flags & KD_FONT_FLAG_DONT_RECALC))
			rc = nvvgacon_adjust_height(vc, op->height);
	} else if (op->op == KD_FONT_OP_GET) {
		op->width = 8;
		op->height = vc->vc_font.height;
		op->charcount = nvvga_512_chars ? 512 : 256;
		if (!op->data) return 0;
		rc = nvvgacon_do_font_op(op->data, 0, 0);
	} else
		rc = -ENOSYS;
	return rc;
}

static int nvvgacon_set_origin(struct vc_data *vc)
{
	if (nvvga_is_gfx 	/* We don't play origin tricks in graphic modes */
#if 0
	    || (console_blanked && !nvvga_palette_blanked)
#endif
            )	/* Nor we write to blanked screens */
		return 0;
	nvvga_set_mem_top(vc);
	return 1;
}

static int nvvgacon_scrolldelta(struct vc_data *vc, int lines)
{
    return 0;
}

static int nvvgacon_scroll(struct vc_data *vc, int t, int b, int dir, int lines)
{
	if (!lines)
		return 0;

	if (lines > vc->vc_rows)   /* maximum realistic size */
		lines = vc->vc_rows;

	switch (dir) {

	case SM_UP:
	        nvvgacon_bmove(vc,lines, 0, 0, 0, vc->vc_rows-lines, 
			       vc->vc_cols);
                nvvgacon_clear(vc, vc->vc_rows - lines, 0, lines, 
			       vc->vc_cols);
		break;

	case SM_DOWN:
	        nvvgacon_bmove(vc, 0, 0, lines, 0, vc->vc_rows - lines, 
			       vc->vc_cols);
                nvvgacon_clear(vc, 0, 0, lines, vc->vc_cols);
		break;
	}

	return 0;
}

#define NVVGA_ADDR(vc,x,y) ((u16 *) nvvga_vram_base + ((y)*vc->vc_cols + (x))*4)

static void nvvgacon_putc(struct vc_data *vc, int ch, int y, int x)
{
	scr_writew(ch, NVVGA_ADDR(vc, x, y));
}

static void nvvgacon_putcs(struct vc_data *vc, const unsigned short *s,
		         int count, int y, int x)
{
	u16 *dest = NVVGA_ADDR(vc, x, y);

        for (; count > 0; count--) {
            scr_writew(readw(s), dest);
                s++;
                dest+=4;
        }
}

static void nvvgacon_clear(struct vc_data *vc, int y, int x, int height, 
			   int width)
{
	u16 *dest = NVVGA_ADDR(vc, x, y);
	u16 eattr = vc->vc_video_erase_char;

	if (width <= 0 || height <= 0)
		return;

        for (; height > 0; height--, dest += vc->vc_cols*4)
            scr_memsetw(dest,eattr,width*2);
}

static void nvvgacon_bmove(struct vc_data *vc, int sy, int sx, 
			 int dy, int dx, int height, int width)
{
	u16 *src, *dest;
        int i;

	if (width <= 0 || height <= 0) {
		return;
		
	} else if (dy < sy || (dy == sy && dx < sx)) {
		src  = NVVGA_ADDR(vc, sx, sy);
		dest = NVVGA_ADDR(vc, dx, dy);

		for (; height > 0; height--) {
			for(i=0;i<width;i++)scr_writew(scr_readw(src+4*i),dest+4*i);
			src  += vc->vc_cols*4;
			dest += vc->vc_cols*4;
		}
	} else {
		src  = NVVGA_ADDR(vc, sx, sy+height-1);
		dest = NVVGA_ADDR(vc, dx, dy+height-1);

		for (; height > 0; height--) {
			for(i=width-1;i>=0;i--)scr_writew(scr_readw(src+4*i),dest+4*i);
			src  -= vc->vc_cols*4;
			dest -= vc->vc_cols*4;
		}
	}
}

/*
 *  The console `switch' structure for the VGA based console
 */
const struct consw nvvga_con = {
	con_startup:		nvvgacon_startup,
	con_init:		nvvgacon_init,
	con_deinit:		nvvgacon_deinit,
	con_clear:		nvvgacon_clear,
        con_putc:		nvvgacon_putc,
        con_putcs:		nvvgacon_putcs,
	con_cursor:		nvvgacon_cursor,
	con_scroll:		nvvgacon_scroll,
        con_bmove:		nvvgacon_bmove,		
	con_switch:		nvvgacon_switch,
	con_blank:		nvvgacon_blank,
	con_font_op:		nvvgacon_font_op,
	con_set_palette:	nvvgacon_set_palette,
	con_scrolldelta:	nvvgacon_scrolldelta,
	con_build_attr:		nvvgacon_build_attr,
	con_invert_region:	nvvgacon_invert_region,
};

#ifndef MODULE
void __init nvvgacon_setup(char *str, int *ints)
{
	return;
}
#endif

int __init nvvgacon_module_init(void)
{
	const char *display_desc = NULL;
        struct vt_struct *vt;
	struct vc_data *vc;
        long q;

	/* Allocate the memory we need */
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
	vt->vt_sw = &nvvga_con;
	vt->vcs.vc_cons[0] = vc;
#ifdef MODULE
        display_desc = vt_map_display(vt, 1);
#else 	
	display_desc = vt_map_display(vt, 0);
#endif
	q = (long) kmalloc(vc->vc_screenbuf_size, GFP_KERNEL);
        if (!display_desc || !q) {
		kfree(vt->vcs.vc_cons[0]);
		kfree(vt->default_mode);
                kfree(vt);
		if (q)
			kfree((char *) q);
                return;
        }
	vc->vc_screenbuf = (unsigned short *) q;
	vc_init(vc, 1);
        printk("Console: color %s %dx%d\n", display_desc, vc->vc_cols, vc->vc_rows);
}

void __exit nvvgacon_module_exit()
{
	/* release_vt(&nvvga_vt); */
}

module_init(nvvgacon_module_init);
module_exit(nvvgacon_module_exit);

MODULE_LICENSE("GPL");

