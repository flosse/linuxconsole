/*
 *  linux/drivers/video/fbcon-vga8-planes.c -- Low level frame buffer operations
 *				  for VGA 256 color, 4-plane modes
 *
 *      Created Feb 2, 1999, by Petr Vandrovec <vandrove@vc.cvut.cz>
 *	                     and Ben Pfaff
 *	Based on code by Michael Schmitz
 *	Based on the old macfb.c 4bpp code by Alan Cox
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/string.h>
#include <linux/fb.h>
#include <linux/vt_buffer.h>

#include "fbcon.h"
#include "fbcon-vga-planes.h"
#include "vga.h"

#define GRAPHICS_ADDR_REG VGA_GFX_I	/* Graphics address register. */
#define GRAPHICS_DATA_REG VGA_GFX_D	/* Graphics data register. */

#define SET_RESET_INDEX VGA_GFX_SR_VALUE		/* Set/Reset Register index. */
#define ENABLE_SET_RESET_INDEX VGA_GFX_SR_ENABLE	/* Enable Set/Reset Register index. */
#define DATA_ROTATE_INDEX VGA_GFX_DATA_ROTATE		/* Data Rotate Register index. */
#define GRAPHICS_MODE_INDEX VGA_GFX_MODE		/* Graphics Mode Register index. */
#define BIT_MASK_INDEX VGA_GFX_BIT_MASK		/* Bit Mask Register index. */

/* The VGA's weird architecture often requires that we read a byte and
   write a byte to the same location.  It doesn't matter *what* byte
   we write, however.  This is because all the action goes on behind
   the scenes in the VGA's 32-bit latch register, and reading and writing
   video memory just invokes latch behavior.

   To avoid race conditions (is this necessary?), reading and writing
   the memory byte should be done with a single instruction.  One
   suitable instruction is the x86 bitwise OR.  The following
   read-modify-write routine should optimize to one such bitwise
   OR. */
static inline void rmw(volatile char *p)
{
	readb(p);
	writeb(0, p);
}

/* Set the Graphics Mode Register, and return its previous value.
   Bits 0-1 are write mode, bit 3 is read mode. */
static inline int setmode(int mode)
{
	int oldmode;
	vga_io_w(GRAPHICS_ADDR_REG, GRAPHICS_MODE_INDEX);
	oldmode = vga_io_r(GRAPHICS_DATA_REG);
	vga_io_w(GRAPHICS_DATA_REG, mode);
	return oldmode;
}

/* Select the Bit Mask Register and return its value. */
static inline int selectmask(void)
{
	return vga_io_rgfx(BIT_MASK_INDEX);
}

/* Set the value of the Bit Mask Register. */
static inline void setmask(int mask)
{
	vga_io_w(GRAPHICS_DATA_REG, mask);
}

/* Set the Data Rotate Register and return its old value.  Bits 0-2
   are rotate count, bits 3-4 are logical operation (0=NOP, 1=AND,
   2=OR, 3=XOR). */
static inline int setop(int op)
{
	int oldop;
	vga_io_w(GRAPHICS_ADDR_REG, DATA_ROTATE_INDEX);
	oldop = vga_io_r(GRAPHICS_DATA_REG);
	vga_io_w(GRAPHICS_DATA_REG, op);
	return oldop;
}

/* Set the Enable Set/Reset Register and return its old value.  The
   code here always uses value 0xf for this register.  */
static inline int setsr(int sr)
{
	int oldsr;
	vga_io_w(GRAPHICS_ADDR_REG, ENABLE_SET_RESET_INDEX);
	oldsr = vga_io_r(GRAPHICS_DATA_REG);
	vga_io_w(GRAPHICS_DATA_REG, sr);
	return oldsr;
}

/* Set the Set/Reset Register and return its old value. */
static inline int setcolor(int color)
{
	int oldcolor;
	vga_io_w(GRAPHICS_ADDR_REG, SET_RESET_INDEX);
	oldcolor = vga_io_r(GRAPHICS_DATA_REG);
	vga_io_w(GRAPHICS_DATA_REG, color);
	return oldcolor;
}

