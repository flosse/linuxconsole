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
#include <linux/kd.h>
#include <linux/malloc.h>
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

#include <asm/linux_logo.h>

#include "console_macros.h"

struct consw *conswitchp = NULL;

/* A bitmap for codes <32. A bit of 1 indicates that the code
 * corresponding to that bit number invokes some special action
 * (such as cursor movement) and should not be displayed as a
 * glyph unless the disp_ctrl mode is explicitly enabled.
 */
#define CTRL_ACTION 0x0d00ff81
#define CTRL_ALWAYS 0x0800f501  /* Cannot be overridden by disp_ctrl */

extern void vcs_make_devfs (unsigned int index, int unregister);

#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif

static struct tty_struct *console_table[MAX_NR_CONSOLES];
static struct termios *console_termios[MAX_NR_CONSOLES];
static struct termios *console_termios_locked[MAX_NR_CONSOLES];

static void con_flush_chars(struct tty_struct *tty);

static int printable = 0;               /* Is console ready for printing? */
static int current_vc = 0;		/* Which /dev/vc/X to allocate next */
struct vt_struct *admin_vt = NULL;	/* VT of /dev/console */

/*
 * want_vc is the virtual console we want to switch to,
 * kmsg_redirect is the virtual console for kernel messages,
 */
struct vc_data *want_vc = NULL;
int kmsg_redirect = 0;

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
int (*console_blank_hook)(int) = NULL;

/* keyboard macros */
#define set_kbd(kbd_table, x) set_vc_kbd_mode(kbd_table, x)
#define clr_kbd(kbd_table, x) clr_vc_kbd_mode(kbd_table, x)
#define is_kbd(kbd_table, x) vc_kbd_mode(kbd_table, x)

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
        if (DO_UPDATE)
                sw->con_putc(vc, i, y, x);
}

void hide_cursor(struct vc_data *vc)
{
	spin_lock_irq(&console_lock);
        if (cons_num == sel_cons)
                clear_selection();
        if (softcursor_original != -1) {
                scr_writew(softcursor_original,(u16 *) pos);
                if (DO_UPDATE)
			sw->con_putc(vc, softcursor_original, y, x);
                softcursor_original = -1;
        }
        sw->con_cursor(vc, CM_ERASE);
	spin_unlock_irq(&console_lock);
}

