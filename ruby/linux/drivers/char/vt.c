/*
 * vt.c - Built-in console device
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1999, 2000  Dominik Kubla
 *
 * $Id$
 *
 * See README.console for list of contributors.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/consolemap.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/config.h>
#include <linux/version.h>
#include <linux/tqueue.h>
#include <linux/bootmem.h>
#include <linux/pm.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include "console_macros.h"

/* A bitmap for codes <32. A bit of 1 indicates that the code
 * corresponding to that bit number invokes some special action
 * (such as cursor movement) and should not be displayed as a
 * glyph unless the disp_ctrl mode is explicitly enabled.
 */
#define CTRL_ACTION 0x0d00ff81
#define CTRL_ALWAYS 0x0800f501  /* Cannot be overridden by disp_ctrl */

extern void vcs_make_devfs (unsigned int index, int unregister);
#ifdef CONFIG_VGA_CONSOLE
extern void vga_console_init(void);
#endif
#ifdef CONFIG_MDA_CONSOLE
extern void mda_console_init(void);	
#endif
#if defined (CONFIG_PROM_CONSOLE)
extern void prom_con_init(void);
#endif
#if defined (CONFIG_FRAMEBUFFER_CONSOLE)
extern void fb_console_init(void);
#endif

#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif

static unsigned int current_vc;		/* Which /dev/vc/X to allocate next */
struct vt_struct *admin_vt;		/* Administrative VT */
struct vt_struct *vt_cons;		/* Head to link list of VTs */

#ifdef CONFIG_VT_CONSOLE
struct console vt_console_driver;
static int kmsg_redirect; 		/* kmsg_redirect is the VC for printk*/ 
static int printable;           	/* Is console ready for printing? */
#endif

/* 
 * the default colour table, for VGA+ colour systems 
 */
int default_red[] = {0x00,0xaa,0x00,0xaa,0x00,0xaa,0x00,0xaa,
    0x55,0xff,0x55,0xff,0x55,0xff,0x55,0xff};
int default_grn[] = {0x00,0x00,0xaa,0x55,0x00,0x00,0xaa,0xaa,
    0x55,0x55,0xff,0xff,0x55,0x55,0xff,0xff};
int default_blu[] = {0x00,0x00,0x00,0x00,0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,0xff,0xff,0xff,0xff};     

unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
                                       8,12,10,14, 9,13,11,15 };  

void respond_string(const char * p, struct tty_struct * tty)
{
        while (*p) {
                tty_insert_flip_char(tty, *p, 0);
                p++;
        }
        tty_schedule_flip(tty);
}

/*
 * Hook so that the power management routines can (un)blank
 * the console on our behalf.
 */
int (*console_blank_hook)(int);

/*
 * Console cursor handling
 */

void add_softcursor(struct vc_data *vc)
{
        int i = scr_readw((u16 *) pos);
	u32 type = cursor_type;

        if (! (type & 0x10)) return;
	if (softcursor_original != -1) return;
	softcursor_original = i;
        i |= ((type >> 8) & 0xff00 );
        i ^= ((type) & 0xff00 );
        if ((type & 0x20) && ((softcursor_original & 0x7000) == (i & 0x7000)))
		i ^= 0x7000;
        if ((type & 0x40) && ((i & 0x700) == ((i & 0x7000) >> 4))) i ^= 0x0700;
        scr_writew(i, (u16 *) pos);
        if (IS_VISIBLE)
                sw->con_putc(vc, i, y, x);
}

void hide_cursor(struct vc_data *vc)
{
/*
	if (visible_origin != origin) {
		set_origin(vc);
		do_update_region(vc, origin, screensize);
	}
*/
        if (cons_num == sel_cons)
                clear_selection();
        if (softcursor_original != -1) {
                scr_writew(softcursor_original,(u16 *) pos);
                if (IS_VISIBLE)
			sw->con_putc(vc, softcursor_original, y, x);
                softcursor_original = -1;
        }
	if (sw->con_cursor)
		sw->con_cursor(vc, CM_ERASE);
}

void set_cursor(struct vc_data *vc)
{
    	if (!IS_VISIBLE || vc->display_fg->vt_blanked || vcmode == KD_GRAPHICS)
        	return;
	if (dectcem) {
        	if (cons_num == sel_cons)
                	clear_selection();
        	add_softcursor(vc);
        	if (((cursor_type & 0x0f) != 1) && sw->con_cursor) {
			/*
			if (visible_origin != origin)
				set_origin(vc);
			*/
            		sw->con_cursor(vc, CM_DRAW);
		}
    	} else
        	hide_cursor(vc);
}

void update_cursor_attr(struct vc_data *vc)
{
	if (!IS_VISIBLE || vc->display_fg->vt_blanked || vcmode == KD_GRAPHICS)
                return;

	if (dectcem) {
        	if (cons_num == sel_cons)
                	clear_selection();
		if (((cursor_type & 0x0f) != 1) && sw->con_cursor)
			sw->con_cursor(vc, CM_CHANGE);
        }	
}

/*
 * gotoxy() must verify all boundaries, because the arguments
 * might also be negative. If the given position is out of
 * bounds, the cursor is placed at the nearest margin.
 */
void gotoxy(struct vc_data *vc, int new_x, int new_y)
{
        int min_y, max_y;

        if (new_x < 0)
                x = 0;                                                                  else
                if (new_x >= video_num_columns)
                        x = video_num_columns - 1;
                else
                        x = new_x;
        if (decom) {
                min_y = top;
                max_y = bottom;
        } else {
                min_y = 0;
                max_y = video_num_lines;
        }
        if (new_y < min_y)
                y = min_y;
        else if (new_y >= max_y)
                y = max_y - 1;
        else
                y = new_y;
        pos = origin + y*video_size_row + (x<<1);
        need_wrap = 0;
}

/* for absolute user moves, when decom is set */
inline void gotoxay(struct vc_data *vc, int new_x, int new_y)
{
        gotoxy(vc, new_x, decom ? (top+new_y) : new_y);
}

/*
 *      Palettes
 */
void set_palette(struct vc_data *vc)
{
        if (IS_VISIBLE && sw->con_set_palette && vcmode != KD_GRAPHICS)
                sw->con_set_palette(vc, color_table);
}

void reset_palette(struct vc_data *vc)
{
        int j, k;

        for (j=k=0; j<16; j++) {
                palette[k++] = default_red[j];
                palette[k++] = default_grn[j];
                palette[k++] = default_blu[j];
        }
        set_palette(vc);
}

/*
 * Functions to handle console scrolling.
 */