/* Return the value in the Graphics Address Register. */
static inline int getindex(void)
{
	return vga_io_r(GRAPHICS_ADDR_REG);
}

/* Set the value in the Graphics Address Register. */
static inline void setindex(int index)
{
	vga_io_w(GRAPHICS_ADDR_REG, index);
}

static void fbcon_vga8_planes_setup(struct display *p)
{
}

static void fbcon_vga8_planes_bmove(struct display *p, int sy, int sx,
				    int dy, int dx, int height, int width)
{
	char oldindex = getindex();
	char oldmode = setmode(0x41);
	char oldop = setop(0);
	char oldsr = setsr(0xf);

	char *src;
	char *dest;
	int line_ofs;
	int x;

	sy *= fontheight(p);
	dy *= fontheight(p);
	height *= fontheight(p);
	sx *= fontwidth(p) / 4;
	dx *= fontwidth(p) / 4;
	width *= fontwidth(p) / 4;
	if (dy < sy || (dy == sy && dx < sx)) {
		line_ofs = p->line_length - width;
		dest = p->screen_base + dx + dy * p->line_length;
		src = p->screen_base + sx + sy * p->line_length;
		while (height--) {
			for (x = 0; x < width; x++) {
				readb(src);
				writeb(0, dest);
				src++;
				dest++;
			}
			src += line_ofs;
			dest += line_ofs;
		}
	} else {
		line_ofs = p->line_length - width;
		dest = p->screen_base + dx + width + (dy + height - 1) * p->line_length;
		src = p->screen_base + sx + width + (sy + height - 1) * p->line_length;
		while (height--) {
			for (x = 0; x < width; x++) {
				--src;
				--dest;
				readb(src);
				writeb(0, dest);
			}
			src -= line_ofs;
			dest -= line_ofs;
		}
	}

	setsr(oldsr);
	setop(oldop);
	setmode(oldmode);
	setindex(oldindex);
}

static void fbcon_vga8_planes_clear(struct vc_data *conp, struct display *p,
				    int sy, int sx, int height, int width)
{
	char oldindex = getindex();
	char oldmode = setmode(0x40);
	char oldop = setop(0);
	char oldsr = setsr(0);
	char oldmask = selectmask();

	int line_ofs;
	char *where;
	int color = attr_bgcol_ec(p, conp);

	width *= fontwidth(p) / 4;
	sx *= fontwidth(p) / 4;
	line_ofs = p->line_length - width;
	setmask(0xff);

	sy *= fontheight(p);
	height *= fontheight(p);

	where = p->screen_base + sx + sy * p->line_length;
	while (height--) {
		int x;

		/* we can do memset... */
		for (x = width; x > 0; --x) {
			writeb(color, where);
			where++;
		}
		where += line_ofs;
	}

	setmask(oldmask);
	setsr(oldsr);
	setop(oldop);
	setmode(oldmode);
	setindex(oldindex);
}

#ifdef __LITTLE_ENDIAN
static unsigned int transl_l[] =
{0x0,0x8,0x4,0xC,0x2,0xA,0x6,0xE,0x1,0x9,0x5,0xD,0x3,0xB,0x7,0xF};
static unsigned int transl_h[] =
{0x000, 0x800, 0x400, 0xC00, 0x200, 0xA00, 0x600, 0xE00,
 0x100, 0x900, 0x500, 0xD00, 0x300, 0xB00, 0x700, 0xF00};
#else
#ifdef __BIG_ENDIAN
static unsigned int transl_h[] =
{0x0,0x8,0x4,0xC,0x2,0xA,0x6,0xE,0x1,0x9,0x5,0xD,0x3,0xB,0x7,0xF};
static unsigned int transl_l[] =
{0x000, 0x800, 0x400, 0xC00, 0x200, 0xA00, 0x600, 0xE00,
 0x100, 0x900, 0x500, 0xD00, 0x300, 0xB00, 0x700, 0xF00};