void set_cursor(struct vc_data *vc)
{
    if (!IS_VISIBLE || vc->display_fg->vt_blanked || vcmode == KD_GRAPHICS)
        return;
    spin_lock_irq(&console_lock); 		
    if (dectcem) {
        if (cons_num == sel_cons)
                clear_selection();
        add_softcursor(vc);
        if ((cursor_type & 0x0f) != 1)
            sw->con_cursor(vc, CM_DRAW);
    } else
        hide_cursor(vc);
    spin_unlock_irq(&console_lock);	
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
        if (vcmode != KD_GRAPHICS)
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
static inline void scrolldelta(struct vc_data *vc, int lines)
{
        vc->display_fg->scrollback_delta += lines;
	want_vc = vc;
	tasklet_schedule(&console_tasklet);
}

void scrollback(struct vc_data *vc, int lines)
{
        if (!lines)
                lines = video_num_lines/2;
        scrolldelta(vc, -lines);
}

void scrollfront(struct vc_data *vc, int lines)
{
        if (!lines)
                lines = video_num_lines/2;
        scrolldelta(vc, lines);
}

void scrup(struct vc_data *vc, unsigned int t, unsigned int b, int nr)
{
        unsigned short *d, *s;

        if (t+nr >= b)
                nr = b - t;
        if (b > video_num_lines || t >= b || nr < 1)
                return;
        if (IS_VISIBLE && sw->con_scroll(vc, t, b, SM_UP, nr))
                return;
        d = (unsigned short *) (origin+video_size_row*t);
        s = (unsigned short *) (origin+video_size_row*(t+nr));
        scr_memcpyw(d, s, (b-t-nr) * video_size_row);
        scr_memsetw(d + (b-t-nr) * video_num_columns, video_erase_char, video_size_row*nr);
}

void scrdown(struct vc_data *vc, unsigned int t, unsigned int b, int nr)
{
        unsigned short *s;
        unsigned int step;

        if (t+nr >= b)
                nr = b - t;
        if (b > video_num_lines || t >= b || nr < 1)
                return;
        if (IS_VISIBLE && sw->con_scroll(vc, t, b, SM_DOWN, nr))
                return;
        s = (unsigned short *) (origin+video_size_row*t);
        step = video_num_columns * nr;
        scr_memmovew(s + step, s, (b-t-nr)*video_size_row);
        scr_memsetw(s, video_erase_char, 2*step);
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

static u8 build_attr(struct vc_data *vc, u8 _color, u8 _intensity, u8 _blink, u8 _underline, u8 _reverse)
{
        if (sw->con_build_attr)
                return sw->con_build_attr(vc, _color, _intensity, _blink, _underline, _reverse);

#ifndef VT_BUF_VRAM_ONLY
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
        {
        u8 a = color;
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
#else
        return 0;
#endif
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
        if (DO_UPDATE) {
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
        if (DO_UPDATE) {
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
        scrdown(vc, y, bottom, nr);
        need_wrap = 0;
}

void delete_line(struct vc_data *vc, unsigned int nr)
{
        scrup(vc, y, bottom, nr);
        need_wrap = 0;
}

/*
 * Functions that manage whats displayed on the screen
 */
void set_origin(struct vc_data *vc)
{
        if (!IS_VISIBLE || !sw->con_set_origin || !sw->con_set_origin(vc))
                origin = (unsigned long) screenbuf;
        visible_origin = origin;
        scr_end = origin + screenbuf_size;
        pos = origin + video_size_row*y + 2*x;
}

inline void save_screen(struct vc_data *vc)
{
        if (sw->con_save_screen)
                sw->con_save_screen(vc);
}

void do_update_region(struct vc_data *vc, unsigned long start, int count)
{
#ifndef VT_BUF_VRAM_ONLY
        unsigned int xx, yy, offset;
        u16 *p;

        p = (u16 *) start;
        if (!sw->con_getxy) {
                offset = (start - origin) / 2;
                xx = offset % video_num_columns;
                yy = offset / video_num_columns;
        } else {
                int nxx, nyy;
                start = sw->con_getxy(vc, start, &nxx, &nyy);
                xx = nxx; yy = nyy;
        }
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
                if (sw->con_getxy) {
                        p = (u16 *)start;
                        start = sw->con_getxy(vc, start, NULL, NULL);
                }
        }
#endif
}

void update_region(struct vc_data *vc, unsigned long start, int count)
{
        if (DO_UPDATE) {
                hide_cursor(vc);
                do_update_region(vc, start, count);
                set_cursor(vc);
        }
}

inline unsigned short *screenpos(struct vc_data *vc, int offset, int viewed)
{
        unsigned short *p;

        if (!viewed)
                p = (unsigned short *)(origin + offset);
        else if (!sw->con_screen_pos)
                p = (unsigned short *)(visible_origin + offset);
        else
                p = sw->con_screen_pos(vc, offset);
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
#ifndef VT_BUF_VRAM_ONLY
        else {
                u16 *q = p;
                int cnt = count;

                if (!can_do_color) {
                        while (cnt--) *q++ ^= 0x0800;
                } else if (hi_font_mask == 0x100) {
                        while (cnt--) {
                                u16 a = *q;
                                a = ((a) & 0x11ff) | (((a) & 0xe000) >> 4) | (((a) & 0x0e00) << 4);
                                *q++ = a;
                        }
                } else {
                        while (cnt--) {
                                u16 a = *q;
                                a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
                                *q++ = a;
                        }
                }
        }
#endif
        if (DO_UPDATE)
                do_update_region(vc, (unsigned long) p, count);
}

/* used by selection: complement pointer position */
void complement_pos(struct vc_data *vc, int offset)
{
        static unsigned short *p = NULL;
        static unsigned short old = 0;
        static unsigned short oldx = 0, oldy = 0;

        if (p) {
                scr_writew(old, p);
                if (DO_UPDATE)
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
                if (DO_UPDATE) {
                        oldx = (offset >> 1) % video_num_columns;
                        oldy = (offset >> 1) / video_num_columns;
                        sw->con_putc(vc, new, oldy, oldx);
                }
        }
}

/*      Redrawing of screen */
void update_screen(struct vc_data *vc)
{
	int update;

        hide_cursor(vc);
	set_origin(vc);
	update = sw->con_switch(vc);
        set_palette(vc);

        if (update && vcmode != KD_GRAPHICS) {
        	/* Update the screen contents */
        	do_update_region(vc, origin, screenbuf_size/2);
        }
        set_cursor(vc);
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
        if (vt->vc_mode != KD_TEXT) {
                vt->vt_blanked = 1;
                return;
        }

        hide_cursor(vt->fg_console);

        if (vt->off_interval) {
        	vt->timer.function = powerdown_screen;
        	vt->timer.expires = jiffies + vt->off_interval;
        	add_timer(&vt->timer);
        } 
        
	save_screen(vt->fg_console);
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
        if (vt->vc_mode == KD_GRAPHICS)
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
static int pm_con_request(struct pm_dev *dev, pm_request_t rqst, void *data)
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
 *      Allocation, freeing and resizing of VTs.
 */

const char *create_vt(struct vt_struct *vt, struct consw *vt_sw)
{
	const char *display_desc = NULL;

	if (vt_sw)
                display_desc = vt_sw->con_startup(vt);
        if (!display_desc) return NULL; 
	vt->vt_sw = vt_sw;
	vt->vt_dont_switch = 0;
        vt->scrollback_delta = 0;
        vt->vt_blanked = 0;
        vt->blank_interval = 10*60*HZ;
        vt->off_interval = 0;
	init_MUTEX(&vt->lock);
	vt->keyboard = NULL;
	vt->data_hook = NULL;
	vt->vcs.first_vc = current_vc;  
	vt->vcs.next = NULL;
        vt->next = vt_cons;
	vt_cons = vt;
	current_vc += MAX_NR_USER_CONSOLES;

        init_timer(&vt->timer);
        vt->timer.data = (long) vt;
        vt->timer.function = blank_screen;
        mod_timer(&vt->timer, jiffies + vt->blank_interval);
	return display_desc;
}

struct vc_data* find_vc(int currcons)
{
	struct vt_struct *vt = vt_cons;
	struct vc_pool *pool = &vt->vcs;

	for (;;) {
		if (currcons >= pool->first_vc + MAX_NR_USER_CONSOLES || 
	    	    currcons < pool->first_vc) 
			pool = pool->next;
		else
			break;	
		
		if (!pool) {
			vt = vt->next;
			if (vt)
				pool = &vt->vcs;
			else return NULL;
		}
	} 
	return pool->vc_cons[currcons - pool->first_vc];	
}

static void visual_init(struct vc_data *vc, int init)
{
    /* ++Geert: sw->con_init determines console size */
    vc->vc_uni_pagedir_loc = &vc->vc_uni_pagedir;
    vc->vc_uni_pagedir = 0;
    hi_font_mask = 0;
    complement_mask = 0;
    can_do_color = 0;
    sw->con_init(vc, init);
    if (!complement_mask)
        complement_mask = can_do_color ? 0x7700 : 0x0800;
    s_complement_mask = complement_mask;
    video_size_row = video_num_columns<<1;
    screenbuf_size = video_num_lines*video_size_row;
}

static void vc_init(struct vc_data *vc, int do_clear)
{
        int j, k ;

        set_origin(vc);
        pos = origin;
        reset_vc(vc);
        for (j=k=0; j<16; j++) {
                palette[k++] = default_red[j] ;
                palette[k++] = default_grn[j] ;
                palette[k++] = default_blu[j] ;
        }
        def_color       = 0x07;   /* white */
        ulcolor         = 0x0f;   /* bold white */
        halfcolor       = 0x08;   /* grey */
        init_waitqueue_head(&vc->paste_wait);
        vte_ris(vc, do_clear);
}

/* return 0 on success */
int vc_allocate(unsigned int currcons)  
{
	struct vt_struct *vt = vt_cons;
	struct vc_pool *pool = &vt->vcs;
	struct vc_data *vc;

	if (currcons >= MAX_NR_CONSOLES)
		return -ENXIO;

        for (;;) {
                if (currcons >= pool->first_vc + MAX_NR_USER_CONSOLES ||
                    currcons < pool->first_vc)
                        pool = pool->next;
                else
                        break;

                if (!pool) {
                        vt = vt->next;
                        if (vt)
                                pool = &vt->vcs;
                        else 
				return -ENXIO;
                }
        }
	vc = pool->vc_cons[currcons - pool->first_vc];

        if (!vc) {
            long p, q;

            /* prevent users from taking too much memory */
            if (currcons >= MAX_NR_CONSOLES && !capable(CAP_SYS_RESOURCE))
              return -EPERM;

            /* due to the granularity of kmalloc, we waste some memory here */
            /* the alloc is done in two steps, to optimize the common situation
               of a 25x80 console (structsize=216, screenbuf_size=4000) */
            /* although the numbers above are not valid since long ago, the
               point is still up-to-date and the comment still has its value
               even if only as a historical artifact.  --mj, July 1998 */
            p = (long) kmalloc(sizeof(struct vc_data), GFP_KERNEL);
            if (!p)
                return -ENOMEM;
            vc = (struct vc_data *)p;
	    vc->vc_num = currcons;	
            vc->display_fg = vt;
            visual_init(vc, 1);
            if (!*vc->vc_uni_pagedir_loc)
                con_set_default_unimap(vc);
            q = (long)kmalloc(screenbuf_size, GFP_KERNEL);
            if (!q) {
                kfree((char *) p);
                vc = NULL;
                return -ENOMEM;
            }
            screenbuf = (unsigned short *) q;
            vc_init(vc, 1);
	    if (currcons - pool->first_vc == 0)
	    	vt->last_console = vt->fg_console = vc;
	
	    pool->vc_cons[currcons - pool->first_vc] = vc;		
	 	
            if (!vt->pm_con) { 
            	vt->pm_con = pm_register(PM_SYS_DEV,PM_SYS_VGA,pm_con_request);	
		if (vt->pm_con)
			vt->pm_con->data = vt;			
	    }	
        }
        return 0;
}

void vc_disallocate(unsigned int currcons)
{
	struct vt_struct *vt = vt_cons;
        struct vc_pool *pool = &vt->vcs;
        struct vc_data *vc;

        if (currcons >= MAX_NR_CONSOLES)
                return;

        for (;;) {
                if (currcons >= pool->first_vc + MAX_NR_USER_CONSOLES ||
                    currcons < pool->first_vc)
                        pool = pool->next;
                else
                        break;

                if (!pool) {
                        vt = vt->next;
                        if (vt)
                                pool = &vt->vcs;
                        else
                                return;
                }
        }
	vc = pool->vc_cons[currcons - pool->first_vc];
        
	if (vc) {
            sw->con_deinit(vc);
            if (vc->display_fg->kmalloced)
                kfree(screenbuf);
            if (currcons >= MIN_NR_CONSOLES)
                kfree(vc);
            pool->vc_cons[currcons - pool->first_vc] = NULL;
        }
}                     

/*
 * Change # of rows and columns (0 means unchanged/the size of visible VC)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
int vc_resize(struct vc_data *vc, unsigned int lines, unsigned int cols)
{
	unsigned long ol, nl, nlend, rlth, rrem;
	unsigned int occ, oll, oss, osr;
	unsigned short *newscreens = NULL;
        unsigned int cc, ll, ss, sr;
	int err = 0;

        cc = (cols ? cols : video_num_columns);
        ll = (lines ? lines : video_num_lines);
        sr = cc << 1;
        ss = sr * ll;

        if (!vc || (cc == video_num_columns && ll == video_num_lines))
		return 0;       
	
	if (vc->display_fg->vt_sw->con_resize)
		err = vc->display_fg->vt_sw->con_resize(vc, ll, cc);    
	
	if (err) return err;    
	
	newscreens = (unsigned short *) kmalloc(ss, GFP_USER);
        if (!newscreens) 
        	return -ENOMEM;
       
	oll = video_num_lines;
        occ = video_num_columns;
        osr = video_size_row;
        oss = screenbuf_size;

        video_num_lines = ll;
        video_num_columns = cc;
        video_size_row = sr;
        screenbuf_size = ss;

        rlth = MIN(osr, sr);
        rrem = sr - rlth;
        ol = origin;
        nl = (long) newscreens;
        nlend = nl + ss;
        if (ll < oll)
        	ol += (oll - ll) * osr;

        update_attr(vc);

        while (ol < scr_end) {
        	scr_memcpyw((unsigned short *) nl, (unsigned short *) ol, rlth);
                if (rrem)
                	scr_memsetw((void *)(nl + rlth),video_erase_char,rrem);
                ol += osr;
                nl += sr;
        }
        if (nlend > nl)
        	scr_memsetw((void *) nl, video_erase_char, nlend - nl);
        
	/* 
	if (vc->display_fg->kmalloced)
        	kfree(screenbuf); 
	*/
        screenbuf = newscreens;
        /* vc->display_fg->kmalloced = 1; */
        screenbuf_size = ss;
        set_origin(vc);

        /* do part of a vte_ris() */
        top = 0;
        bottom = video_num_lines;
        gotoxy(vc, x, y);
        vte_decsc(vc);

        if (console_table[cons_num]) {
        	struct winsize ws, *cws = &console_table[cons_num]->winsize;
                memset(&ws, 0, sizeof(ws));
                ws.ws_row = video_num_lines;
                ws.ws_col = video_num_columns;
                if ((ws.ws_row != cws->ws_row || ws.ws_col != cws->ws_col) &&
                     console_table[cons_num]->pgrp > 0)
                        kill_pg(console_table[cons_num]->pgrp, SIGWINCH, 1);
                *cws = ws;
	}

        if (IS_VISIBLE)
        	update_screen(vc);
        return 0;
}

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

#define CON_BUF_SIZE    PAGE_SIZE

static int do_con_write(struct tty_struct * tty, int from_user,
                        const unsigned char *buf, int count)
{
#ifdef VT_BUF_VRAM_ONLY
#define FLUSH do { } while(0);
#else
#define FLUSH if (draw_x >= 0) { \
        sw->con_putcs(vc, (u16 *)draw_from, (u16 *)draw_to-(u16 *)draw_from, y, draw_x); \
        draw_x = -1; \
        }
#endif

        int c, tc, ok, n = 0, draw_x = -1;
        unsigned long draw_from = 0, draw_to = 0;
        struct vc_data *vc = (struct vc_data *)tty->driver_data;
        u16 himask, charmask;
        const unsigned char *orig_buf = NULL;
        int orig_count;

        if (!vc) {
            /* could this happen? */
            static int error = 0;
            if (!error) {
                error = 1;
                printk("con_write: tty %d not allocated\n", cons_num);
            }
            return 0;
        }

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

        spin_lock_irq(&console_lock);

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
                  tc = translate[toggle_meta ? (c|0x80) : c];
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

                        if (need_wrap || irm)
                                FLUSH
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
                        if (DO_UPDATE && draw_x < 0) {
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
                FLUSH
                terminal_emulation(tty, c);
        }
        FLUSH
        spin_unlock_irq(&console_lock);

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
#undef FLUSH
}

/*
 * This is the console switching tasklet.
 *
 * Doing console switching in a tasklet allows
 * us to do the switches asynchronously (needed when we want
 * to switch due to a keyboard interrupt).  Synchronization
 * with other console code and prevention of re-entrancy is
 * ensured with console_lock.
 */
static void console_softint(unsigned long ignored)
{
	if  (!want_vc)	return;

        spin_lock_irq(&console_lock);

	if (want_vc->vc_num != want_vc->display_fg->fg_console->vc_num &&
	    !want_vc->display_fg->vt_dont_switch) { 
		hide_cursor(want_vc->display_fg->fg_console);
		/* New console, old console */
                change_console(want_vc, want_vc->display_fg->fg_console);
                /* we only changed when the console had already
                   been allocated - a new console is not created
                   in an interrupt routine */
        }
	/* do not unblank for a LED change */
        poke_blanked_console(want_vc->display_fg);
	
	if (want_vc->display_fg->scrollback_delta) {
		struct vc_data *vc = want_vc->display_fg->fg_console;
                clear_selection();
                if (vcmode == KD_TEXT)
                      sw->con_scrolldelta(vc,vc->display_fg->scrollback_delta); 
                vc->display_fg->scrollback_delta = 0;
        }
        spin_unlock_irq(&console_lock);
}

/*
 *      Handling of Linux-specific VC ioctls
 */

int tioclinux(struct tty_struct *tty, unsigned long arg)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
        char type, data;

        if (tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
                return -EINVAL;
        if (current->tty != tty && !suser())
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
                        if (!suser())
                                return -EPERM;
                        if (get_user(data, (char *)arg+1))
                                        return -EFAULT;
                        kmsg_redirect = data;
                        return 0;
                case 12:        /* get fg_console */
                        return vc->display_fg->fg_console->vc_num; 
        }
        return -EINVAL;
}

/*
 *      /dev/ttyN handling
 */

/* Allocate the console screen memory. */
static int con_open(struct tty_struct *tty, struct file * filp)
{
        unsigned int currcons = MINOR(tty->device) - tty->driver.minor_start;
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	int i;

	if (!vc) {
        	i = vc_allocate(currcons);
		if (i)               
	 		return i;
		vc = find_vc(currcons);
        	tty->driver_data = vc;
		
		if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
                        tty->winsize.ws_row = video_num_lines;
                        tty->winsize.ws_col = video_num_columns;
 	       }
	} 

        if (tty->count == 1)
                vcs_make_devfs(currcons, 0);
        return 0;
}

static void con_close(struct tty_struct *tty, struct file * filp)
{
        if (!tty)
                return;
        if (tty->count != 1) return;
        vcs_make_devfs (MINOR (tty->device) - tty->driver.minor_start, 1);
        tty->driver_data = 0;
}

static int con_write(struct tty_struct * tty, int from_user,
                     const unsigned char *buf, int count)
{
        struct vc_data *vc = (struct vc_data *) tty->driver_data;
	int     retval;

        pm_access(vc->display_fg->pm_con);
        retval = do_con_write(tty, from_user, buf, count);
        con_flush_chars(tty);

        return retval;
}

static void con_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

        pm_access(vc->display_fg->pm_con);
        do_con_write(tty, 0, &ch, 1);
}