void scroll_up(struct vc_data *vc, int lines)
{
	unsigned short *s = screenbuf + video_num_columns*lines;
	unsigned short *d = screenbuf;

	if (!lines)
		return;

        scr_memcpyw(d, s, screenbuf_size - video_size_row*lines);
        d = (unsigned short *) (scr_end - video_size_row*lines);
	scr_memsetw(d, video_erase_char, video_size_row*lines);
       	if (IS_VISIBLE) {
		if (visible_origin != origin)
			set_origin(vc);
		if (!sw->con_scroll || sw->con_scroll(vc, -lines))
			do_update_region(vc, origin, screensize);
	}
}

void scroll_down(struct vc_data *vc, int lines)
{
	unsigned short *d = screenbuf + video_num_columns*lines;
	unsigned short *s = screenbuf;

	if (!lines)
		return;        

        scr_memmovew(d, s, screenbuf_size - video_size_row*lines);
        scr_memsetw(s, video_erase_char, video_size_row*lines);

       	if (IS_VISIBLE) {
		if (visible_origin != origin)
			set_origin(vc);
		if (!sw->con_scroll || sw->con_scroll(vc, lines))
			do_update_region(vc, origin, screensize);
	}
}

void scroll_region_up(struct vc_data *vc,unsigned int t,unsigned int b,int nr)
{
        unsigned short *d, *s;

        if (t+nr >= b)
                nr = b - t - 1;
        if (b > video_num_lines || t >= b || nr < 1)
                return;
        d = (unsigned short *) (origin + video_size_row*t);
        s = (unsigned short *) (origin + video_size_row*(t+nr));
        scr_memcpyw(d, s, (b-t-nr) * video_size_row);
        scr_memsetw(d + (b-t-nr) * video_num_columns, video_erase_char, video_size_row*nr);
	if (IS_VISIBLE) {
		sw->con_scroll_region(vc, t, b, SM_UP, nr);
		sw->con_clear(vc, 0, b-nr, video_num_columns, nr); 
	}
}

void scroll_region_down(struct vc_data *vc,unsigned int t,unsigned int b,int nr)
{
        unsigned short *s;
        unsigned int step;

        if (t+nr >= b)
                nr = b - t - 1;
        if (b > video_num_lines || t >= b || nr < 1)
                return;
        s = (unsigned short *) (origin + video_size_row*t);
        step = video_num_columns * nr;
        scr_memmovew(s + step, s, (b-t-nr)*video_size_row);
        scr_memsetw(s, video_erase_char, 2*step);
	if (IS_VISIBLE) {
		sw->con_scroll_region(vc, t, b, SM_DOWN, nr);
		sw->con_clear(vc, 0, t, video_num_columns, nr); 
	}
}

/*
 * Console attribute handling. Structure of attributes is hardware-dependent
 */
void default_attr(struct vc_data *vc)
{
        intensity = 1;
        underline = 0;
        reverse = 0;
        blink = 0;
        color = def_color;
}

/*
 * ++roman: I completely changed the attribute format for monochrome
 * mode (!can_do_color). The formerly used MDA (monochrome display
 * adapter) format didn't allow the combination of certain effects.
 * Now the attribute is just a bit vector:
 *  Bit 0..1: intensity (0..2)
 *  Bit 2   : underline
 *  Bit 3   : reverse
 *  Bit 7   : blink
 */
static u8 build_attr(struct vc_data *vc, u8 _color, u8 _intensity, u8 _blink, u8 _underline, u8 _reverse)
{
	u8 a;	

	if (sw->con_build_attr)
        	return sw->con_build_attr(vc, _color, _intensity, _blink, _underline, _reverse);
        a = color;
        if (!can_do_color)
                return _intensity |
                       (_underline ? 4 : 0) |
                       (_reverse ? 8 : 0) |
                       (_blink ? 0x80 : 0);
        if (_underline)
                a = (a & 0xf0) | ulcolor;
        else if (_intensity == 0)
                a = (a & 0xf0) | halfcolor;
        if (_reverse)
                a = ((a) & 0x88) | ((((a) >> 4) | ((a) << 4)) & 0x77);
        if (_blink)
                a ^= 0x80;
        if (_intensity == 2)
                a ^= 0x08;
        if (hi_font_mask == 0x100)
                a <<= 1;
        return a;
}

void update_attr(struct vc_data *vc)
{
        attr = build_attr(vc, color, intensity, blink, underline, reverse ^ decscnm);
        video_erase_char = (build_attr(vc, color, intensity, 0, 0, decscnm) << 8) | ' ';
}

/*
 *  Character management
 */
void insert_char(struct vc_data *vc, unsigned int nr)
{
        unsigned short *p, *q = (unsigned short *) pos;

        p = q + video_num_columns - nr - x;
        while (--p >= q)
                scr_writew(scr_readw(p), p + nr);
        scr_memsetw(q, video_erase_char, nr*2);
        need_wrap = 0;
        if (IS_VISIBLE && sw->con_bmove && sw->con_putc) {
                unsigned short oldattr = attr;
                sw->con_bmove(vc, y, x, y, x+nr, 1, video_num_columns-x-nr);
                attr = video_erase_char >> 8;
                while (nr--)
                        sw->con_putc(vc, video_erase_char, y, x+nr);
                attr = oldattr;
        }
}

void delete_char(struct vc_data *vc, unsigned int nr)
{
        unsigned int i = x;
        unsigned short *p = (unsigned short *) pos;

        while (++i <= video_num_columns - nr) {
                scr_writew(scr_readw(p+nr), p);
                p++;
        }
        scr_memsetw(p, video_erase_char, nr*2);
        need_wrap = 0;
        if (IS_VISIBLE && sw->con_bmove && sw->con_putc) {
                unsigned short oldattr = attr;
                sw->con_bmove(vc, y, x+nr, y, x, 1, video_num_columns-x-nr);
                attr = video_erase_char >> 8;
                while (nr--)
                        sw->con_putc(vc, video_erase_char, y, 
					video_num_columns-1-nr);
                attr = oldattr;
        }
}

void insert_line(struct vc_data *vc, unsigned int nr)
{
        scroll_region_down(vc, y, bottom, nr);
        need_wrap = 0;
}

void delete_line(struct vc_data *vc, unsigned int nr)
{
        scroll_region_up(vc, y, bottom, nr);
        need_wrap = 0;
}

/*
 * Functions that manage whats displayed on the screen
 */