#else
#error "Only __BIG_ENDIAN and __LITTLE_ENDIAN are supported in vga-planes"
#endif
#endif
static void fbcon_vga8_planes_putc(struct vc_data *conp, struct display *p,
				   int c, int yy, int xx)
{
	int fg = attr_fgcol(p,c);
	int bg = attr_bgcol(p,c);

	char oldindex = getindex();
	char oldmode = setmode(0x40);
	char oldop = setop(0);
	char oldsr = setsr(0);
	char oldmask = selectmask();

	int y;
	u8 *cdat = p->fontdata + (c & p->charmask) * fontheight(p);
	char *where;

	xx *= fontwidth(p) / 4;
	where = p->screen_base + xx + yy * p->line_length * fontheight(p);

	setmask(0xff);
	writeb(bg, where);
	readb(where);
	selectmask();
	setmask(fg ^ bg);
	setmode(0x42);
	setop(0x18);
	for (y = 0; y < fontheight(p); y++, where += p->line_length) {
		writew(transl_h[cdat[y]&0xF] | transl_l[cdat[y] >> 4],
		       where);
	}
	setmask(oldmask);
	setsr(oldsr);
	setop(oldop);
	setmode(oldmode);
	setindex(oldindex);
}

static void fbcon_vga8_planes_putcs(struct vc_data *conp, struct display *p, 
				    const unsigned short *s, int count, 
				    int yy, int xx)
{
	int fg = attr_fgcol(p,scr_readw(s));
	int bg = attr_bgcol(p,scr_readw(s));

	char oldindex = getindex();
	char oldmode = setmode(0x40);
	char oldop = setop(0);
	char oldsr = setsr(0);
	char oldmask = selectmask();

	char *where;
	int n;

	xx *= fontwidth(p) / 4;
	/* First clear it all to the background color. */
	setmask(0xff);
	where = p->screen_base + xx + yy * p->line_length * fontheight(p);
	writeb(bg, where);
	readb(where); /* fill latches with background */
	selectmask();
	setmask(fg ^ bg);
	setmode(0x42);
	setop(0x18);
	for (n = 0; n < count; n++) {
		int y;
		int c = scr_readw(s++) & p->charmask;
		u8 *cdat = p->fontdata + (c & p->charmask) * fontheight(p);

		for (y = 0; y < fontheight(p); y++, cdat++) {
			writew(transl_h[*cdat&0xF] | transl_l[*cdat >> 4],
			       where);
			where += p->line_length;
		}
		where += 2 - p->line_length * fontheight(p);
	}
	
	selectmask();
	setmask(oldmask);
	setop(oldop);
	setmode(oldmode);
	setsr(oldsr);
	setindex(oldindex);
}

static void fbcon_vga8_planes_revc(struct display *p, int xx, int yy)
{
	char oldindex = getindex();
	char oldmode = setmode(0x40);
	char oldop = setop(0x18);
	char oldsr = setsr(0xf);
	char oldcolor = setcolor(0xf);
	char oldmask = selectmask();

	char *where;
	int y;

	xx *= fontwidth(p) / 4;
	where = p->screen_base + xx + yy * p->line_length * fontheight(p);
	
	setmask(0x0F);
	for (y = 0; y < fontheight(p); y++) {
		rmw(where);
		rmw(where+1);
		where += p->line_length;
	}

	setmask(oldmask);
	setcolor(oldcolor);
	setsr(oldsr);
	setop(oldop);
	setmode(oldmode);
	setindex(oldindex);
}

struct display_switch fbcon_vga8_planes = {
	setup:          fbcon_vga8_planes_setup,
       	bmove:          fbcon_vga8_planes_bmove,
       	clear:          fbcon_vga8_planes_clear,
       	putc:           fbcon_vga8_planes_putc,
       	putcs:          fbcon_vga8_planes_putcs,
       	revc:           fbcon_vga8_planes_revc,
       	fontwidthmask:  FONTWIDTH(8)
};

#ifdef MODULE
int init_module(void)
{
    return 0;
}

void cleanup_module(void)
{}
#endif /* MODULE */


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fbcon_vga8_planes);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