static int con_write_room(struct tty_struct *tty)
{
        if (tty->stopped)
                return 0;
        return 4096;            /* No limit, really; we're not buffering */
}

static void con_flush_chars(struct tty_struct *tty)
{
        struct vc_data *vc = (struct vc_data *)tty->driver_data;
        unsigned long flags;

        pm_access(vc->display_fg->pm_con);
        spin_lock_irqsave(&console_lock, flags);
        set_cursor(vc);
        spin_unlock_irqrestore(&console_lock, flags);
}

static int con_chars_in_buffer(struct tty_struct *tty)
{
        return 0;               /* we're not buffering */
}

/*
 * Turn the Scroll-Lock LED on when the tty is stopped
 */
static void con_stop(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
        
	if (!tty || !vc)
                return;
        set_vc_kbd_led(&vc->kbd_table, VC_SCROLLOCK);
        set_leds();
}

/*
 * Turn the Scroll-Lock LED off when the console is started
 */
static void con_start(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
        
	if (!tty || !vc)
                return;
        clr_vc_kbd_led(&vc->kbd_table, VC_SCROLLOCK);
        set_leds();
}

/*
 * con_throttle and con_unthrottle are only used for
 * paste_selection(), which has to stuff in a large number of
 * characters...
 */
static void con_throttle(struct tty_struct *tty)
{
}