void set_origin(struct vc_data *vc)
{
	scr_end = (unsigned long) screenbuf + screenbuf_size;
        origin = scr_end - 2*screensize; 
        visible_origin = origin;
        pos = origin + video_size_row*y + 2*x;
	if (IS_VISIBLE && sw->con_set_origin)
		sw->con_set_origin(vc);
}

inline void clear_region(struct vc_data *vc,int sx,int sy,int width,int height) 
{
	/* Clears the video memory, not the screen buffer */
        if (IS_VISIBLE && sw->con_clear)
                return sw->con_clear(vc, sx, sy, width, height);
}

void do_update_region(struct vc_data *vc, unsigned long start, int count)
{
	unsigned int xx, yy, offset;
        u16 *p = (u16 *) start;

        offset = (start - visible_origin) / 2;
        xx = offset % video_num_columns;
        yy = offset / video_num_columns;
       
	for(;;) {
                u16 attrib = scr_readw(p) & 0xff00;
                int startx = xx;
                u16 *q = p;
                while (xx < video_num_columns && count) {
                        if (attrib != (scr_readw(p) & 0xff00)) {
                                if (p > q)
                                        sw->con_putcs(vc, q, p-q, yy, startx);
                                startx = xx;
                                q = p;
                                attrib = scr_readw(p) & 0xff00;
                        }
                        p++;
                        xx++;
                        count--;
                }
                if (p > q)
                        sw->con_putcs(vc, q, p-q, yy, startx);
                if (!count)
                        break;
                xx = 0;
                yy++;
        }
}

void update_region(struct vc_data *vc, unsigned long start, int count)
{
        if (IS_VISIBLE) {
                hide_cursor(vc);
                do_update_region(vc, start, count);
                set_cursor(vc);
        }
}

/*      Redrawing of screen */
void update_screen(struct vc_data *vc)
{
        hide_cursor(vc);
        set_origin(vc);
        set_palette(vc);

        if (vcmode != KD_GRAPHICS) {
               /* Update the screen contents */
               do_update_region(vc, origin, screensize);
        }
        set_cursor(vc);
}

inline unsigned short *screenpos(struct vc_data *vc, int offset, int viewed)
{
        unsigned short *p;

        if (!viewed)
                p = (unsigned short *)(origin + offset);
        else 
                p = (unsigned short *)(visible_origin + offset);
        return p;
}

/* Note: inverting the screen twice should revert to the original state */
void invert_screen(struct vc_data *vc, int offset, int count, int viewed)
{
        unsigned short *p;

        count /= 2;
        p = screenpos(vc, offset, viewed);
        if (sw->con_invert_region)
                sw->con_invert_region(vc, p, count);
        else {
                int cnt = count;
                u16 *q = p;
		u16 a;

                if (!can_do_color) {
                        while (cnt--) {
				a = scr_readw(q);
				a ^= 0x0800;
				scr_writew(a, q);
				q++;
			}
                } else if (hi_font_mask == 0x100) {
                        while (cnt--) {
				u16 a = scr_readw(q);
                                a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) | (((a) & 0x0e00) << 4);
                                scr_writew(a, q);
				q++;
                        }
                } else {
                        while (cnt--) {
				u16 a = scr_readw(q);
                                a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
                                scr_writew(a, q);
				q++;
                        }
                }
        }
        if (IS_VISIBLE)
                do_update_region(vc, (unsigned long) p, count);
}

/* used by selection: complement pointer position */
void complement_pos(struct vc_data *vc, int offset)
{
	static unsigned short old, oldx, oldy;
        static unsigned short *p;

        if (p) {
                scr_writew(old, p);
                if (IS_VISIBLE && sw->con_putc)
                        sw->con_putc(vc, old, oldy, oldx);
        }
        if (offset == -1)
                p = NULL;
        else {
                unsigned short new;
                p = screenpos(vc, offset, 1);
                old = scr_readw(p);
                new = old ^ complement_mask;
                scr_writew(new, p);
                if (IS_VISIBLE && sw->con_putc) {
                        oldx = (offset >> 1) % video_num_columns;
                        oldy = (offset >> 1) / video_num_columns;
                        sw->con_putc(vc, new, oldy, oldx);
                }
        }
}

inline int resize_screen(struct vc_data *vc, int width, int height)
{
        /* Resizes the resolution of the display adapater */
	int err = 0;

        if (IS_VISIBLE && vcmode != KD_GRAPHICS && sw->con_resize)
               	err = sw->con_resize(vc, width, height);
	return err;
}

/*
 *      Screen blanking
 */
static void powerdown_screen(unsigned long private)
{
	struct vt_struct *vt = (struct vt_struct *) private;
    	/*
     	 *  Power down if currently suspended (1 or 2),
    	 *  suspend if currently blanked (0),
     	 *  else do nothing (i.e. already powered down (3)).
    	 *  Called only if powerdown features are allowed.
     	 */
	switch (vt->blank_mode) {
        	case VESA_NO_BLANKING:
            	   	vt->vt_sw->con_blank(vt->fg_console, 
					     VESA_VSYNC_SUSPEND+1);
            		break;
        	case VESA_VSYNC_SUSPEND:
        	case VESA_HSYNC_SUSPEND:
            		vt->vt_sw->con_blank(vt->fg_console, VESA_POWERDOWN+1);
            		break;
    	}
}

static void blank_screen(unsigned long private)
{
	struct vt_struct *vt = (struct vt_struct *) private;
        int i;

        if (vt->vt_blanked)
                return;

        /* don't blank graphics */
        if (vt->fg_console->vc_mode != KD_TEXT) {
                vt->vt_blanked = 1;
                return;
        }

        hide_cursor(vt->fg_console);

        if (vt->off_interval) {
        	vt->timer.function = powerdown_screen;
        	vt->timer.expires = jiffies + vt->off_interval;
        	add_timer(&vt->timer);
        } 
        
        /* In case we need to reset origin, blanking hook returns 1 */
        i = vt->vt_sw->con_blank(vt->fg_console, 1);
        vt->vt_blanked = 1;        
        if (i)
                set_origin(vt->fg_console);

        if (console_blank_hook && console_blank_hook(1))
                return;
        if (vt->blank_mode)
		vt->vt_sw->con_blank(vt->fg_console, vt->blank_mode + 1);
}

void unblank_screen(struct vt_struct *vt)
{
        if (!vt->vt_blanked)
                return;
	
	/* We might be powering down so we delete the current timer */	
	del_timer(&vt->timer);
        
	if (vt->blank_interval) {
		vt->timer.function = blank_screen;
		vt->timer.expires = jiffies + vt->blank_interval;
		add_timer(&vt->timer);
        }
	
	if (vt->fg_console->vc_mode != KD_TEXT)
               	return; /* but leave console_blanked != 0 */

        vt->vt_blanked = 0;
        if (console_blank_hook)
                console_blank_hook(0);
	set_palette(vt->fg_console);
        if (vt->vt_sw->con_blank(vt->fg_console, 0))
                /* Low-level driver cannot restore -> do it ourselves */
                update_screen(vt->fg_console);
        set_cursor(vt->fg_console);
}