static void con_unthrottle(struct tty_struct *tty)
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
        static unsigned long printing = 0;
        const ushort *start;
	struct vc_data *vc = admin_vt->fg_console;
        unsigned char c;
	ushort cnt = 0;
        ushort myx;

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
	/* I think this is wrong. Only should happen when visiable? */
        set_cursor(vc);
        poke_blanked_console(vc->display_fg);
quit:
        clear_bit(0, &printing);
}

static kdev_t vt_console_device(struct console *c)
{
        return MKDEV(TTY_MAJOR, c->index ? c->index : admin_vt->fg_console->vc_num);
}

void vt_console_unblank(void)
{
	unblank_screen(admin_vt);		
}

struct console vt_console_driver = {
	name:		"tty",
        write:		vt_console_print,
        device:		vt_console_device,
        wait_key:	keyboard_wait_for_keypress,
        unblank:	vt_console_unblank, 
        flags:		CON_PRINTBUFFER,
        index:		-1,
        cflag:		0,
};
#endif

/*
 * This routine initializes console interrupts, and does nothing
 * else. If you want the screen to clear, call tty_write with
 * the appropriate escape-sequence.
 */

struct tty_driver console_driver;
static int console_refcount;

DECLARE_TASKLET_DISABLED(console_tasklet, console_softint, 0);