void poke_blanked_console(struct vt_struct *vt)
{
        if (vt->fg_console->vc_mode == KD_GRAPHICS)
                return;
	if (vt->vt_blanked) {
		unblank_screen(vt);
  	} else if (vt->blank_interval) {
		mod_timer(&vt->timer, jiffies + vt->blank_interval);
	} 
}

/*
 * Power management for the console system.
 */
int pm_con_request(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	struct vt_struct *vt = dev->data;
	
	if (vt) {	
		switch (rqst) {
 			case PM_RESUME:
                        	unblank_screen(vt);
                        	break;
  			case PM_SUSPEND:
                        	blank_screen((unsigned long) vt);
                        	break;
  		}
	}
        return 0;
}

/*
 * This is the console switching callback.
 *
 * Doing console switching in a process context allows
 * us to do the switches asynchronously (needed when we want
 * to switch due to a keyboard interrupt).  Synchronization
 * with other console code and prevention of re-entrancy is
 * ensured with console semaphore.
 */
static void vt_callback(void *private)
{
        struct vt_struct *vt = (struct vt_struct *) private;

        if (!vt || !vt->want_vc || !vt->want_vc->vc_tty) return;

        acquire_console_sem(vt->want_vc->vc_tty->device);

        if (vt->want_vc->vc_num != vt->fg_console->vc_num &&
            !vt->vt_dont_switch) {
                hide_cursor(vt->fg_console);
                /* New console, old console */
                change_console(vt->want_vc, vt->fg_console);
                /* we only changed when the console had already
                   been allocated - a new console is not created
                   in an interrupt routine */
        }
        /* do not unblank for a LED change */
        poke_blanked_console(vt);

        if (vt->scrollback_delta) {
                struct vc_data *vc = vt->fg_console;
                clear_selection();
                if (vcmode == KD_TEXT)
                      sw->con_scroll(vt->fg_console, vt->scrollback_delta);
                vt->scrollback_delta = 0;
        }
        release_console_sem(vt->want_vc->vc_tty->device);
}

/*
 *      Allocation, freeing and resizing of VTs.
 */
static void visual_init(struct vc_data *vc)
{
    /* ++Geert: sw->con_startup determines console size */
    vc->vc_uni_pagedir_loc = &vc->vc_uni_pagedir;
    vc->vc_uni_pagedir = 0;
    hi_font_mask = 0;
    complement_mask = 0;
    scrollback = 0;
    can_do_color = vc->display_fg->default_mode->vc_can_do_color;
    video_num_columns = vc->display_fg->default_mode->vc_cols;
    video_num_lines = vc->display_fg->default_mode->vc_rows;
    screensize = video_num_columns * video_num_lines;
    vc->vc_font = vc->display_fg->default_mode->vc_font;	
    sw->con_init(vc);
    if (!complement_mask)
        complement_mask = can_do_color ? 0x7700 : 0x0800;
    s_complement_mask = complement_mask;
    if (!scrollback)	
	scrollback = 1;
    video_size_row = video_num_columns<<1;
    screenbuf_size = scrollback*video_num_lines*video_size_row;
}

void vc_init(struct vc_data *vc, int do_clear)
{
        set_origin(vc);
        pos = origin;
        reset_vc(vc);
        def_color       = 0x07;   /* white */
        ulcolor         = 0x0f;   /* bold white */
        halfcolor       = 0x08;   /* grey */
        init_waitqueue_head(&vc->paste_wait);
        vte_ris(vc, do_clear);
}

struct vc_data* find_vc(int currcons)
{
	struct vt_struct *vt;

	for (vt = vt_cons; vt != NULL; vt = vt->next) {
               	if (currcons < vt->first_vc + MAX_NR_USER_CONSOLES &&
                            currcons >= vt->first_vc) 
        		return vt->vc_cons[currcons - vt->first_vc];
	}
	return NULL;
}

/* return a vc on success */
struct vc_data* vc_allocate(unsigned int currcons)  
{
	struct vc_data *vc = NULL;
	struct vt_struct *vt;

	/* prevent users from taking too much memory */
        if (currcons >= MAX_NR_CONSOLES && !capable(CAP_SYS_RESOURCE)) {
                currcons = -EPERM;
                return NULL;
        }
	
	for (vt = vt_cons; vt != NULL; vt = vt->next) { 
               	if (currcons < vt->first_vc + MAX_NR_USER_CONSOLES &&
                            currcons >= vt->first_vc) 
                                goto found_pool;
	}
	currcons = -ENXIO;
	return NULL;	
found_pool:
        /* due to the granularity of kmalloc, we waste some memory here */
        /* the alloc is done in two steps, to optimize the common situation
           of a 25x80 console (structsize=216, screenbuf_size=4000) */
        /* although the numbers above are not valid since long ago, the
           point is still up-to-date and the comment still has its value
           even if only as a historical artifact.  --mj, July 1998 */

	if (vt->kmalloced || !(vt->first_vc == currcons))
		vc = (struct vc_data *) kmalloc(sizeof(struct vc_data), GFP_KERNEL);
	else
		vc = (struct vc_data *) alloc_bootmem(sizeof(struct vc_data));
	
	if (!vc) {
		currcons = -ENOMEM; 
               	return NULL;
	}
 	vc->vc_num = currcons;	
        vc->display_fg = vt;
        visual_init(vc);
        if (vt->kmalloced || !(vt->first_vc == currcons)) { 
        	screenbuf = (unsigned short *) kmalloc(screenbuf_size, GFP_KERNEL);
		if (!screenbuf) {
			kfree(vc);
			currcons = -ENOMEM;
			return NULL;
		}
        	if (!*vc->vc_uni_pagedir_loc)
        		con_set_default_unimap(vc);
	} else {
		screenbuf = (unsigned short *) alloc_bootmem(screenbuf_size);
		if (!screenbuf) {
			free_bootmem((unsigned long) vc,sizeof(struct vc_data));
			currcons = -ENOMEM;
			return NULL;
		}			
	}
	vt->vc_cons[currcons - vt->first_vc] = vc;		
	if (vt->first_vc == currcons)
		vt->want_vc= vt->fg_console= vt->last_console = vc;
        vc_init(vc, 1);
        return vc;
}

int vc_disallocate(struct vc_data *vc)
{
	struct vt_struct *vt = vc->display_fg;

	if (vc) {
        	sw->con_deinit(vc);
		vt->vc_cons[cons_num - vt->first_vc] = NULL;
        	if (vt->kmalloced || !(vt->first_vc == cons_num)) { 
                	kfree(screenbuf);
            		kfree(vc);
	    	} else {
			free_bootmem((unsigned long) screenbuf, screenbuf_size);
			free_bootmem((unsigned long) vc,sizeof(struct vc_data));
	    	}
        }
	return 0;
}                     

/*
 * Change # of rows and columns (0 means unchanged/the size of visible VC)
 */
int vc_resize(struct vc_data *vc, unsigned int cols, unsigned int lines)
{
	unsigned long new_scr_end, new_origin, new_screensize, rlth, rrem;
	unsigned long old_screenbuf = 0, old_scr_end = 0;
	unsigned int old_cols, old_rows, old_screenbuf_size, old_row_size;
	unsigned short *newscreen = NULL;
        unsigned int new_cols, new_rows, new_screenbuf_size, new_row_size;
	int err = 0;

	if (!vc) return 0;

  	new_cols = (cols ? cols : video_num_columns);
        new_rows = (lines ? lines : video_num_lines);
        new_row_size = new_cols << 1;
        new_screenbuf_size = new_row_size * new_rows;

	if (new_cols == video_num_columns && new_rows == video_num_lines)
                return 0;
	
	old_rows = video_num_lines;
        old_cols = video_num_columns;
        old_row_size = video_size_row;
        old_screenbuf_size = screenbuf_size;
	old_screenbuf = (unsigned long) screenbuf;	

	err = resize_screen(vc, new_cols*vc->vc_font.width, new_rows*vc->vc_font.height);    
	if (err) {
		resize_screen(vc, old_cols*vc->vc_font.width, old_rows*vc->vc_font.height); 
		return err;    
	}

	/* scrollback could have been changed by resize_screen */
	newscreen = (unsigned short *) kmalloc(new_screenbuf_size, GFP_USER);
        if (!newscreen) {
		resize_screen(vc, old_cols*vc->vc_font.width, old_rows*vc->vc_font.height); 
        	return -ENOMEM;
	}
	
        rlth = MIN(old_row_size, new_row_size);
        rrem = new_row_size - rlth;
        new_scr_end = scr_end = (long) newscreen + new_screenbuf_size;
	new_screensize = new_rows*new_cols;
        new_origin = (long) (new_scr_end - 2*new_screensize);

        update_attr(vc);

	while (old_screenbuf < old_scr_end) {
		old_scr_end -= old_row_size;
		new_scr_end -= new_row_size;
        	scr_memcpyw((unsigned short *) new_scr_end, 
			    (unsigned short *) old_scr_end, rlth);
                if (rrem)
                	scr_memsetw((void *)(new_scr_end + rlth),video_erase_char,rrem);
	}

        if (new_screenbuf_size > old_screenbuf_size)
        	scr_memsetw((void *) newscreen, video_erase_char, new_screenbuf_size - old_screenbuf_size);
      
	/* 
	if (vc->display_fg->kmalloced)
	*/        
	kfree(screenbuf);
        screenbuf_size = new_screenbuf_size * scrollback;
	screensize = new_screensize;
        screenbuf = newscreen;
        //vc->display_fg->kmalloced = 1;
	video_num_lines = new_rows;
        video_num_columns = new_cols;
        video_size_row = new_row_size;
	origin = new_origin;
	scr_end = new_scr_end;
 
        set_origin(vc);
        /* do part of a vte_ris() */
        top = 0;
        bottom = video_num_lines;
        gotoxy(vc, x, y);
        vte_decsc(vc);

        if (vc->vc_tty) {
        	struct winsize ws, *cws = &vc->vc_tty->winsize;
                memset(&ws, 0, sizeof(ws));
                ws.ws_row = video_num_lines;
                ws.ws_col = video_num_columns;
                if ((ws.ws_row != cws->ws_row || ws.ws_col != cws->ws_col) &&
                     vc->vc_tty->pgrp > 0)
                        kill_pg(vc->vc_tty->pgrp, SIGWINCH, 1);
                *cws = ws;
	}

        if (IS_VISIBLE)
        	update_screen(vc);
        return 0;
}

/*
 * Selection stuff for GPM.
 */
void mouse_report(struct tty_struct *tty, int butt, int mrx, int mry)
{
        char buf[8];

        sprintf(buf, "\033[M%c%c%c", (char)(' ' + butt), (char)('!' + mrx),
                (char)('!' + mry));
        respond_string(buf, tty);
}

/* invoked via ioctl(TIOCLINUX) and through set_selection */
int mouse_reporting(struct tty_struct *tty)
{
        struct vc_data *vc = (struct vc_data *) tty->driver_data;

        return report_mouse;
}

/*
 * tty driver funtions except do_con_write which is a helper function. 
 */
#define CON_BUF_SIZE    PAGE_SIZE