void __init vt_console_init(void)
{
        const char *display_desc = NULL;
	struct vt_struct *vt;
	struct vc_data *vc;

        memset(&console_driver, 0, sizeof(struct tty_driver));
        console_driver.magic = TTY_DRIVER_MAGIC;
        console_driver.name = "vc/%d";
        console_driver.name_base = 0;
        console_driver.major = TTY_MAJOR;
        console_driver.minor_start = 0;
        console_driver.num = MAX_NR_CONSOLES;
        console_driver.type = TTY_DRIVER_TYPE_CONSOLE;
        console_driver.init_termios = tty_std_termios;
        console_driver.flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_RESET_TERMIOS;
        /* Tell tty_register_driver() to skip consoles because they are
         * registered before kmalloc() is ready. We'll patch them in later.
         * See comments at console_init(); see also con_init_devfs().
         */
        console_driver.flags |= TTY_DRIVER_NO_DEVFS;
        console_driver.refcount = &console_refcount;
        console_driver.table = console_table;
        console_driver.termios = console_termios;
        console_driver.termios_locked = console_termios_locked;

        console_driver.open = con_open;
        console_driver.close = con_close;
        console_driver.write = con_write;
        console_driver.write_room = con_write_room;
        console_driver.put_char = con_put_char;
        console_driver.flush_chars = con_flush_chars;
        console_driver.chars_in_buffer = con_chars_in_buffer;
        console_driver.ioctl = vt_ioctl;
        console_driver.stop = con_stop;
        console_driver.start = con_start;
        console_driver.throttle = con_throttle;
        console_driver.unthrottle = con_unthrottle;

        if (tty_register_driver(&console_driver))
                panic("Couldn't register console driver\n");

        /*
         * kmalloc is not running yet - we use the bootmem allocator.
         */
	vt = (struct vt_struct *) alloc_bootmem(sizeof(struct vt_struct));
	vt->kmalloced = 0;
	display_desc = create_vt(vt, conswitchp);
	if (!display_desc) { 
		free_bootmem((unsigned long) vt, sizeof(struct vt_struct));
		return;
	}
	vc = (struct vc_data *) alloc_bootmem(sizeof(struct vc_data));
	vt->last_console = vt->fg_console = vt->vcs.vc_cons[0] = vc; 
	vc->vc_num = 0;
        vc->display_fg = admin_vt = vt;
	visual_init(vc, 1);
        screenbuf = (unsigned short *) alloc_bootmem(screenbuf_size);
        vc_init(vc, !sw->con_save_screen); 
        
        set_origin(vc);
        save_screen(vc);
        gotoxy(vc, x, y);
        vte_ed(vc, 0);
        update_screen(vc);
        printk("Console: %s %s %dx%d",
                can_do_color ? "colour" : "mono",
                display_desc, video_num_columns, video_num_lines);
        printable = 1;
        printk("\n");

#ifdef CONFIG_VT_CONSOLE
        register_console(&vt_console_driver);
#endif
	want_vc = vc;
        tasklet_enable(&console_tasklet);
        tasklet_schedule(&console_tasklet);
}