static int do_con_write(struct tty_struct * tty, int from_user,
                        const unsigned char *buf, int count)
{
        struct vc_data *vc = (struct vc_data *)tty->driver_data;
	unsigned long draw_from = 0, draw_to = 0;	
	const unsigned char *orig_buf = NULL;
	int c, tc, ok, n = 0, draw_x = -1;
        unsigned short *translation;
	u16 himask, charmask;
        int orig_count;

        if (!vc) {
            /* could this happen? */
            static int error;
            if (!error) {
                error = 1;
                printk("con_write: tty %d not allocated\n", minor(tty->device));
            }
            return 0;
        }
	translation = get_acm(translate);
        orig_buf = buf;
        orig_count = count;

        if (from_user) {
                down(&vc->display_fg->lock);
again:
                if (count > CON_BUF_SIZE)
                        count = CON_BUF_SIZE;
                if (copy_from_user(&vc->display_fg->con_buf, buf, count)) {
                        n = 0; /* ?? are error codes legal here ?? */
                        goto out;
                }
                buf = vc->display_fg->con_buf;
        }

        /* At this point 'buf' is guarenteed to be a kernel buffer
         * and therefore no access to userspace (and therefore sleeping)
         * will be needed.  The con_buf_sem serializes all tty based
         * console rendering and vcs write/read operations.  We hold
         * the console spinlock during the entire write.
         */
        himask = hi_font_mask;
        charmask = himask ? 0x1ff : 0xff;

        /* undraw cursor first */
        if (IS_VISIBLE)
                hide_cursor(vc);

        while (!tty->stopped && count) {
                c = *buf;
                buf++;
                n++;
                count--;

                if (utf) {
                    /* Combine UTF-8 into Unicode */
                    /* Incomplete characters silently ignored */
                    if(c > 0x7f) {
                        if (utf_count > 0 && (c & 0xc0) == 0x80) {
                                utf_char = (utf_char << 6) | (c & 0x3f);
                                utf_count--;
                                if (utf_count == 0)
                                    tc = c = utf_char;
                                else continue;
                        } else {
                                if ((c & 0xe0) == 0xc0) {
                                    utf_count = 1;
                                    utf_char = (c & 0x1f);
                                } else if ((c & 0xf0) == 0xe0) {
                                    utf_count = 2;
                                    utf_char = (c & 0x0f);
                                } else if ((c & 0xf8) == 0xf0) {
                                    utf_count = 3;
                                    utf_char = (c & 0x07);
                                } else if ((c & 0xfc) == 0xf8) {
                                    utf_count = 4;
                                    utf_char = (c & 0x03);
                                } else if ((c & 0xfe) == 0xfc) {
                                    utf_count = 5;
                                    utf_char = (c & 0x01);
                                } else
                                    utf_count = 0;
                                continue;
                              }
                    } else {
                      tc = c;
                      utf_count = 0;
                    }
                } else {        /* no utf */
                  tc = translation[toggle_meta ? (c|0x80) : c];
                }

                /* If the original code was a control character we
                 * only allow a glyph to be displayed if the code is
                 * not normally used (such as for cursor movement) or
                 * if the disp_ctrl mode has been explicitly enabled.
                 * Certain characters (as given by the CTRL_ALWAYS
                 * bitmap) are always displayed as control characters,
                 * as the console would be pretty useless without
                 * them; to display an arbitrary font position use the
                 * direct-to-font zone in UTF-8 mode.
                 */
                ok = tc && (c >= 32 ||
                            (!utf && !(((disp_ctrl ? CTRL_ALWAYS
                                         : CTRL_ACTION) >> c) & 1)))
                        && (c != 127 || disp_ctrl)
                        && (c != 128+27);

		/* See if vc_state is ESinit or equivalent */
                if (vc_state == 0 && ok) {
                        /* Now try to find out how to display it */
                        tc = conv_uni_to_pc(vc, tc);
                        if ( tc == -4 ) {
                                /* If we got -4 (not found) then see if we have
                                   defined a replacement character (U+FFFD) */
                                tc = conv_uni_to_pc(vc, 0xfffd);

                                /* One reason for the -4 can be that we just
                                   did a clear_unimap();
                                   try at least to show something. */
                                if (tc == -4)
                                     tc = c;
                        } else if ( tc == -3 ) {
                                /* Bad hash table -- hope for the best */
                                tc = c;
                        }
                        if (tc & ~charmask)
                                continue; /* Conversion failed */

                        if ((need_wrap || irm) && IS_VISIBLE && sw->con_putc 
			     && draw_x >= 0) {
        			sw->con_putcs(vc, (u16 *)draw_from, 
					      (u16 *)draw_to-(u16 *)draw_from,
					      y, draw_x);
				draw_x = -1;
			}
                        if (need_wrap) {
                                vte_cr(vc);
                                vte_lf(vc);
                        }
                        if (irm)
                                insert_char(vc, 1);
                        scr_writew(himask ?
                                     ((attr << 8) & ~himask) + ((tc & 0x100) ? himask : 0) + (tc & 0xff) :
                                     (attr << 8) + tc,
                                   (u16 *) pos);
                        if (IS_VISIBLE && sw->con_putc && draw_x < 0) {
                                draw_x = x;
                                draw_from = pos;
                        }
                        if (x == video_num_columns - 1) {
                                need_wrap = decawm;
                                draw_to = pos+2;
                        } else {
                                x++;
                                draw_to = (pos+=2);
                        }
                        continue;
                }
		if (sw->con_putcs && draw_x >= 0) {
        		sw->con_putcs(vc, (u16 *)draw_from, 
				      (u16 *)draw_to-(u16 *)draw_from, y,
				      draw_x);
        		draw_x = -1;
        	}
                terminal_emulation(tty, c);
        }
	if (sw->con_putcs && draw_x >= 0) {
        	sw->con_putcs(vc, (u16 *)draw_from, 
			      (u16 *)draw_to-(u16 *)draw_from, y, draw_x);
        	draw_x = -1;
        }
out:
        if (from_user) {
                /* If the user requested something larger than
                 * the CON_BUF_SIZE, and the tty is not stopped,
                 * keep going.
                 */
                if ((orig_count > CON_BUF_SIZE) && !tty->stopped) {
                        orig_count -= CON_BUF_SIZE;
                        orig_buf += CON_BUF_SIZE;
                        count = orig_count;
                        buf = orig_buf;
                        goto again;
                }

                up(&vc->display_fg->lock);
        }
        return n;
}

/*
 *      /dev/ttyN handling
 */

/* Allocate the console screen memory. */
static int vt_open(struct tty_struct *tty, struct file * filp)
{
        unsigned int currcons = minor(tty->device);
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (!vc) {
		vc = find_vc(currcons);
		if (!vc) {
        		vc = vc_allocate(currcons);
			if (!vc) return currcons;               
		}
        	tty->driver_data = vc;
		vc->vc_tty = tty;	
	
		if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
                        tty->winsize.ws_row = video_num_lines;
                        tty->winsize.ws_col = video_num_columns;
 	       }
	} 

        if (tty->count == 1)
                vcs_make_devfs(currcons, 0);
        return 0;
}

static void vt_close(struct tty_struct *tty, struct file * filp)
{
        if (!tty)
                return;
        if (tty->count != 1) return;
        vcs_make_devfs(minor(tty->device), 1);
        tty->driver_data = 0;
}

static int vt_write(struct tty_struct * tty, int from_user,
                    const unsigned char *buf, int count)
{
        struct vc_data *vc = (struct vc_data *) tty->driver_data;
	int retval;

        pm_access(vc->display_fg->pm_con);
        retval = do_con_write(tty, from_user, buf, count);
	acquire_console_sem(vc->vc_tty->device);
        set_cursor(vc);
	release_console_sem(vc->vc_tty->device);
        return retval;
}

static void vt_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

        pm_access(vc->display_fg->pm_con);
        do_con_write(tty, 0, &ch, 1);
}

static int vt_write_room(struct tty_struct *tty)
{
        if (tty->stopped)
                return 0;
        return 4096;            /* No limit, really; we're not buffering */
}

static void vt_flush_chars(struct tty_struct *tty)
{
        struct vc_data *vc = (struct vc_data *)tty->driver_data;

	if (in_interrupt())     /* from flush_to_ldisc */
		return;

        pm_access(vc->display_fg->pm_con);
	acquire_console_sem(vc->vc_tty->device);
        set_cursor(vc);
	release_console_sem(vc->vc_tty->device);
}

static int vt_chars_in_buffer(struct tty_struct *tty)
{
        return 0;               /* we're not buffering */
}

/*
 * Turn the Scroll-Lock LED on when the tty is stopped
 */
static void vt_stop(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
        
	if (!tty || !vc)
                return;
        set_kbd_led(&vc->kbd_table, VC_SCROLLOCK);
        set_leds();
}

/*
 * Turn the Scroll-Lock LED off when the console is started
 */
static void vt_start(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
        
	if (!tty || !vc)
                return;
        clr_kbd_led(&vc->kbd_table, VC_SCROLLOCK);
        set_leds();
}

/*
 * con_throttle and con_unthrottle are only used for
 * paste_selection(), which has to stuff in a large number of
 * characters...
 */
static void vt_throttle(struct tty_struct *tty)
{
}

static void vt_unthrottle(struct tty_struct *tty)
{
        struct vc_data *vc = (struct vc_data *) tty->driver_data;

        wake_up_interruptible(&vc->paste_wait);
}

#ifdef CONFIG_VT_CONSOLE
/*
 *      Console on virtual terminal
 *
 * The console_lock must be held when we get here.
 */

void vt_console_print(struct console *co, const char * b, unsigned count)
{
        struct vc_data *vc = admin_vt->fg_console;
	static unsigned long printing;
	const ushort *start;
     	ushort myx, cnt = 0; 
        unsigned char c;

        /* console busy or not yet initialized */
        if (!printable || test_and_set_bit(0, &printing))
                return;

        if (kmsg_redirect) {
		vc = find_vc(kmsg_redirect-1);
		if (vc)
			admin_vt = vc->display_fg;
		else
			/* Should we allocate a VC instead ? */
			goto quit;
	}

	pm_access(vc->display_fg->pm_con);
	
        /* read `x' only after setting co->index properly (otherwise
           the `x' macro will read the x of the foreground console). */
        myx = x;

        if (vcmode != KD_TEXT)
                goto quit;

        /* undraw cursor first */
        if (IS_VISIBLE)
                hide_cursor(vc);

        start = (ushort *)pos;

        /* Contrived structure to try to emulate original need_wrap behaviour
         * Problems caused when we have need_wrap set on '\n' character */
        while (count--) {
                c = *b++;
                if (c == 10 || c == 13 || c == 8 || need_wrap) {
                        if (cnt > 0) {
                                if (IS_VISIBLE)
                                        sw->con_putcs(vc, start, cnt, y, x);
                                x += cnt;
                                if (need_wrap)
                                        x--;
                                cnt = 0;
                        }
                        if (c == 8) {           /* backspace */
                                vte_bs(vc);
                                start = (ushort *)pos;
                                myx = x;
                                continue;
                        }
                        if (c != 13)
                                vte_lf(vc);
                        vte_cr(vc);
                        start = (ushort *)pos;
                        myx = x;
                        if (c == 10 || c == 13)
                                continue;
                }
                scr_writew((attr << 8) + c, (unsigned short *) pos);
                cnt++;
                if (myx == video_num_columns - 1) {
                        need_wrap = 1;
                        continue;
                }
                pos+=2;
                myx++;
        }
        if (cnt > 0) {
                if (IS_VISIBLE)
                        sw->con_putcs(vc, start, cnt, y, x);
                x += cnt;
                if (x == video_num_columns) {
                        x--;
                        need_wrap = 1;
                }
        }
	/* I think this is wrong. Only should happen when visible? */
        set_cursor(vc);
	if (!oops_in_progress)
        	poke_blanked_console(vc->display_fg);
quit:
        clear_bit(0, &printing);
}

static kdev_t vt_console_device(struct console *c)
{
        return mk_kdev(TTY_MAJOR, c->index ? c->index : admin_vt->fg_console->vc_num);
}

struct console vt_console_driver = {
	name:		"tty",
        write:		vt_console_print,
        device:		vt_console_device,
        flags:		CON_PRINTBUFFER,
        index:		-1,
};
#endif

/*
 *      Handling of Linux-specific VC ioctls
 */
int tioclinux(struct tty_struct *tty, unsigned long arg)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
        char type, data;

        if (tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
                return -EINVAL;
        if (current->tty != tty && !capable(CAP_SYS_ADMIN))
                return -EPERM;
        if (get_user(type, (char *)arg))
                return -EFAULT;
        switch (type)
        {
                case 2:
                        return set_selection(arg, tty, 1);
                case 3:
                        return paste_selection(tty);
                case 4:
                        unblank_screen(vc->display_fg);
                        return 0;
                case 5:
                        return sel_loadlut(arg);
                case 6:

        /*
         * Make it possible to react to Shift+Mousebutton.
         * Note that 'shift_state' is an undocumented
         * kernel-internal variable; programs not closely
         * related to the kernel should not use this.
         */
                        data = shift_state;
                        return __put_user(data, (char *) arg);
                case 7:
                        data = mouse_reporting(tty);
                        return __put_user(data, (char *) arg);
                case 10:
    			if (get_user(data, (char *)arg+1))
				return -EFAULT;
    			vc->display_fg->blank_mode = (data < 4) ? data : 0;
			return 0;
                case 11:        /* set kmsg redirect */
#ifdef CONFIG_VT_CONSOLE
                        if (!capable(CAP_SYS_ADMIN))
                                return -EPERM;
                        if (get_user(data, (char *)arg+1))
                                        return -EFAULT;
                        kmsg_redirect = data;
                        return 0;
#else
			retrun -EINVAL;
#endif
                case 12:        /* get fg_console */
                        return vc->display_fg->fg_console->vc_num; 
        }
        return -EINVAL;
}

/*
 * Mapping and unmapping devices to a VT  
 */