#ifndef VT_SINGLE_DRIVER

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

void take_over_console(struct vt_struct *vt, struct consw *csw)
{
        const char *desc;
	int i;

        desc = csw->con_startup(vt);
        if (!desc) return;

        for (i = 0; i <= MAX_NR_USER_CONSOLES; i++) {
                int old_was_color;
		struct vc_data *vc = vt->vcs.vc_cons[i];

                if (!vc || !sw)
                        continue;

                if (IS_VISIBLE)
                        save_screen(vc);
                old_was_color = vc->vc_can_do_color;
                sw->con_deinit(vc);
                visual_init(vc, 0);
                update_attr(vc);

                /* If the console changed between mono <-> color, then
                 * the attributes in the screenbuf will be wrong.  The
                 * following resets all attributes to something sane.
                 */
                if (old_was_color != vc->vc_can_do_color)
                        clear_buffer_attributes(vc);

                if (IS_VISIBLE)
                        update_screen(vc);
        }
        printk("Console: switching to %s %s %dx%d\n", 
                vc->vc_can_do_color ? "colour" : "mono",
                desc, vc->vc_cols, vc->vc_rows);
}

void give_up_console(struct vt_struct *vt, struct consw *csw)
{
}

#endif

/* We can't register the console with devfs during con_init(), because it
 * is called before kmalloc() works.  This function is called later to
 * do the registration.
 */
void __init con_init_devfs (void)
{
        int i;

        for (i = 0; i < console_driver.num; i++)
                tty_register_devfs (&console_driver, DEVFS_FL_AOPEN_NOTIFY,
                                    console_driver.minor_start + i);
}

/*
 *      Visible symbols for modules
 */

EXPORT_SYMBOL(color_table);
EXPORT_SYMBOL(default_red);
EXPORT_SYMBOL(default_grn);
EXPORT_SYMBOL(default_blu);
EXPORT_SYMBOL(video_font_height);
EXPORT_SYMBOL(video_scan_lines);
EXPORT_SYMBOL(create_vt);
EXPORT_SYMBOL(vc_resize);
EXPORT_SYMBOL(vc_allocate);
EXPORT_SYMBOL(console_blank_hook);
EXPORT_SYMBOL(take_over_console);
EXPORT_SYMBOL(give_up_console);