const char *vt_map_display(struct vt_struct *vt, int init)
{
	const char *display_desc = vt->vt_sw->con_startup(vt, init);

	if (!display_desc) return NULL;	

	/* Now to setup the VT */
	vt->first_vc = current_vc;
	init_MUTEX(&vt->lock);
	vt->first_vc = current_vc;
	vt->next = vt_cons;
	vt_cons = vt;
	vt->vt_dont_switch = 0;
        vt->scrollback_delta = 0;
        vt->vt_blanked = 0;
        vt->blank_interval = 10*60*HZ;
        vt->off_interval = 0;
	if (vt->pm_con)
		vt->pm_con->data = vt;
	vt->default_mode->display_fg = vt;
	vt->vc_cons[0] = vc_allocate(current_vc);
	vt->keyboard = NULL;

        init_timer(&vt->timer);
        vt->timer.data = (long) vt;
        vt->timer.function = blank_screen;
        mod_timer(&vt->timer, jiffies + vt->blank_interval);
	INIT_TQUEUE(&vt->vt_tq, vt_callback, vt);
	if (!admin_vt) {
		struct vc_data *vc = vt->vc_cons[0];		

		admin_vt = vt;
#ifdef CONFIG_VT_CONSOLE
		register_console(&vt_console_driver);
        	printable = 1;
#endif
                gotoxy(vc, x, y);
                vte_ed(vt->vc_cons[0], 0);
                update_screen(vt->vc_cons[0]);
	}
	current_vc += MAX_NR_USER_CONSOLES;
	return display_desc;
}

/* 
 * This is called when we have detected a keyboard and have a VT lacking one 
 */
void vt_map_input(struct vt_struct *vt)
{
	struct tty_driver *vty_driver;

	vty_driver = kmalloc(sizeof(struct tty_driver), GFP_KERNEL);
	memset(vty_driver, 0, sizeof(struct tty_driver));

	vty_driver->refcount = kmalloc(sizeof(int), GFP_KERNEL);
	vty_driver->table = kmalloc(sizeof(struct tty_struct) * MAX_NR_USER_CONSOLES, GFP_KERNEL);
	vty_driver->termios = kmalloc(sizeof(struct termios) * MAX_NR_USER_CONSOLES, GFP_KERNEL);
	vty_driver->termios_locked = kmalloc(sizeof(struct termios) * MAX_NR_USER_CONSOLES, GFP_KERNEL);
	
	vty_driver->magic = TTY_DRIVER_MAGIC;
	vty_driver->name = "vc/%d";
	vty_driver->name_base = vt->first_vc;
	vty_driver->major = TTY_MAJOR;
	vty_driver->minor_start = vt->first_vc;
	vty_driver->num = MAX_NR_USER_CONSOLES;
	vty_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	vty_driver->init_termios = tty_std_termios;
	vty_driver->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
#ifdef CONFIG_VT_CONSOLE
	if (admin_vt == vt)
		vty_driver->console = &vt_console_driver;
#endif
	vty_driver->open = vt_open;
	vty_driver->close = vt_close;
	vty_driver->write = vt_write;
	vty_driver->write_room = vt_write_room;
	vty_driver->put_char = vt_put_char;
	vty_driver->flush_chars = vt_flush_chars;
	vty_driver->chars_in_buffer = vt_chars_in_buffer;
	vty_driver->ioctl = vt_ioctl;
	vty_driver->stop = vt_stop;
	vty_driver->start = vt_start;
	vty_driver->throttle = vt_throttle;
	vty_driver->unthrottle = vt_unthrottle;
	if (tty_register_driver(vty_driver))
		printk("Couldn't register console driver\n");
}

int release_vt(struct vt_struct *vt)
{
	return 0;
}

/*
 * This routine initializes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequence.
 */
void __init vt_console_init(void)
{
#if defined(CONFIG_VGA_CONSOLE)
	vga_console_init();
#endif
#if defined(CONFIG_MDA_CONSOLE)
	mda_console_init();
#endif
}

int __init vty_init(void)
{
#if defined (CONFIG_PROM_CONSOLE)
	prom_con_init();
#endif
#if defined (CONFIG_FRAMEBUFFER_CONSOLE)
	fb_console_init();
#endif
	kbd_init();
	console_map_init();
	vcs_init();
	return 0;
}

static void clear_buffer_attributes(struct vc_data *vc)
{
        unsigned short *p = (unsigned short *) origin;
        int count = screenbuf_size/2;
        int mask = hi_font_mask | 0xff;

        for (; count > 0; count--, p++) {
                scr_writew((scr_readw(p)&mask) | (video_erase_char&~mask), p);
        }
}

/*
 *      If we support more console drivers, this function is used
 *      when a driver wants to take over some existing consoles
 *      and become default driver for newly opened ones.
 */
void take_over_console(struct vt_struct *vt, const struct consw *csw)
{
	struct vc_data *vc = vt->fg_console;
	const char *desc;
	int i;

	/* First shutdown old console driver */
	hide_cursor(vc);
	
	for (i = 0; i < MAX_NR_USER_CONSOLES; i++) {
		vc = vt->vc_cons[i];
		if (vc) sw->con_deinit(vc);
	}

	/* Test new hardware state */
        desc = csw->con_startup(vt, 0);
        if (!desc) {
		/* Make sure the original driver state is restored to normal */
		vt->vt_sw->con_startup(vt, 1);
		return;
	}
	vt->vt_sw = csw;

	/* Set the VC states to the new default mode */
        for (i = 0; i < MAX_NR_USER_CONSOLES; i++) {
                int old_was_color;
		vc = vt->vc_cons[i];

                if (vc) {
                	old_was_color = vc->vc_can_do_color;
			cons_num = vt->first_vc + i;
			vc_resize(vc, vt->default_mode->vc_cols, vt->default_mode->vc_rows);
			visual_init(vc);
	        	update_attr(vc);

                	/* If the console changed between mono <-> color, then
                 	 * the attributes in the screenbuf will be wrong.  The
                 	 * following resets all attributes to something sane.
                 	 */
                	if (old_was_color != vc->vc_can_do_color) 
                        	clear_buffer_attributes(vc);
        	}
	}
	vc = vt->fg_console;
	update_screen(vc);

       	printk("Console: switching to %s %s %dx%d\n", 
               	vc->vc_can_do_color ? "colour" : "mono",
               	desc, vc->vc_cols, vc->vc_rows);
}

/*
 *      Visible symbols for modules
 */

EXPORT_SYMBOL(color_table);
EXPORT_SYMBOL(default_red);
EXPORT_SYMBOL(default_grn);
EXPORT_SYMBOL(default_blu);
EXPORT_SYMBOL(vt_map_display);
EXPORT_SYMBOL(release_vt);
EXPORT_SYMBOL(vc_resize);
EXPORT_SYMBOL(vc_init);
EXPORT_SYMBOL(console_blank_hook);
EXPORT_SYMBOL(take_over_console);
