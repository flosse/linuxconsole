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
#include <linux/kbd_kern.h>
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

/*
 * Here is the default bell parameters: 750HZ, 1/8th of a second
 */
#define DEFAULT_BELL_PITCH      750
#define DEFAULT_BELL_DURATION   (HZ/8)

extern void vcs_make_devfs (unsigned int index, int unregister);

#ifndef MIN
#define MIN(a,b)        ((a) < (b) ? (a) : (b))
#endif

static struct tty_struct *console_table[MAX_NR_CONSOLES];
static struct termios *console_termios[MAX_NR_CONSOLES];
static struct termios *console_termios_locked[MAX_NR_CONSOLES];
struct vc vc_cons [MAX_NR_CONSOLES];

#ifndef VT_SINGLE_DRIVER
static struct consw *con_driver_map[MAX_NR_CONSOLES];
#endif

static void vte_decsc(struct vc_data *vc);
static void vte_ris(struct vc_data *vc, int do_clear);
static void con_flush_chars(struct tty_struct *tty);

static int printable = 0;               /* Is console ready for printing? */

int do_poke_blanked_console = 0;
int console_blanked = 0;

static int vesa_blank_mode = 0; /* 0:none 1:suspendV 2:suspendH 3:powerdown */
static int blankinterval = 10*60*HZ;
static int vesa_off_interval = 0;

/*
 * fg_console is the current virtual console,
 * last_console is the last used one,
 * want_console is the console we want to switch to,
 * kmsg_redirect is the console for kernel messages,
 */
int fg_console = 0;
int last_console = 0;
int want_console = -1;
int kmsg_redirect = 0;

/*
 * For each existing display, we have a pointer to console currently visible
 * on that display, allowing consoles other than fg_console to be refreshed
 * appropriately. Unless the low-level driver supplies its own display_fg
 * variable, we use this one for the "master display".
 */
static struct vc_data *master_display_fg = NULL;

/*
 * Unfortunately, we need to delay tty echo when we're currently writing to the
 * console since the code is (and always was) not re-entrant, so we insert
 * all filp requests to con_task_queue instead of tq_timer and run it from
 * the console_tasklet.  The console_tasklet is protected by the IRQ
 * protected console_lock.
 */
DECLARE_TASK_QUEUE(con_task_queue);

/*
 * Hook so that the power management routines can (un)blank
 * the console on our behalf.
 */
int (*console_blank_hook)(int) = NULL;

/*
 *      Low-Level Functions
 */

#define IS_FG (currcons == fg_console)
#define IS_VISIBLE CON_IS_VISIBLE(vc_cons[currcons].d)

#ifdef VT_BUF_VRAM_ONLY
#define DO_UPDATE 0
#else
#define DO_UPDATE IS_VISIBLE
#endif

static int pm_con_request(struct pm_dev *dev, pm_request_t rqst, void *data);
static struct pm_dev *pm_con = NULL;

/*
 * Console cursor handling
 */

void add_softcursor(struct vc_data *vc)
{
	int currcons = vc->vc_num;
        int i = scr_readw((u16 *) pos);                                                 u32 type = cursor_type;

        if (! (type & 0x10)) return;                                                    if (softcursor_original != -1) return;                                          softcursor_original = i;
        i |= ((type >> 8) & 0xff00 );
        i ^= ((type) & 0xff00 );
        if ((type & 0x20) && ((softcursor_original & 0x7000) == (i & 0x7000))) i ^= 0x7000;
        if ((type & 0x40) && ((i & 0x700) == ((i & 0x7000) >> 4))) i ^= 0x0700;
        scr_writew(i, (u16 *) pos);
        if (DO_UPDATE)
                sw->con_putc(vc, i, y, x);
}

static void hide_cursor(struct vc_data *vc)
{
	int currcons = vc->vc_num;	

        if (currcons == sel_cons)
                clear_selection();
        if (softcursor_original != -1) {
                scr_writew(softcursor_original,(u16 *) pos);
                if (DO_UPDATE)
			sw->con_putc(vc, softcursor_original, y, x);
                softcursor_original = -1;
        }
        sw->con_cursor(vc, CM_ERASE);
}

void set_cursor(struct vc_data *vc)
{
    int currcons = vc->vc_num;  	

    if (!IS_FG || console_blanked || vcmode == KD_GRAPHICS)
        return;
    if (dectcem) {
        if (currcons == sel_cons)
                clear_selection();
        add_softcursor(vc);
        if ((cursor_type & 0x0f) != 1)
            sw->con_cursor(vc, CM_DRAW);
    } else
        hide_cursor(vc);
}

/*
 * gotoxy() must verify all boundaries, because the arguments
 * might also be negative. If the given position is out of
 * bounds, the cursor is placed at the nearest margin.
 */
void gotoxy(struct vc_data *vc, int new_x, int new_y)
{
	int currcons = vc->vc_num;
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
static void gotoxay(struct vc_data *vc, int new_x, int new_y)
{
	int currcons = vc->vc_num;
	
        gotoxy(vc, new_x, decom ? (top+new_y) : new_y);
}

/*
 *      Palettes
 */

void set_palette(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        if (vcmode != KD_GRAPHICS)
                sw->con_set_palette(vc, color_table);
}

void reset_palette(struct vc_data *vc)
{
	int currcons = vc->vc_num;
        int j, k;

        for (j=k=0; j<16; j++) {
                palette[k++] = default_red[j];
                palette[k++] = default_grn[j];
                palette[k++] = default_blu[j];
        }
        set_palette(vc);
}

/*
 * Load palette into the DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */

static int set_get_cmap(unsigned char *arg, int set)
{
    int i, j, k;

    for (i = 0; i < 16; i++)
        if (set) {
            get_user(default_red[i], arg++);
            get_user(default_grn[i], arg++);
            get_user(default_blu[i], arg++);
        } else {
            put_user(default_red[i], arg++);
            put_user(default_grn[i], arg++);
            put_user(default_blu[i], arg++);
        }
    if (set) {
        for (i = 0; i < MAX_NR_CONSOLES; i++)
            if (vc_cons_allocated(i)) {
                for (j = k = 0; j < 16; j++) {
                    vc_cons[i].d->vc_palette[k++] = default_red[j];
                    vc_cons[i].d->vc_palette[k++] = default_grn[j];
                    vc_cons[i].d->vc_palette[k++] = default_blu[j];
                }
                set_palette(vc_cons[i].d);
            }
    }
    return 0;
}

int con_set_cmap(unsigned char *arg)
{
        return set_get_cmap (arg,1);
}

int con_get_cmap(unsigned char *arg)
{
        return set_get_cmap (arg,0);
}

/*
 * Functions to handle console scrolling.
 */
static int scrollback_delta = 0;

static inline void scrolldelta(int lines)
{
        scrollback_delta += lines;
        tasklet_schedule(&console_tasklet);
}

void scrollback(int lines)
{                                                                                       int currcons = fg_console;

        if (!lines)
                lines = video_num_lines/2;
        scrolldelta(-lines);
}

void scrollfront(int lines)
{
        int currcons = fg_console;

        if (!lines)
                lines = video_num_lines/2;
        scrolldelta(lines);
}

static void scrup(struct vc_data *vc, unsigned int t, unsigned int b, int nr)
{
	int currcons = vc->vc_num;
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

static void
scrdown(struct vc_data *vc, unsigned int t, unsigned int b, int nr)
{
	int currcons = vc->vc_num;
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
static u8 build_attr(struct vc_data *vc, u8 _color, u8 _intensity, u8 _blink, u8 _underline, u8 _reverse)
{
	int currcons = vc->vc_num;	

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

static void update_attr(struct vc_data *vc)
{
	int currcons = vc->vc_num; 

        attr = build_attr(vc, color, intensity, blink, underline, reverse ^ decscnm);
        video_erase_char = (build_attr(vc, color, intensity, 0, 0, decscnm) << 8) | ' ';
}

/*
 *  Character management
 */
void insert_char(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;
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

static void delete_char(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;
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

static void insert_line(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;

        scrdown(vc, y, bottom, nr);
        need_wrap = 0;
}

static void delete_line(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;

        scrup(vc, y, bottom, nr);
        need_wrap = 0;
}

/*
 * Functions that manage whats displayed on the screen
 */
static void set_origin(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        if (!IS_VISIBLE || !sw->con_set_origin || !sw->con_set_origin(vc))
                origin = (unsigned long) screenbuf;
        visible_origin = origin;
        scr_end = origin + screenbuf_size;
        pos = origin + video_size_row*y + 2*x;
}

static inline void save_screen(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        if (sw->con_save_screen)
                sw->con_save_screen(vc);
}

static void do_update_region(struct vc_data *vc, unsigned long start, int count)
{
#ifndef VT_BUF_VRAM_ONLY
	int currcons = vc->vc_num;
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
	int currcons = vc->vc_num;

        if (DO_UPDATE) {
                hide_cursor(vc);
                do_update_region(vc, start, count);
                set_cursor(vc);
        }
}

inline unsigned short *screenpos(struct vc_data *vc, int offset, int viewed)
{
	int currcons = vc->vc_num;
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
	int currcons = vc->vc_num;
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
	int currcons = vc->vc_num;
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

/*
 *      Redrawing of screen
 */

void redraw_screen(int new_console, int is_switch)
{
        int redraw = 1;
        int currcons, old_console;

        if (!vc_cons_allocated(new_console)) {
                /* strange ... */
                /* printk("redraw_screen: tty %d not allocated ??\n", new_console+1); */
                return;
        }

        if (is_switch) {
                currcons = fg_console;
                hide_cursor(vc_cons[currcons].d);
                if (fg_console != new_console) {
                        struct vc_data **display = vc_cons[new_console].d->vc_display_fg;
                        old_console = (*display) ? (*display)->vc_num : fg_console;
                        *display = vc_cons[new_console].d;
                        fg_console = new_console;
                        currcons = old_console;
                        if (!IS_VISIBLE) {
                                save_screen(vc_cons[currcons].d);
                                set_origin(vc_cons[currcons].d);
                        }
                        currcons = new_console;
                        if (old_console == new_console)
                                redraw = 0;
                }
        } else {
                currcons = new_console;
                hide_cursor(vc_cons[currcons].d);
        }

        if (redraw) {
                if (sw->con_switch(vc_cons[currcons].d) && vcmode != KD_GRAPHICS)
                        /* Change the palette after a VT switch. */
                        set_origin(vc_cons[currcons].d);
                        sw->con_set_palette(vc_cons[currcons].d, color_table);
                        /* Update the screen contents */
                        do_update_region(vc_cons[currcons].d, origin, screenbuf_size/2);
        }
        set_cursor(vc_cons[currcons].d);
        if (is_switch) {
                set_leds();
                compute_shiftstate();
        }
}

/*
 *      Screen blanking
 */
static void set_vesa_blanking(unsigned long arg)
{
    char *argp = (char *)arg + 1;
    unsigned int mode;
    get_user(mode, argp);
    vesa_blank_mode = (mode < 4) ? mode : 0;
}

static void vesa_powerdown(void)
{
    struct vc_data *vc = vc_cons[fg_console].d;
    /*
     *  Power down if currently suspended (1 or 2),
     *  suspend if currently blanked (0),
     *  else do nothing (i.e. already powered down (3)).
     *  Called only if powerdown features are allowed.
     */
    switch (vesa_blank_mode) {
        case VESA_NO_BLANKING:
            vc->vc_sw->con_blank(vc, VESA_VSYNC_SUSPEND+1);
            break;
        case VESA_VSYNC_SUSPEND:
        case VESA_HSYNC_SUSPEND:
            vc->vc_sw->con_blank(vc, VESA_POWERDOWN+1);
            break;
    }
}

static void blank_screen(void)
{
        do_blank_screen(0);
}

void unblank_screen(void)
{
        int currcons;

        if (!console_blanked)
                return;
        if (!vc_cons_allocated(fg_console)) {
                /* impossible */
                printk("unblank_screen: tty %d not allocated ??\n", fg_console+1);
                return;
        }
        timer_table[BLANK_TIMER].fn = blank_screen;
        if (blankinterval) {
                timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
                timer_active |= 1<<BLANK_TIMER;
        }

        currcons = fg_console;
        console_blanked = 0;
        if (console_blank_hook)
                console_blank_hook(0);
        if (sw->con_blank(vc_cons[currcons].d, 0))
                /* Low-level driver cannot restore -> do it ourselves */
                update_screen(fg_console);
        set_cursor(vc_cons[fg_console].d);
}

static void vesa_powerdown_screen(void)
{
        timer_active &= ~(1<<BLANK_TIMER);
        timer_table[BLANK_TIMER].fn = unblank_screen;

        vesa_powerdown();
}

void do_blank_screen(int entering_gfx)
{
        int currcons = fg_console;
	struct vc_data *vc = vc_cons[currcons].d;
        int i;

        if (console_blanked)
                return;

        /* entering graphics mode? */
        if (entering_gfx) {
                hide_cursor(vc);
                save_screen(vc);
                sw->con_blank(vc, -1);
                console_blanked = fg_console + 1;
                set_origin(vc);
                return;
        }

        /* don't blank graphics */
        if (vcmode != KD_TEXT) {
                console_blanked = fg_console + 1;
                return;
        }

        hide_cursor(vc);
        if (vesa_off_interval) {
                timer_table[BLANK_TIMER].fn = vesa_powerdown_screen;
                timer_table[BLANK_TIMER].expires = jiffies + vesa_off_interval;
                timer_active |= (1<<BLANK_TIMER);
        } else {
                timer_active &= ~(1<<BLANK_TIMER);
                timer_table[BLANK_TIMER].fn = unblank_screen;
        }

        save_screen(vc);
        /* In case we need to reset origin, blanking hook returns 1 */
        i = sw->con_blank(vc_cons[currcons].d, 1);
        console_blanked = fg_console + 1;
        if (i)
                set_origin(vc);

        if (console_blank_hook && console_blank_hook(1))
                return;
        if (vesa_blank_mode)
                sw->con_blank(vc, vesa_blank_mode + 1);
}

void poke_blanked_console(void)
{
        timer_active &= ~(1<<BLANK_TIMER);
        if (vt_cons[fg_console]->vc_mode == KD_GRAPHICS)
                return;
        if (console_blanked) {
                timer_table[BLANK_TIMER].fn = unblank_screen;
                timer_table[BLANK_TIMER].expires = jiffies;     /* Now */
                timer_active |= 1<<BLANK_TIMER;
        } else if (blankinterval) {
                timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
                timer_active |= 1<<BLANK_TIMER;
        }
}

/*
 * Power management for the console system.
 */
static int pm_con_request(struct pm_dev *dev, pm_request_t rqst, void *data)
{
  switch (rqst) {
  case PM_RESUME:
                        unblank_screen();
                        break;
  case PM_SUSPEND:
                        blank_screen();
                        break;
  }
        return 0;
}

/*
 *      Allocation, freeing and resizing of VTs.
 */

int vc_cons_allocated(unsigned int i)
{
        return (i < MAX_NR_CONSOLES && vc_cons[i].d);
}

static void visual_init(int currcons, int init)
{
    /* ++Geert: sw->con_init determines console size */
    sw = conswitchp;
#ifndef VT_SINGLE_DRIVER
    if (con_driver_map[currcons])
        sw = con_driver_map[currcons];
#endif
    cons_num = currcons;
    display_fg = &master_display_fg;
    vc_cons[currcons].d->vc_uni_pagedir_loc = &vc_cons[currcons].d->vc_uni_pagedir;
    vc_cons[currcons].d->vc_uni_pagedir = 0;
    hi_font_mask = 0;
    complement_mask = 0;
    can_do_color = 0;
    sw->con_init(vc_cons[currcons].d, init);
    if (!complement_mask)
        complement_mask = can_do_color ? 0x7700 : 0x0800;
    s_complement_mask = complement_mask;
    video_size_row = video_num_columns<<1;
    screenbuf_size = video_num_lines*video_size_row;
}

static void vc_init(unsigned int currcons, unsigned int rows, unsigned int cols, int do_clear)
{
        int j, k ;

        video_num_columns = cols;
        video_num_lines = rows;
        video_size_row = cols<<1;
        screenbuf_size = video_num_lines * video_size_row;

        set_origin(vc_cons[currcons].d);
        pos = origin;
        reset_vc(currcons);
        for (j=k=0; j<16; j++) {
                vc_cons[currcons].d->vc_palette[k++] = default_red[j] ;
                vc_cons[currcons].d->vc_palette[k++] = default_grn[j] ;
                vc_cons[currcons].d->vc_palette[k++] = default_blu[j] ;
        }
        def_color       = 0x07;   /* white */
        ulcolor         = 0x0f;   /* bold white */
        halfcolor       = 0x08;   /* grey */
        init_waitqueue_head(&vt_cons[currcons]->paste_wait);
        vte_ris(vc_cons[currcons].d, do_clear);
}

int vc_allocate(unsigned int currcons)  /* return 0 on success */
{
        if (currcons >= MAX_NR_CONSOLES)
                return -ENXIO;
        if (!vc_cons[currcons].d) {
            long p, q;

            /* prevent users from taking too much memory */
            if (currcons >= MAX_NR_USER_CONSOLES && !capable(CAP_SYS_RESOURCE))
              return -EPERM;

            /* due to the granularity of kmalloc, we waste some memory here */
            /* the alloc is done in two steps, to optimize the common situation
               of a 25x80 console (structsize=216, screenbuf_size=4000) */
            /* although the numbers above are not valid since long ago, the
               point is still up-to-date and the comment still has its value
               even if only as a historical artifact.  --mj, July 1998 */
            p = (long) kmalloc(structsize, GFP_KERNEL);
            if (!p)
                return -ENOMEM;
            vc_cons[currcons].d = (struct vc_data *)p;
            vt_cons[currcons] = (struct vt_struct *)(p+sizeof(struct vc_data));
            visual_init(currcons, 1);
            if (!*vc_cons[currcons].d->vc_uni_pagedir_loc)
                con_set_default_unimap(currcons);
            q = (long)kmalloc(screenbuf_size, GFP_KERNEL);
            if (!q) {
                kfree_s((char *) p, structsize);
                vc_cons[currcons].d = NULL;
                vt_cons[currcons] = NULL;
                return -ENOMEM;
            }
            screenbuf = (unsigned short *) q;
            kmalloced = 1;
            vc_init(currcons, video_num_lines, video_num_columns, 1);

            if (!pm_con) {
                    pm_con = pm_register(PM_SYS_DEV,
                                         PM_SYS_VGA,
                                         pm_con_request);
            }
        }
        return 0;
}

/*
 * Change # of rows and columns (0 means unchanged/the size of fg_console)
 * [this is to be used together with some user program
 * like resize that changes the hardware videomode]
 */
int vc_resize(unsigned int lines, unsigned int cols,
              unsigned int first, unsigned int last)
{
        unsigned int cc, ll, ss, sr, todo = 0;
        unsigned int currcons = fg_console, i;
        unsigned short *newscreens[MAX_NR_CONSOLES];

        cc = (cols ? cols : video_num_columns);
        ll = (lines ? lines : video_num_lines);
        sr = cc << 1;
        ss = sr * ll;

        for (currcons = first; currcons <= last; currcons++) {
                if (!vc_cons_allocated(currcons) ||
                    (cc == video_num_columns && ll == video_num_lines))
                        newscreens[currcons] = NULL;
                else {
                        unsigned short *p = (unsigned short *) kmalloc(ss, GFP_USER);
                        if (!p) {
                                for (i = first; i < currcons; i++)
                                        if (newscreens[i])
                                                kfree_s(newscreens[i], ss);
                                return -ENOMEM;
                        }
                        newscreens[currcons] = p;
                        todo++;
                }
        }
        if (!todo)
                return 0;

        for (currcons = first; currcons <= last; currcons++) {
                unsigned int occ, oll, oss, osr;
                unsigned long ol, nl, nlend, rlth, rrem;
                if (!newscreens[currcons] || !vc_cons_allocated(currcons))
                        continue;

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
                nl = (long) newscreens[currcons];
                nlend = nl + ss;
                if (ll < oll)
                        ol += (oll - ll) * osr;

                update_attr(vc_cons[currcons].d);

                while (ol < scr_end) {
                        scr_memcpyw((unsigned short *) nl, (unsigned short *) ol, rlth);
                        if (rrem)
                                scr_memsetw((void *)(nl + rlth), video_erase_char, rrem);
                        ol += osr;
                        nl += sr;
                }
                if (nlend > nl)
                        scr_memsetw((void *) nl, video_erase_char, nlend - nl);
                if (kmalloced)
                        kfree_s(screenbuf, oss);
                screenbuf = newscreens[currcons];
                kmalloced = 1;
                screenbuf_size = ss;
                set_origin(vc_cons[currcons].d);

                /* do part of a vte_ris() */
                top = 0;
                bottom = video_num_lines;
                gotoxy(vc_cons[currcons].d, x, y);
                vte_decsc(vc_cons[currcons].d);

                if (console_table[currcons]) {
                        struct winsize ws, *cws = &console_table[currcons]->winsize;
                        memset(&ws, 0, sizeof(ws));
                        ws.ws_row = video_num_lines;
                        ws.ws_col = video_num_columns;
                        if ((ws.ws_row != cws->ws_row || ws.ws_col != cws->ws_col) &&
                            console_table[currcons]->pgrp > 0)
                                kill_pg(console_table[currcons]->pgrp, SIGWINCH, 1);
                        *cws = ws;
                }

                if (IS_VISIBLE)
                        update_screen(currcons);
        }

        return 0;
}

void vc_disallocate(unsigned int currcons)
{
        if (vc_cons_allocated(currcons)) {
            sw->con_deinit(vc_cons[currcons].d);
            if (kmalloced)
                kfree_s(screenbuf, screenbuf_size);
            if (currcons >= MIN_NR_CONSOLES)
                kfree_s(vc_cons[currcons].d, structsize);
            vc_cons[currcons].d = NULL;
        }
}

/*
 * VT102 emulator
 */
#define VTE_VERSION     211

/*
 * Different states of the emulator
 */
enum {  ESinit,
        /* ESC substates */
        ESesc, ESacs, ESscf, ESgzd4, ESg1d4, ESg2d4,
        ESg3d4, ESg1d6, ESg2d6, ESg3d6, ESdocs,
        /* CSI substates */
        EScsi, EScsi_getpars, EScsi_gotpars, EScsi_space,
        EScsi_exclam, EScsi_dquote, EScsi_dollar, EScsi_and,
        EScsi_squote, EScsi_star, EScsi_plus,
        /* OSC substates */
        ESosc, ESpalette,
        /* Misc. states */
        ESfunckey, ESignore,
};

/*
 *
 */
#define __VTE_CSI       (c8bit == 0 ? "\033[" : "\233")
#define __VTE_DCS       (c8bit == 0 ? "\033P" : "\220")
#define __VTE_ST        (c8bit == 0 ? "\033\\" : "\234")
#define __VTE_APC       (c8bit == 0 ? "\033_" : "\237")

#define set_kbd(x) set_vc_kbd_mode(kbd_table+currcons,x)
#define clr_kbd(x) clr_vc_kbd_mode(kbd_table+currcons,x)
#define is_kbd(x) vc_kbd_mode(kbd_table+currcons,x)

unsigned char color_table[] = { 0, 4, 2, 6, 1, 5, 3, 7,
                                       8,12,10,14, 9,13,11,15 };

/* the default colour table, for VGA+ colour systems */
int default_red[] = {0x00,0xaa,0x00,0xaa,0x00,0xaa,0x00,0xaa,
    0x55,0xff,0x55,0xff,0x55,0xff,0x55,0xff};
int default_grn[] = {0x00,0x00,0xaa,0x55,0x00,0x00,0xaa,0xaa,
    0x55,0x55,0xff,0xff,0x55,0x55,0xff,0xff};
int default_blu[] = {0x00,0x00,0x00,0x00,0xaa,0xaa,0xaa,0xaa,
    0x55,0x55,0x55,0x55,0xff,0xff,0xff,0xff};

/*
 * LINE FEED (LF)
 */
static void vte_lf(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        /* don't scroll if above bottom of scrolling region, or
         * if below scrolling region
         */
        if (y+1 == bottom)
                scrup(vc, top, bottom, 1);
        else if (y < video_num_lines-1) {
                y++;
                pos += video_size_row;
        }
        need_wrap = 0;
}

/*
 * REVERSE LINE FEED (RI)
 */
static void vte_ri(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        /* don't scroll if below top of scrolling region, or
         * if above scrolling region
         */
        if (y == top)
                scrdown(vc, top, bottom, 1);
        else if (y > 0) {
                y--;
                pos -= video_size_row;
        }
        need_wrap = 0;
}

/*
 * CARRIAGE RETURN (CR)
 */
static inline void vte_cr(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        pos -= x<<1;
        need_wrap = x = 0;
}

/*
 * BACK SPACE (BS)
 */
static inline void vte_bs(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        if (x) {
                pos -= 2;
                x--;
                need_wrap = 0;
        }
}

#ifdef CONFIG_VT_EXTENDED
/*
 * CURSOR LINE TABULATION (CVT)
 *
 * NOTE:
 * In accordance with our interpretation of VT as LF we will treat CVT as
 * (par[0] * LF).  Not very creative, but at least consequent.
 */
static void vte_cvt(struct vc_data *vc, int vpar)
{
        int i;

        for (i = 0; i < vpar; i++) {
                vte_lf(vc);
        }
}

/*
 * CURSOR BACKWARD TABULATION (CBT)
 */
static void vte_cbt(struct vc_data *vc, int vpar)
{
	int currcons = vc->vc_num;
        int i;

        for (i = 0; i < vpar; i++) {
                pos -= (x << 1);
                while (x > 0) {
                        x--;
                        if (tab_stop[x >> 5] & (1 << (x & 31)))
                                break;
                }
        	pos += (x << 1);
        }
}

/*
 * CURSOR FORWARD TABULATION (CHT)
 */
static void vte_cht(struct vc_data *vc, int vpar)
{
	int currcons = vc->vc_num;
        int i;

        for (i = 0; i < vpar; i++) {
                pos -= (x << 1);
                while (x < video_num_columns - 1) {
                        x++;
                        if (tab_stop[x >> 5] & (1 << (x & 31)))
                                break;
                }
       	 	pos += (x << 1);
        }
}
#endif /* CONFIG_VT_EXTENDED */

/*
 * ERASE IN PAGE (ED)
 */
static void vte_ed(struct vc_data *vc, int vpar)
{
	int currcons = vc->vc_num;
        unsigned int count;
        unsigned short * start;

        switch (vpar) {
                case 0: /* erase from cursor to end of display */
                        count = (scr_end-pos)>>1;
                        start = (unsigned short *) pos;
                        if (DO_UPDATE) {
                                /* do in two stages */
                                sw->con_clear(vc, y, x, 1, video_num_columns-x);
                                sw->con_clear(vc, y+1, 0, video_num_lines-y-1,
                                              video_num_columns);
                        }
                        break;
                case 1: /* erase from start to cursor */
                        count = ((pos-origin)>>1)+1;
                        start = (unsigned short *) origin;
                        if (DO_UPDATE) {
                                /* do in two stages */
                                sw->con_clear(vc, 0, 0, y, video_num_columns);
                                sw->con_clear(vc, y, 0, 1, x + 1);
                        }
                        break;
                case 2: /* erase whole display */
                        count = video_num_columns * video_num_lines;
                        start = (unsigned short *) origin;
                        if (DO_UPDATE)
                                sw->con_clear(vc, 0, 0, video_num_lines,
                                              video_num_columns);
                        break;
                default:
                        return;
        }
        scr_memsetw(start, video_erase_char, 2*count);
        need_wrap = 0;
}

/*
 * ERASE IN LINE (EL)
 */
static void vte_el(struct vc_data *vc, int vpar)
{
	int currcons = vc->vc_num;
        unsigned int count;
        unsigned short * start;

        switch (vpar) {
                case 0: /* erase from cursor to end of line */
                        count = video_num_columns-x;
                        start = (unsigned short *) pos;
                        if (DO_UPDATE)
                                sw->con_clear(vc, y, x, 1, video_num_columns-x);
                        break;
                case 1: /* erase from start of line to cursor */
                        start = (unsigned short *) (pos - (x<<1));
                        count = x+1;
                        if (DO_UPDATE)
                                sw->con_clear(vc, y, 0, 1, x + 1);
                        break;
                case 2: /* erase whole line */
                        start = (unsigned short *) (pos - (x<<1));
                        count = video_num_columns;
                        if (DO_UPDATE)
                                sw->con_clear(vc, y, 0, 1, video_num_columns);
                        break;
                default:
                        return;
        }
        scr_memsetw(start, video_erase_char, 2 * count);
        need_wrap = 0;
}

/*
 * Erase character (ECH)
 *
 * NOTE:  This function is not available in DEC VT1xx terminals.
 */
static void vte_ech(struct vc_data *vc, int vpar)
{
	int currcons = vc->vc_num;
        int count;

        if (!vpar)
                vpar++;
        count = (vpar > video_num_columns-x) ? (video_num_columns-x) : vpar;

        scr_memsetw((unsigned short *) pos, video_erase_char, 2 * count);
        if (DO_UPDATE)
                sw->con_clear(vc, y, x, 1, count);
        need_wrap = 0;
}

static void default_attr(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        intensity = 1;
        underline = 0;
        reverse = 0;
        blink = 0;
        color = def_color;
}

/*
 * SELECT GRAPHIC RENDITION (SGR)
 *
 * NOTE: The DEC vt1xx series only implements attribute values 0,1,4,5 and 7.
 */
static void vte_sgr(struct vc_data *vc)
{
	int currcons = vc->vc_num;
        int i;

        for (i=0;i<=npar;i++)
                switch (par[i]) {
                        case 0: /* all attributes off */
                                default_attr(vc);
                                break;
                        case 1: /* bold or increased intensity */
                                intensity = 2;
                                break;
                        case 2: /* faint or decreased intensity */
                                intensity = 0;
                                break;
                        case 4: /* singly underlined. */
                                underline = 1;
                                break;
                        case 5: /* slowly blinking (< 2.5 Hz) */
#ifdef CONFIG_VT_EXTENDED
                        case 6: /* rapidly blinking (>= 2.5 Hz) */
#endif /* CONFIG_VT_EXTENDED */
                                blink = 1;
                                break;
                        case 7: /* negative image */
                                reverse = 1;
                                break;
                        case 10:        /*  primary (default) font */
                                translate = set_translate(charset == 0
                                                ? G0_charset
                                                : G1_charset, cons_num);
                                disp_ctrl = 0;
                                toggle_meta = 0;
                                break;
                        case 11:        /* first alternative font */
                                translate = set_translate(IBMPC_MAP,cons_num);
                                disp_ctrl = 1;
                                toggle_meta = 0;
                                break;
                        case 12:        /* second alternative font */
                                translate = set_translate(IBMPC_MAP,cons_num);
                                disp_ctrl = 1;
                                toggle_meta = 1;
                                break;
#if 1 /* ndef CONFIG_VT_EXTENDED */
                        case 21:        /* normal intensity */
#endif /* CONFIG_VT_EXTENDED */
                        case 22:        /* normal intensity */
                                intensity = 1;
                                break;
                        case 24:        /* not underlined (neither singly nor doubly) */
                                underline = 0;
                                break;
                        case 25:        /* steady (not blinking) */
                                blink = 0;
                                break;
                        case 27:        /* positive image */
                                reverse = 0;
                                break;
                        case 38:        /* foreground color (ISO 8613-6/ITU T.416) */
                                color = (def_color & 0x0f) | background;
                                underline = 1;
                                break;
                        case 39:        /* default display color */
                                color = (def_color & 0x0f) | background;
                                underline = 0;
                                break;
                        case 49:        /* default background color */
                                color = (def_color & 0xf0) | foreground;
                                break;
                        default:
                                if (par[i] >= 30 && par[i] <= 37)
                                        color = color_table[par[i]-30]
                                                | background;
                                else if (par[i] >= 40 && par[i] <= 47)
                                        color = (color_table[par[i]-40]<<4)
                                                | foreground;
                                break;
                }
        update_attr(vc);
}

static void respond_string(const char * p, struct tty_struct * tty)
{
        while (*p) {
                tty_insert_flip_char(tty, *p, 0);
                p++;
        }
        con_schedule_flip(tty);
}

#ifdef CONFIG_VT_EXTENDED
/*
 * Fake a DEC DSR for non-implemented features
 */
static void vte_fake_dec_dsr(struct vc_data *vc, struct tty_struct *tty, char *reply)
{
	int currcons = vc->vc_num;	
        char buf[40];

        sprintf(buf, "%s?%sn", __VTE_CSI, reply);
        respond_string(buf, tty);
}
#endif /* CONFIG_VT_EXTENDED */

/*
 * CURSOR POSITION REPORT (CPR)
 * DEC EXTENDED CURSOR POSITION REPORT (DECXCPR)
 */
static void vte_cpr(struct vc_data *vc, struct tty_struct *tty, int ext)
{
	int currcons = vc->vc_num;
        char buf[40];

#ifdef CONFIG_VT_EXTENDED
        if (ext) {
                /*
                 * NOTE:  Since we do not (yet?) implement any form of page
                 * memory, we will always return the cursor position in page 1.
                 */
                sprintf(buf, "%s?%d;%d;1R", __VTE_CSI,
                                y + (decom ? top + 1 : 1), x+1);
        } else {
                sprintf(buf, "%s%d;%dR", __VTE_CSI,
                                y + (decom ? top + 1 : 1), x+1);
        }
        respond_string(buf, tty);
#else   /* ndef CONFIG_VT_EXTENDED */
        sprintf(buf, "\033[%d;%dR", y + (decom ? top + 1 : 1), x+1);
        respond_string(buf, tty);
#endif  /* ndef CONFIG_VT_EXTENDED */
}

/*
 * DEVICE STATUS REPORT (DSR)
 */
static inline void vte_dsr(struct vc_data *vc, struct tty_struct * tty)
{
	int currcons = vc->vc_num;

#ifdef CONFIG_VT_EXTENDED
        char buf[40];
        sprintf(buf, "%s0n", __VTE_CSI);
        respond_string(buf, tty);
#else /* ndef CONFIG_VT_EXTENDED */
        respond_string("\033[0n", tty); /* Terminal ok */
#endif
}

/*
 * ANSWERBACK MESSAGE
 */
static inline void vte_answerback(struct tty_struct *tty)
{
        respond_string("l i n u x", tty);
}

/*
 * DA - DEVICE ATTRIBUTE
 */
static inline void vte_da(struct vc_data *vc, struct tty_struct *tty)
{
	int currcons = vc->vc_num;
#ifdef CONFIG_VT_EXTENDED

        char buf[40];

        /* We claim VT220 compatibility... */
        sprintf(buf, "%s?62;1;2;6;7;8;9c", __VTE_CSI);
        respond_string(buf, tty);

#else /* ! CONFIG_VT_EXTENDED */

        /* We are a VT102 */
        respond_string("\033[?6c", tty);

#endif /* ! CONFIG_VT_EXTENDED */
}

#ifdef CONFIG_VT_EXTENDED
/*
 * DA - SECONDARY DEVICE ATTRIBUTE [VT220 and up]
 *
 * Reply parameters:
 * 1 = Model (1=vt220, 18=vt330, 19=vt340, 41=vt420)
 * 2 = Firmware version (nn = n.n)
 * 3 = Installed options (0 = none)
 */
static void vte_dec_da2(struct vc_data *vc, struct tty_struct *tty)
{
	int currcons = vc->vc_num;
        char buf[40];

        sprintf(buf, "%s>%d;%d;0c", __VTE_CSI, 1, VTE_VERSION / 10);
        respond_string(buf, tty);
}

/*
 * DA - TERTIARY DEVICE ATTRIBUTE [VT220 and up]
 *
 * Reply: unit ID (we report "0")
 */
static void vte_dec_da3(struct vc_data *vc, struct tty_struct *tty)
{
	int currcons = vc->vc_num;
        char buf[40];

        sprintf(buf, "%s!|%s%s", __VTE_DCS, "0", __VTE_ST);
        respond_string(buf, tty);
}

/*
 * DECREPTPARM - DEC REPORT TERMINAL PARAMETERS [VT1xx/VT2xx/VT320]
 */
static void vte_decreptparm(struct vc_data *vc, struct tty_struct *tty)
{
	int currcons = vc->vc_num;
        char buf[40];

        sprintf(buf, "\033[%d;1;1;120;120;1;0x", par[0] + 2);
        respond_string(buf, tty);
}
#endif /* CONFIG_VT_EXTENDED */

void mouse_report(struct tty_struct * tty, int butt, int mrx, int mry)
{
        char buf[8];

        sprintf(buf, "\033[M%c%c%c", (char)(' ' + butt), (char)('!' + mrx),
                (char)('!' + mry));
        respond_string(buf, tty);
}

/* invoked via ioctl(TIOCLINUX) and through set_selection */
int mouse_reporting(void)
{
        int currcons = fg_console;

        return report_mouse;
}

/*
 * SM - SET MODE /
 * RM - RESET MODE
 */
static void set_mode(struct vc_data *vc, int on_off)
{
	int currcons = vc->vc_num;
        int i;

        for (i=0; i<=npar; i++)
                if (priv4) switch(par[i]) {     
			/* DEC private modes set/reset */
                        case 1: /* DECCKM - Cursor keys mode */
                                if (on_off)
                                        set_kbd(VC_CKMODE);
                                else
                                        clr_kbd(VC_CKMODE);
                                break;
                        case 2: /* DECANM - ANSI mode */
                                break;
                        case 3: /* DECCOLM -  Column mode */
#if 0
                                deccolm = on_off;
                                (void) vc_resize(video_num_lines, deccolm ? 132 : 80);
                                /* this alone does not suffice; some user mode
                                   utility has to change the hardware regs */
#endif
                                break;
                        case 4: /* DECSCLM - Scrolling mode */
                                break;
                        case 5: /* DECSCNM - Screen mode */
                                if (decscnm != on_off) {
                                        decscnm = on_off;
                                        invert_screen(vc, 0, screenbuf_size, 0);
                                        update_attr(vc);
                                }
                                break;
                        case 6: /* DECOM - Origin mode */
                                decom = on_off;
                                gotoxay(vc, 0, 0);
                                break;
                        case 7: /* DECAWM - Autowrap mode */
                                decawm = on_off;
                                break;
                        case 8: /* DECARM - Autorepeat mode */
                                decarm = on_off;
                                if (on_off)
                                        set_kbd(VC_REPEAT);
                                else
                                        clr_kbd(VC_REPEAT);
                                break;
                        case 9:
                                report_mouse = on_off ? 1 : 0;
                                break;
                        case 25:        /* DECTCEM - Text cursor enable mode */
                                dectcem = on_off;
                                break;
#ifdef CONFIG_VT_EXTENDED
                        case 42: /* DECNCRS - National character set replacement mode */
                                break;
                        case 60: /* DECHCCM - Horizontal cursor coupling mode */
                                break;
                        case 61: /* DECVCCM - Vertical cursor coupling mode */
                                break;
                        case 64: /* DECPCCM - Page cursor coupling mode */
                                break;
                        case 66: /* DECNKM - Numeric keybad mode */
                                decnkm = on_off;
                                if (on_off)
                                        set_kbd(VC_APPLIC);
                                else
                                        clr_kbd(VC_APPLIC);
                                break;
                        case 67:        /* DECBKM - Backarrow key mode */
                                break;
                        case 68:        /* DECKBUM - Keyboard usage mode */
                                break;
                        case 69:        /* DECVSSM - Vertical split screen mode */
                                break;
                        case 73:        /* DECXRLM - Transfer rate limiting mode */
                                break;
                        case 81:        /* DECKPM - Keyboard position mode */
                                break;
#endif /* def CONFIG_VT_EXTENDED */
                        case 1000:
                                report_mouse = on_off ? 2 : 0;
                                break;
                } else switch(par[i]) {         /* ANSI modes set/reset */
                        case 3:                 /* Monitor (display ctrls) */
                                disp_ctrl = on_off;
                                break;
                        case 4:                 /* Insert Mode on/off */
                                irm = on_off;
                                break;
                        case 20:                /* Lf, Enter == CrLf/Lf */
                                if (on_off)
                                        set_kbd(VC_CRLF);
                                else
                                        clr_kbd(VC_CRLF);
                                break;
                }
}

#ifdef CONFIG_VT_EXTENDED
/*
 * DECCIR - Cursor information report
 */
static void vte_deccir(struct tty_struct *tty)
{
        /* not yet implemented */
}

/*
 * DECMSR - Macro space report
 */
static void vte_decmsr(struct vc_data *vc, struct tty_struct *tty)
{
	int currcons = vc->vc_num;
        char buf[40];

        sprintf(buf, "%s%d*{", __VTE_CSI, 0); /* No space left */
        respond_string(buf, tty);
}

/*
 * DECRPM - Report mode
 */
static void vte_decrpm(struct vc_data *vc, struct tty_struct *tty, int priv, int mode, int status)
{
	int currcons = vc->vc_num;
        char buf[40];

        if (status == 0) {
                status = 2;
        } else {
                if (status == 2) {
                        status = 0;
                }
        }

        if (priv)
                sprintf(buf, "%s?%d;%d$y", __VTE_CSI, mode, status);
        else
                sprintf(buf, "%s%d;%d$y", __VTE_CSI, mode, status);
        respond_string(buf, tty);
}

/*
 * DECRQM - Request mode
 *
 * Reply codes:
 * 0 = reset
 * 1 = set
 * 2 = unknown
 * 3 = premanently set
 * 4 = permanently reset
 */
static void vte_decrqm(struct vc_data *vc, struct tty_struct *tty, int priv)
{
	int currcons = vc->vc_num;

        if (priv) {
                switch (par[0]) {
                        case 1: /* DECCKM - Cursor keys mode */
                                vte_decrpm(vc, tty, priv, par[0], decckm);
                                break;
                        case 2: /* DECANM */
                        case 3: /* DECCOLM */
                        case 4: /* DECSCLM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        case 5: /* DECSCNM */
                                vte_decrpm(vc, tty, priv, par[0], decscnm);
                                break;
                        case 6: /* DECOM */
                                vte_decrpm(vc, tty, priv, par[0], decom);
                                break;
                        case 7: /* DECAWM */
                                vte_decrpm(vc, tty, priv, par[0], decawm);
                                break;
                        case 8: /* DECARM */
                                vte_decrpm(vc, tty, priv, par[0], decarm);
                                break;
                        case 25: /* DECTCEM */
                                vte_decrpm(vc, tty, priv, par[0], dectcem);
                                break;
                        case 42: /* DECNCRM */
                        case 60: /* DECHCCM */
                        case 61: /* DECVCCM */
                        case 64: /* DECPCCM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        case 66: /* DECNKM */
                                vte_decrpm(vc, tty, priv, par[0], decnkm);
                                break;
                        case 67: /* DECBKM */
                        case 68: /* DECKBUM */
                        case 69: /* DECVSSM */
                        case 73: /* DECXRLM */
                        case 81: /* DECKPM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        default:
                                vte_decrpm(vc, tty, priv, par[0], 2);
                }
        } else {
                switch (par[0]) {
                        case 1: /* GATM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        case 2: /* KAM */
                                vte_decrpm(vc, tty, priv, par[0], kam);
                                break;
                        case 3: /* CRM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        case 4: /* IRM */
                                vte_decrpm(vc, tty, priv, par[0], irm);
                                break;
                        case 5: /* SRTM */
                        case 6: /* ERM */
                        case 7: /* VEM */
                        case 8: /* BDSM */
                        case 9: /* DCSM */
                        case 10: /* HEM */
                        case 11: /* PUM */
                        case 12: /* SRM */
                        case 13: /* FEAM */
                        case 14: /* FETM */
                        case 15: /* MATM */
                        case 16: /* TTM */
                        case 17: /* SATM */
                        case 18: /* TSM */
                        case 19: /* EBM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        case 20: /* LNM */
                                vte_decrpm(vc, tty, priv, par[0], lnm);
                                break;
                        case 21: /* GRCM */
                        case 22: /* ZDM */
                                vte_decrpm(vc, tty, priv, par[0], 4);
                                break;
                        default:
                                vte_decrpm(vc, tty, priv, par[0], 2);
                }
        }
}

/*
 * DECSCL - Set operating level
 */
static void vte_decscl(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        switch (par[0]) {
                case 61:        /* VT100 mode */
                        if (npar == 1) {
                                decscl = 1;
                                c8bit = 0;
                        }
                        break;
                case 62:        /* VT200 mode */
                case 63:        /* VT300 mode */
                case 64:        /* VT400 mode */
                        if (npar <= 2) {
                                decscl = 4;
                                if (par[1] == 1)
                                        c8bit = 0;
                                else
                                        c8bit = 1;
                        }
                        break;
        }
        return;
}

/*
 * DECTABSR - Tabulation stop report
 */
void vte_dectabsr(struct tty_struct *tty)
{
        /* not yet implemented */
}

/*
 * DECTSR - Terminal state report
 */
void vte_dectsr(struct tty_struct *tty)
{
        /* not yet implemented */
}
#endif /* def CONFIG_VT_EXTENDED */

static void setterm_command(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        switch(par[0]) {
                case 1: /* set color for underline mode */
                        if (can_do_color && par[1] < 16) {
                                ulcolor = color_table[par[1]];
                                if (underline)
                                        update_attr(vc);
                        }
                        break;
                case 2: /* set color for half intensity mode */
                        if (can_do_color && par[1] < 16) {
                                halfcolor = color_table[par[1]];
                                if (intensity == 0)
                                        update_attr(vc);
                        }
                        break;
                case 8: /* store colors as defaults */
                        def_color = attr;
                        if (hi_font_mask == 0x100)
                                def_color >>= 1;
                        default_attr(vc);
                        update_attr(vc);
                        break;
                case 9: /* set blanking interval */
                        blankinterval = ((par[1] < 60) ? par[1] : 60) * 60 * HZ;
                        poke_blanked_console();
                        break;
                case 10: /* set bell frequency in Hz */
                        if (npar >= 1)
                                bell_pitch = par[1];
                        else
                                bell_pitch = DEFAULT_BELL_PITCH;
                        break;
                case 11: /* set bell duration in msec */
                        if (npar >= 1)
                                bell_duration = (par[1] < 2000) ?
                                        par[1]*HZ/1000 : 0;
                        else
                                bell_duration = DEFAULT_BELL_DURATION;
                        break;
                case 12: /* bring specified console to the front */
                        if (par[1] >= 1 && vc_cons_allocated(par[1]-1))
                                set_console(par[1] - 1);
                        break;
                case 13: /* unblank the screen */
                        poke_blanked_console();
                        break;
                case 14: /* set vesa powerdown interval */
                        vesa_off_interval = ((par[1] < 60) ? par[1] : 60) * 60 * HZ;
                        break;
        }
}

/*
 * ICH - INSERT CHARACTER [VT220]
 */
static void vte_ich(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;	

        if (nr > video_num_columns - x)
                nr = video_num_columns - x;
        else if (!nr)
                nr = 1;
        insert_char(vc, nr);
}

/*
 * IL - INSERT LINE
 */
static void vte_il(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;	

        if (nr > video_num_lines - y)
                nr = video_num_lines - y;
        else if (!nr)
                nr = 1;
        insert_line(vc, nr);
}

/*
 * DCH - DELETE CHARACTER
 */
static void vte_dch(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;

        if (nr > video_num_columns - x)
                nr = video_num_columns - x;
        else if (!nr)
                nr = 1;
        delete_char(vc, nr);
}

/*
 * DL - DELETE LINE
 */
static void vte_dl(struct vc_data *vc, unsigned int nr)
{
	int currcons = vc->vc_num;

        if (nr > video_num_lines - y)
                nr = video_num_lines - y;
        else if (!nr)
                nr=1;
        delete_line(vc, nr);
}

/*
 * DECSC - SAVE CURSOR
 *
 * This saves the following states:
 *  - cursor position
 *  - graphic rendition
 *  - character set shift state
 *  - state of wrap flag
 *  - state of origin mode
 *  - state of selective erase (not implemented)
 */
static void vte_decsc(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        saved_x         = x;
        saved_y         = y;
        s_intensity     = intensity;
        s_underline     = underline;
        s_blink         = blink;
        s_reverse       = reverse;
        s_charset       = charset;
        s_color         = color;
        saved_G0        = G0_charset;
        saved_G1        = G1_charset;
#ifdef CONFIG_VT_EXTENDED
        saved_G2        = G2_charset;
        saved_G3        = G3_charset;
#endif /* def CONFIG_VT_EXTENDED */
}

/*
 * DECRC - RESTORE CURSOR
 */
static void vte_decrc(struct vc_data *vc)
{
	int currcons = vc->vc_num;

        gotoxy(vc, saved_x, saved_y);
        intensity       = s_intensity;
        underline       = s_underline;
        blink           = s_blink;
        reverse         = s_reverse;
        charset         = s_charset;
        color           = s_color;
        G0_charset      = saved_G0;
        G1_charset      = saved_G1;
#ifdef CONFIG_VT_EXTENDED
        G2_charset      = saved_G2;
        G3_charset      = saved_G3;
#endif /* ndef CONFIG_VT_EXTENDED */
        translate       = set_translate(charset ? G1_charset : G0_charset,cons_num);
        update_attr(vc);
        need_wrap = 0;
}

/*
 * RIS - RESET TO INITIAL STATE
 *
 * On DEC terminals this causes the following:
 *  - all set-up parameters are replaced by power-up defaults
 *  - all communications lines are disconnected (should we send SIGHUP to
 *    controlling process?)
 *  - all user-defined keys are cleared (not implemented)
 *  - the screen is cleared
 *  - cursor is place to upper-left corner
 *  - SGR state is set to normal
 *  - selective erase attribute write state is set to "non-selective erase"
 *    (not implemented)
 *  - all character sets are set to default (not implemented)
 */
static void vte_ris(struct vc_data *vc, int do_clear)
{
	int currcons = vc->vc_num;

        top             = 0;
        bottom          = video_num_lines;
        vc_state        = ESinit;
        priv1           = 0;
        priv2           = 0;
        priv3           = 0;
        priv4           = 0;
        translate       = set_translate(LAT1_MAP, cons_num);
        G0_charset      = LAT1_MAP;
        G1_charset      = GRAF_MAP;
        charset         = 0;
        need_wrap       = 0;
        report_mouse    = 0;
        utf             = 0;
        utf_count       = 0;

        disp_ctrl       = 0;
        toggle_meta     = 0;

#ifdef CONFIG_VT_EXTENDED
        c8bit           = 0;    /* disable 8-bit controls */
#endif
        decckm          = 0;    /* cursor key sequences */
        decsclm         = 0;    /* jump scroll */
        decscnm         = 0;    /* normal screen */
        decom           = 0;    /* absolute adressing */
        decawm          = 1;    /* autowrap disabled */
        decarm          = 1;    /* autorepeat enabled */
        dectcem         = 1;    /* text cursor enabled */

        kam             = 0;    /* keyboard enabled */
        crm             = 0;    /* execute control functions */
        irm             = 0;    /* replace */
        lnm             = 0;    /* line feed */

        set_kbd(VC_REPEAT);
        clr_kbd(VC_CKMODE);
        clr_kbd(VC_APPLIC);
        clr_kbd(VC_CRLF);
        kbd_table[currcons].lockstate = 0;
        kbd_table[currcons].slockstate = 0;
        kbd_table[currcons].ledmode = LED_SHOW_FLAGS;
        kbd_table[currcons].ledflagstate = kbd_table[currcons].default_ledflagstate;
        set_leds();

        cursor_type = CUR_DEFAULT;
        complement_mask = s_complement_mask;

        default_attr(vc);
        update_attr(vc);

        tab_stop[0]     = 0x01010100;
        tab_stop[1]     =
        tab_stop[2]     =
        tab_stop[3]     =
        tab_stop[4]     = 0x01010101;

        bell_pitch = DEFAULT_BELL_PITCH;
        bell_duration = DEFAULT_BELL_DURATION;

        gotoxy(vc, 0, 0);
        vte_decsc(vc);
        if (do_clear)
            vte_ed(vc, 2);
}

/*
 * TABULATION CLEAR (TBC)
 *
 * NOTE:
 * In case of parameters 0 and 2 the number of lines affected depends on the
 * setting of the Tabulation Stop Mode (TSM).  Since we don't implement TSM,
 * this is silently ignored.
 *
 * Parameters 1 and 4 are similiar to 0 and 3, but affect only line tabulation
 * stops, which are not implemented.
 *
 * Parameter 2 may only be interpreted, when we implement a tabulation stop map
 * per display line.
 */
static void vte_tbc(struct vc_data *vc, int vpar)
{
	int currcons = vc->vc_num;

        switch (vpar) {
        case 0:
                /*
                 * The character tabulation stop at the active
                 * presentation position is cleared.
                 */
                tab_stop[x >> 5] &= ~(1 << (x & 31));
                return;
#if 0
        case 2:
                /*
                 * All character tabulation stops in the active
                 * line are cleared.
                 */
#endif
        case 3:
                /*
                 * All character tabulation stops are cleared.
                 */
        case 5:
                /*
                 * All tabulation stops are cleared.
                 */
                tab_stop[0] = tab_stop[1] = tab_stop[2] = tab_stop[3] =
                        tab_stop[4] = 0;
        }
}

static void do_con_trol(struct tty_struct *tty, struct vc_data *vc, int c)
{
	int currcons = vc->vc_num;

        /*
         * C0 CONTROL CHARACTERS
         *
         * NOTE: Control characters can be used in the _middle_
         *       of an escape sequence.  (XXX: Really? Test!)
         */
        switch (c) {
        case 0x00:      /* NUL - Null */
        case 0x01:      /* SOH - Start of header */
        case 0x02:      /* STX - */
        case 0x03:      /* ETX - */
        case 0x04:      /* EOT - End of transmission */
                return;
        case 0x05:      /* ENQ - Enquiry */
                vte_answerback(tty);
                return;
        case 0x06:      /* ACK - Acknowledge */
                return;
        case 0x07:      /* BEL - Bell */
                if (bell_duration)
                        kd_mksound(bell_pitch, bell_duration);
                return;
        case 0x08:      /* BS - Back space */
                vte_bs(vc);
                return;
        case 0x09:      /* HT - Character tabulation */
                pos -= (x << 1);
                while (x < video_num_columns - 1) {
                        x++;
                        if (tab_stop[x >> 5] & (1 << (x & 31)))
                                break;
                }
                pos += (x << 1);
                return;
        case 0x0a:      /* LF - Line feed */
        case 0x0b:      /* VT - Line tabulation */
                /*
                 * Since line tabulation is not implemented in the DEC VT
                 * series (except VT131 ?),  the DEC VT series treats any
                 * VT as LF.
                 */
        case 0x0c:      /* FF - Form feed */
                /*
                 * DEC VT series processes FF as LF.
                 */
                vte_lf(vc);
                if (!is_kbd(VC_CRLF))
                        return;
        case 0x0d:      /* CR - Carriage return */
                vte_cr(vc);
                return;
        case 0x0e:      /* SO - Shift out / LS1 - Locking shift 1 */
                charset = 1;
                translate = set_translate(G1_charset, cons_num);
                disp_ctrl = 1;
                return;
        case 0x0f:      /* SI - Shift in / LS0 - Locking shift 0 */
                charset = 0;
                translate = set_translate(G0_charset, cons_num);
                disp_ctrl = 0;
                return;
        case 0x10:      /* DLE - */
        case 0x11:      /* DC1 - Device control 1 */
        case 0x12:      /* DC2 - Device control 1 */
        case 0x13:      /* DC3 - Device control 1 */
        case 0x14:      /* DC4 - Device control 1 */
        case 0x15:      /* NAK - Negative acknowledge */
        case 0x16:      /* SYN - Synchronize */
        case 0x17:      /* ETB - */
                return;
        case 0x18:      /* CAN - Cancel */
                vc_state = ESinit;
                return;
        case 0x19:      /* EM - */
                return;
        case 0x1a:      /* SUB - Substitute */
                vc_state = ESinit;
                return;
        case 0x1b:      /* ESC - Escape */
                vc_state = ESesc;
                return;
        case 0x1c:      /* IS4 - */
        case 0x1d:      /* IS3 - */
        case 0x1e:      /* IS2 - */
        case 0x1f:      /* IS1 - */
          return;
        case 0x7f:      /* DEL - Delete */
                /*
                 * This character is ignored, unless a 96-set has been mapped,
                 * but this is not supported at the moment.
                 */
                return;
        }

#ifdef CONFIG_VT_EXTENDED
        if (c8bit == 1)
                /*
                 * C1 control functions (8-bit mode).
                 */
                switch (c) {
                case 0x80:      /* unused */
                case 0x81:      /* unused */
                case 0x82:      /* BPH - Break permitted here */
                case 0x83:      /* NBH - No break here */
                        return;
                case 0x84:      /* IND - Line feed (DEC only) */
#ifndef VTE_STRICT_ISO
                        vte_lf(vc);
#endif /* ndef VTE_STRICT_ISO */
                        return;
                case 0x85:      /* NEL - Next line */
                        vte_lf(vc);
                        vte_cr(vc);
                        return;
                case 0x86:      /* SSA - Start of selected area */
                case 0x87:      /* ESA - End of selected area */
                        return;
                case 0x88:      /* HTS - Character tabulation set */
                        tab_stop[x >> 5] |= (1 << (x & 31));
                        return;
                case 0x89:      /* HTJ - Character tabulation with justify */
                case 0x8a:      /* VTS - Line tabulation set */
                case 0x8b:      /* PLD - Partial line down */
                case 0x8c:      /* PLU - Partial line up */
                        return;
                case 0x8d:      /* RI - Reverse line feed */
                        vte_ri(vc);
                        return;
#if 0
                case 0x8e:      /* SS2 - Single shift 2 */
                        need_shift = 1;
                        GS_charset = G2_charset; /* G2 -> GS */
                        return;
                case 0x8f:      /* SS3 - Single shift 3 */
                        need_shift = 1;
                        GS_charset = G3_charset; /* G3 -> GS */
                        return;
#endif
                case 0x90:      /* DCS - Device control string */
                        return;
                case 0x91:      /* PU1 - Private use 1 */
                case 0x92:      /* PU2 - Private use 2 */
                case 0x93:      /* STS - Set transmit state*/
                case 0x94:      /* CCH - Cancel character */
                case 0x95:      /* MW  - Message waiting */
                case 0x96:      /* SPA - Start of guarded area */
                case 0x97:      /* EPA - End of guarded area */
                case 0x98:      /* SOS - Start of string */
                case 0x99:      /* unused */
                        return;
                case 0x9a:      /* SCI - Single character introducer */
#ifndef VTE_STRICT_ISO
                        vte_da(vc, tty);
#endif /* ndef VTE_STRICT_ISO */
                        return;
                case 0x9b:      /* CSI - Control sequence introducer */
                        vc_state = EScsi;
                        return;
                case 0x9c:      /* ST  - String Terminator */
                case 0x9d:      /* OSC - Operating system command */
                case 0x9e:      /* PM  - Privacy message */
                case 0x9f:      /* APC - Application program command */
                        return;
        }
#endif /* CONFIG_VT_EXTENDED */

        switch(vc_state) {
        case ESesc:
                vc_state = ESinit;
                switch (c) {

#ifdef CONFIG_VT_EXTENDED
                case ' ':       /* ACS - Announce code structure */
                        vc_state = ESacs;
                        return;
#endif /* CONFIG_VT_EXTENDED */
                case '#':       /* SCF - Single control functions */
                        vc_state = ESscf;
                        return;
                case '%':       /* DOCS - Designate other coding system */
                        vc_state = ESdocs;
                        return;
#ifdef CONFIG_VT_HP
                case '&':       /* HP terminal emulation */
                        vc_state = ESesc_and;
                        return;
#endif /* def CONFIG_VT_HP */
                case '(':       /* GZD4 - G0-designate 94-set */
                        vc_state = ESgzd4;
                        return;
                case ')':       /* G1D4 - G1-designate 94-set */
                        vc_state = ESg1d4;
                        return;
#ifdef CONFIG_VT_EXTENDED
#if 0
                case '*':       /* G2D4 - G2-designate 94-set */
                        vc_state = ESg2d4;
                        return;
                case '+':       /* G3D4 - G3-designate 94-set */
                        vc_state = ESg3d4;
                        return;
                case '-':       /* G1D6 - G1-designate 96-set */
                        vc_state = ESg1d6;
                        return;
                case '.':       /* G2D6 - G2-designate 96-set */
                        vc_state = ESg2d6;
                        return;
                case '/':       /* G3D6 - G3-designate 96-set */
                        vc_state = ESg3d6;
                        return;
#endif
#endif /* def CONFIG_VT_EXTENDED */

                        /* ===== Private control functions ===== */

#ifdef CONFIG_VT_EXTENDED
                case '6':       /* DECBI - Back index */
                        return;
#endif /* def CONFIG_VT_EXTENDED */
                case '7':       /* DECSC - Save cursor */
                        vte_decsc(vc);
                        return;
                case '8':       /* DECRC - Restore cursor */
                        vte_decrc(vc);
                        return;
#ifdef CONFIG_VT_EXTENDED
                case '9':       /* DECFI - Forward index */
                        return;
#endif /* def CONFIG_VT_EXTENDED */
                case '=':       /* DECKPAM - Keypad application mode */
                        decnkm = 1;
                        set_kbd(VC_APPLIC);
                        return;
                case '>':       /* DECKPNM - Keypad numeric mode */
                        decnkm = 0;
                        clr_kbd(VC_APPLIC);
                        return;

                        /* ===== C1 control functions ===== */

                case '@': /* unallocated */
                case 'A': /* unallocated */
                case 'B': /* BPH - Break permitted here */
                case 'C': /* NBH - No break here */
                case 'D': /* IND - Line feed (DEC only) */
#ifndef VTE_STRICT_ISO
                        vte_lf(vc_cons[currcons].d);
#endif /* ndef VTE_STRICT_ISO */
                        return;
                case 'E': /* NEL - Next line */
                        vte_cr(vc);
                        vte_lf(vc);
                        return;
                case 'F': /* SSA - Start of selected area */
                case 'G': /* ESA - End of selected area */
                        return;
                case 'H': /* HTS - Character tabulation set */
                        tab_stop[x >> 5] |= (1 << (x & 31));
                        return;
                case 'I': /* HTJ - Character tabulation with justify */
                case 'J': /* VTS - Line tabulation set */
                case 'K': /* PLD - Partial line down */
                case 'L': /* PLU - Partial line up */
                        return;
                case 'M': /* RI - Reverse line feed */
                        vte_ri(vc);
                        return;
#ifdef CONFIG_VT_EXTENDED
                case 'N': /* SS2 - Single shift 2 */
                        shift = 1;
                        GS_charset = G2_charset; /* G2 -> GS */
                        return;
                case 'O': /* SS3 - Single shift 3 */
                        shift = 1;
                        GS_charset = G3_charset;
                        return;
#endif /* def VTE_STRICT_ISO */
                case 'P': /* DCS - Device control string */
                        return;
                case 'Q': /* PU1 - Private use 1 */
                case 'R': /* PU2 - Private use 2 */
                case 'S': /* STS - Set transmit state */
                case 'T': /* CCH - Cancel character */
                case 'U': /* MW - Message waiting */
                case 'V': /* SPA - Start of guarded area */
                case 'W': /* EPA - End of guarded area */
                case 'X': /* SOS - Start of string */
                case 'Y': /* unallocated */
                        return;
                case 'Z': /* SCI - Single character introducer */
#ifndef VTE_STRICT_ISO
                        vte_da(vc, tty);
#endif /* ndef VTE_STRICT_ISO */
                        return;
                case '[':       /* CSI - Control sequence introducer */
                        vc_state = EScsi;
                        return;
                case '\\':      /* ST  - String Terminator */
                        return;
                case ']':       /* OSC - Operating system command */
                        /* XXX: Fixme! Wrong sequence and format! */
                        vc_state = ESosc;
                        return;
                case '^':       /* PM  - Privacy Message */
                case '_':       /* APC - Application Program Command */
                        return;

                        /* ===== Single control functions ===== */

#ifdef CONFIG_VT_EXTENDED
                case '`':       /* DMI - Disable manual input */
                        kam = 0;
                        return;
                case 'b':       /* EMI - Enable manual input */
                        kam = 1;
                        return;
#endif /* def CONFIG_VT_EXTENDED */
                case 'c':       /* RIS - Reset ti initial state */
                        vte_ris(vc, 1);
                        return;
#ifdef CONFIG_VT_EXTENDED
                case 'd':       /* CMD - Coding Method Delimiter */
                        return;
#if 0
                case 'n':       /* LS2 - Locking shift G2 */
                        GL_charset = G2_charset; /*  (G2 -> GL) */
                        return;
                case 'o':       /* LS3 - Locking shift G3 */
                        GL_charset = G3_charset; /*  (G3 -> GL) */
                        return;
                case '|':       /* LS3R - Locking shift G3 right */
                        GR_charset = G3_charset; /* G3 -> GR */
                        return;
                case '}':       /* LS2R - Locking shift G2 right */
                        GR_charset = G2_charset; /* G2 -> GR */
                        return;
                case '~':       /* LS1R - Locking shift G1 right */
                        GR_charset = G1_charset; /* G1 -> GR */
                        return;
#endif
#endif /* def CONFIG_VT_EXTENDED */
                }
                return;
#ifdef CONFIG_VT_EXTENDED
        case ESacs:
                vc_state = ESinit;
                switch (c) {
                case 'F':       /* Select 7-bit C1 control transmission */
                        if (decscl != 1) /* Ignore if in VT100 mode */
                                c8bit = 0;
                        return;
                case 'G':       /* Select 8-Bit C1 control transmission */
                        if (decscl != 1) /* Ignore if in VT100 mode */
                                c8bit = 1;
                        return;
                case 'L':       /* ANSI conformance level 1 */
                case 'M':       /* ANSI conformance level 2 */
                case 'N':       /* ANSI conformance level 3 */
                        /* Not yet implemented. */
                        return;
                }
                return;
#endif /* def CONFIG_VT_EXTENDED */
        case ESosc:
                vc_state = ESinit;
                switch (c) {
                case 'P':       /* palette escape sequence */
                        for (npar = 0; npar < NPAR; npar++)
                                par[npar] = 0;
                        npar = 0;
                        vc_state = ESpalette;
                        return;
                case 'R':       /* reset palette */
                        reset_palette(vc);
                        vc_state = ESinit;
                        return;
                }
                return;
        case ESpalette:
                if ( (c>='0'&&c<='9') || (c>='A'&&c<='F') || (c>='a'&&c<='f') ) {
                        par[npar++] = (c>'9' ? (c&0xDF)-'A'+10 : c-'0') ;
                        if (npar==7) {
                                int i = par[0]*3, j = 1;
                                palette[i] = 16*par[j++];
                                palette[i++] += par[j++];
                                palette[i] = 16*par[j++];
                                palette[i++] += par[j++];
                                palette[i] = 16*par[j++];
                                palette[i] += par[j];
                                set_palette(vc);
                                vc_state = ESinit;
                        }
                } else
                        vc_state = ESinit;
                return;
        case EScsi:
                for(npar = 0 ; npar < NPAR ; npar++)
                        par[npar] = 0;
                npar = 0;
                vc_state = EScsi_getpars;
                if (c == '[') {
                        /* Function key */
                        vc_state = ESfunckey;
                        return;
                }
                priv1 = (c == '<');
                priv2 = (c == '=');
                priv3 = (c == '>');
                priv4 = (c == '?');
                if (priv1) {
                        vc_state = ESinit;
                        return;
                }
                if (priv2 || priv3 || priv4) {
                        return;
                }
        case EScsi_getpars:
                if (c==';' && npar<NPAR-1) {
                        npar++;
                        return;
                } else if (c>='0' && c<='9') {
                        par[npar] *= 10;
                        par[npar] += c-'0';
                        return;
                } else vc_state=EScsi_gotpars;
        case EScsi_gotpars:
                vc_state = ESinit;
                /*
                 * Process control functions  with private parameter flag.
                 */
                switch(c) {
#ifdef CONFIG_VT_EXTENDED
                case '$':
                        if (priv4) {
                                vc_state = EScsi_dollar;
                                return;
                        }
                        break;
                case 'J':
                        if (priv4) {
                                /* DECSED - Selective erase in display */
                                return;
                        }
                        break;
                case 'K':
                        if (priv4) {
                                /* DECSEL - Selective erase in display */
                                return;
                        }
                        break;
#endif /* def CONFIG_VT_EXTENDED */
                case 'h':       /* SM - Set Mode */
                        set_mode(vc, 1);
                        return;
                case 'l':       /* RM - Reset Mode */
                        set_mode(vc, 0);
                        return;
                case 'c':
                        if (priv2) {
                                if (!par[0])
                                        vte_dec_da3(vc, tty);
                                priv2 = 0;
                                return;
                        }
                        if (priv3) {
                                if (!par[0])
                                        vte_dec_da2(vc, tty);
                                priv3 = 0;
                                return;
                        }
                        if (priv4) {
                                if (par[0])
                                        cursor_type = par[0] | (par[1]<<8) | (par[2]<<16);
                                else
                                        cursor_type = CUR_DEFAULT;
                                priv4 = 0;
                                return;
                        }
                        break;
                case 'm':
                        if (priv4) {
                                clear_selection();
                                if (par[0])
                                        complement_mask = par[0]<<8 | par[1];
                                else
                                        complement_mask = s_complement_mask;
                                priv4 = 0;
                                return;
                        }
                        break;
                case 'n':
#ifdef CONFIG_VT_EXTENDED
                        if (priv4) {
                                switch (par[0]) {
                                case 6: /* DECXCPR - Extended CPR */
                                        vte_cpr(vc, tty, 1);
                                        break;
                                case 15:        /* DEC printer status */
                                        vte_fake_dec_dsr(vc, tty, "13");
                                        break;
                                case 25:        /* DEC UDK status */
                                        vte_fake_dec_dsr(vc, tty, "21");
                                        break;
                                case 26:        /* DEC keyboard status */
                                        vte_fake_dec_dsr(vc, tty, "27;1;0;1");
                                        break;
                                case 53:        /* DEC locator status */
                                        vte_fake_dec_dsr(vc, tty, "53");
                                        break;
                                case 62:        /* DEC macro space */
                                        vte_decmsr(vc, tty);
                                        break;
                                case 75:        /* DEC data integrity */
                                        vte_fake_dec_dsr(vc, tty, "70");
                                        break;
                                case 85:        /* DEC multiple session status */
                                        vte_fake_dec_dsr(vc, tty, "83");
                                        break;
                                }
                        } else
#endif /* CONFIG_VT_EXTENDED */
                                switch (par[0]) {
                                case 5: /* DSR - Device status report */
                                        vte_dsr(vc, tty);
                                        break;
                                case 6: /* CPR - Cursor position report */
                                        vte_cpr(vc,tty,0);
                                        break;
                                }
                        priv4 = 0;
                        return;
                }
                if (priv1 || priv2 || priv3 || priv4) {
                        priv1 =
                        priv2 =
                        priv3 =
                        priv4 = 0;
                        return;
                }
                /*
                 * Process control functions with standard parameter strings.
                 */
                switch(c) {

                        /* ===== Control functions w/ intermediate byte ===== */

#ifdef CONFIG_VT_EXTENDED
                case ' ':       /* Intermediate byte: SP (ISO 6429) */
                        vc_state = EScsi_space;
                        return;
                case '!':       /* Intermediate byte: ! (DEC VT series) */
                        vc_state = EScsi_exclam;
                        return;
                case '"':       /* Intermediate byte: " (DEC VT series) */
                        vc_state = EScsi_dquote;
                        return;
                case '$':       /* Intermediate byte: $ (DEC VT series) */
                        vc_state = EScsi_dollar;
                        return;
                case '&':       /* Intermediate byte: & (DEC VT series) */
                        vc_state = EScsi_and;
                        return;
                case '*':       /* Intermediate byte: * (DEC VT series) */
                        vc_state = EScsi_star;
                        return;
                case '+':       /* Intermediate byte: + (DEC VT series) */
                        vc_state = EScsi_plus;
                        return;
#endif /* def CONFIG_VT_EXTENDED */

                        /* ===== Control functions w/o intermediate byte ===== */

                case '@':       /* ICH - Insert character */
                        vte_ich(vc, par[0]);
                        return;
                case 'A':       /* CUU - Cursor up */
#ifdef CONFIG_VT_EXTENDED
                case 'k':       /* VPB - Line position backward */
#endif /* def CONFIG_VT_EXTENDED */
                        if (!par[0]) par[0]++;
                        gotoxy(vc, x, y-par[0]);
                        return;
                case 'B':       /* CUD - Cursor down */
                case 'e':       /* VPR - Line position forward */
                        if (!par[0]) par[0]++;
                        gotoxy(vc, x, y+par[0]);
                        return;
                case 'C':       /* CUF - Cursor right */
                case 'a':       /* HPR - Character position forward */
                        if (!par[0]) par[0]++;
                        gotoxy(vc, x+par[0], y);
                        return;
                case 'D':       /* CUB - Cursor left */
#ifdef CONFIG_VT_EXTENDED
                case 'j':       /* HPB - Character position backward */
#endif /* def CONFIG_VT_EXTENDED */
                        if (!par[0]) par[0]++;
                        gotoxy(vc, x-par[0], y);
                        return;
                case 'E':       /* CNL - Cursor next line */
                        if (!par[0]) par[0]++;
                        gotoxy(vc, 0, y+par[0]);
                        return;
                case 'F':       /* CPL - Cursor preceeding line */
                        if (!par[0]) par[0]++;
                        gotoxy(vc, 0, y-par[0]);
                        return;
                case 'G':       /* CHA - Cursor character absolute */
                case '`':       /* HPA - Character position absolute */
                        if (par[0]) par[0]--;
                        gotoxy(vc, par[0], y);
                        return;
                case 'H':       /* CUP - Cursor position */
                case 'f':       /* HVP - Horizontal and vertical position */
                        if (par[0]) par[0]--;
                        if (par[1]) par[1]--;
                        gotoxay(vc, par[1], par[0]);
                        return;
#ifdef CONFIG_VT_EXTENDED
                case 'I':       /* CHT - Cursor forward tabulation */
                        if (!par[0])
                                par[0]++;
                        vte_cht(vc, par[0]);
                        return;
#endif /* def CONFIG_VT_EXTENDED */
                case 'J':       /* ED - Erase in page */
                        vte_ed(vc, par[0]);
                        return;
                case 'K':       /* EL - Erase in line */
                        vte_el(vc, par[0]);
                        return;
                case 'L':       /* IL - Insert line */
                        vte_il(vc, par[0]);
                        return;
                case 'M':       /* DL - Delete line */
                        vte_dl(vc, par[0]);
                        return;
                case 'P':       /* DCH - Delete character */
                        vte_dch(vc, par[0]);
                        return;
#ifdef CONFIG_VT_EXTENDED
                case 'U':       /* NP - Next page */
                case 'V':       /* PP - Preceeding page */
                        return;
                case 'W':       /* CTC - Cursor tabulation control */
                        switch (par[0]) {
                        case 0: /* Set character tab stop at current position */
                                tab_stop[x >> 5] |= (1 << (x & 31));
                                return;
                        case 2: /* Clear character tab stop at curr. position */
                                vte_tbc(vc, 0);
                                return;
                        case 5: /* All character tab stops are cleared. */
                                vte_tbc(vc, 5);
                                return;
                        }
                        return;
#endif /* def CONFIG_VT_EXTENDED */
                case 'X':       /* ECH - Erase character */
                        vte_ech(vc, par[0]);
                        return;
#ifdef CONFIG_VT_EXTENDED
                case 'Y':       /* CVT - Cursor line tabulation */
                        if (!par[0])
                                par[0]++;
                        vte_cvt(vc, par[0]);
                        return;
                case 'Z':       /* CBT - Cursor backward tabulation */
                        vte_cbt(vc, par[0]);
                        return;
#endif /* def CONFIG_VT_EXTENDED */
                case ']':
#ifndef VT_STRICT_ISO
                        setterm_command(vc);
#endif /* def VT_STRICT_ISO */
                        return;
                case 'c':       /* DA - Device attribute */
                        if (!par[0])
                                vte_da(vc, tty);
                        return;
                case 'd':       /* VPA - Line position absolute */
                        if (par[0]) par[0]--;
                        gotoxay(vc, x, par[0]);
                        return;
                case 'g':       /* TBC - Tabulation clear */
                        vte_tbc(vc, par[0]);
                        return;
                case 'm':       /* SGR - Select graphics rendition */
                        vte_sgr(vc);
                        return;

                        /* ===== Private control sequences ===== */

                case 'q': /* DECLL - but only 3 leds */
                        switch (par[0]) {
                        case 0: /* all LEDs off */
                        case 1: /* LED 1 on */
                        case 2: /* LED 2 on */
                        case 3: /* LED 3 on */
                                setledstate(kbd_table + cons_num,
                                            (par[0] < 3) ? par[0] : 4);
                        case 4: /* LED 4 on */
                        }
                        return;
                case 'r':       /* DECSTBM - Set top and bottom margin */
                        if (!par[0])
                                par[0]++;
                        if (!par[1])
                                par[1] = video_num_lines;
                        /* Minimum allowed region is 2 lines */
                        if (par[0] < par[1] &&
                            par[1] <= video_num_lines) {
                                top=par[0]-1;
                                bottom=par[1];
                                gotoxay(vc, 0, 0);
                        }
                        return;
#ifndef CONFIG_VT_EXTENDED
                case 's':       /* DECSC - Save cursor */
                        vte_decsc(vc);
                        return;
                case 'u':       /* DECRC - Restore cursor */
                        vte_decrc(vc);
                        return;
#else
                case 's':       /* DECSLRM - Set left and right margin */
                        return;
                case 't':       /* DECSLPP - Set lines per page */
                        return;
                case 'x':       /* DECREQTPARM - Request terminal parameters */
                        vte_decreptparm(vc, tty);
                        return;
                case 'y':
                        if (par[0] == 4) {
                                /* DECTST - Invoke confidence test */
                                return;
                        }
#endif /* CONFIG_VT_EXTENDED */
                }
                return;
#if CONFIG_VT_EXTENDED
        case EScsi_space:
                vc_state = ESinit;
                switch (c) {
                        /*
                         * Note: All codes betweem 0x40 and 0x6f are subject to
                         * standardisation by the ISO. The codes netweem 0x70
                         * and 0x7f are free for private use.
                         */
                        case '@':       /* SL - Scroll left */
                        case 'A':       /* SR - Scroll right */
                                return;
                        case 'P':       /* PPA - Page position absolute */
                        case 'Q':       /* PPR - Page position forward */
                        case 'R':       /* PPB - Page position backward */
                                return;
                }
                return;
        case EScsi_exclam:
                vc_state = ESinit;
                switch (c) {
                        case 'p':       /* DECSTR - Soft terminal reset */
                                /*
                                 * Note: On a true DEC VT there are differences
                                 * between RIS and DECSTR. Right now we ignore
                                 * this... -dbk
                                 */
                                vte_ris(vc, 1);
                                return;
                }
                return;
        case EScsi_dquote:
                vc_state = ESinit;
                switch (c) {
                        case 'p':       /* DECSCL - Set operating level */
                                vte_decscl(vc);
                                return;
                        case 'q':       /* DECSCA - Select character protection attribute */
                                return;
                        case 'v':       /* DECRQDE - Request window report */
                }
                return;
        case EScsi_dollar:
                vc_state = ESinit;
                switch (c) {
                        case 'p':       /* DECRQM - Request mode */
                                vte_decrqm(vc, tty, priv4);
                                return;
                        case 'r':       /* DECCARA - Change attributes in rectangular area */
                                return;
                        case 't':       /* DECRARA - Reverse attributes in rectangular area */
                                return;
                        case 'u':       /* DECRQTSR - Request terminal state */
                                if (par[0] == 1)
                                        vte_dectsr(tty);
                                return;
                        case 'v':       /* DECCRA - Copy rectangular area */
                                return;
                        case 'w':       /* DECRQPSR - Request presentation status */
                                switch (par[0]) {
                                        case 1:
                                                vte_deccir(tty);
                                                break;
                                        case 2:
                                                vte_dectabsr(tty);
                                                break;
                                }
                                return;
                        case 'x':       /* DECFRA - Fill rectangular area */
                                return;
                        case 'z':       /* DECERA - Erase rectangular area */
                                return;
                        case '{':       /* DECSERA - Selective erase rectangular area */
                                return;
                        case '|':       /* DECSCPP - Set columns per page */
                                return;
                        case '}':       /* DECSASD - Select active status  display */
                                return;
                        case '~':       /* DECSSDT - Select status display type */
                                return;
                }
                return;
        case EScsi_and:
                vc_state = ESinit;
                switch (c) {
                        case 'u':       /* DECRQUPSS - Request user-preferred supplemental set */
                                return;
                        case 'x':       /* Enable Session Command */
                                return;
                }
                return;
        case EScsi_squote:
                vc_state = ESinit;
                switch (c) {
                        case '}':       /* DECIC - Insert column */
                                return;
                        case '~':       /* DECDC - Delete column */
                                return;
                }
        case EScsi_star:
                vc_state = ESinit;
                switch (c) {
                        case 'x':       /* DECSACE - Select attribute change extent */
                                return;
                        case 'y':       /* DECRQCRA - Request checksum on rectangular area */
                                return;
                        case 'z':       /* DECINVM - Invoke macro */
                                return;
                        case '|':       /* DECSNLS - Select number of lines */
                                return;
                        case '}':       /* DECLFKC - Local function key control */
                                return;
                }
                return;
        case EScsi_plus:
                vc_state = ESinit;
                switch (c) {
                        case 'p':       /* DECSR - Secure reset */
                                return;
                }
                return;
#endif /* CONFIG_VT_EXTENDED */
        case ESdocs:
                vc_state = ESinit;
                switch (c) {
                case '@':  /* defined in ISO 2022 */
                        utf = 0;
                        return;
                case 'G':  /* prelim official escape code */
                case '8':  /* retained for compatibility */
                        utf = 1;
                        return;
                }
                return;
#ifdef CONFIG_VT_HP
        case ESesc_and:
                vc_state = ESinit;
                switch (c) {
                        case 'f':       /* Set function key label */
                                return;
                        case 'j':       /* Display function key labels */
                                return;
                }
                return;
#endif
        case ESfunckey:
                vc_state = ESinit;
                return;
        case ESscf:
                vc_state = ESinit;
                if (c == '8') {
                        /* DEC screen alignment test. kludge :-) */
                        video_erase_char =
                                (video_erase_char & 0xff00) | 'E';
                        vte_ed(vc, 2);
                        video_erase_char =
                                (video_erase_char & 0xff00) | ' ';
                        do_update_region(vc, origin, screenbuf_size/2);
                }
                return;
        case ESgzd4:
                switch (c) {
                case '0':       /* DEC Special graphics */
                        G0_charset = GRAF_MAP;
                        break;
#if 0
                case '>':       /* DEC Technical */
                        G0_charset = DEC_TECH_MAP;
                        break;
#endif
                case 'A':       /* ISO Latin-1 supplemental */
                        G0_charset = LAT1_MAP;
                        break;
                case 'B':       /* ASCII */
                        G0_charset = LAT1_MAP;
                        break;
                case 'U':
                        G0_charset = IBMPC_MAP;
                        break;
                case 'K':
                        G0_charset = USER_MAP;
                        break;
                }
                if (charset == 0)
                        translate = set_translate(G0_charset, cons_num);
                vc_state = ESinit;
                return;
        case ESg1d4:
                switch (c) {
                case '0':       /* DEC Special graphics */
                        G1_charset = GRAF_MAP;
                        break;
#if 0
                case '>':       /* DEC Technical */
                        G1_charset = DEC_TECH_MAP;
                        break;
#endif
                case 'A':       /* Latin-1 supplemental */
                        G1_charset = LAT1_MAP;
                        break;
                case 'B':       /* ASCII */
                        G1_charset = LAT1_MAP;
                        break;
                case 'U':
                        G1_charset = IBMPC_MAP;
                        break;
                case 'K':
                        G1_charset = USER_MAP;
                        break;
                }
                if (charset == 1)
                        translate = set_translate(G1_charset, cons_num);
                vc_state = ESinit;
                return;
#ifdef CONFIG_VT_EXTENDED
        case ESg2d4:
                switch (c) {
                case '0':       /* DEC Special graphics */
                        G2_charset = GRAF_MAP;
                        break;
#if 0
                case '>':       /* DEC Technical */
                        G2_charset = DEC_TECH_MAP;
                        break;
#endif
                case 'A':       /* ISO Latin-1 supplemental */
                        G2_charset = LAT1_MAP;
                        break;
                case 'B':       /* ASCII */
                        G2_charset = LAT1_MAP;
                        break;
                case 'U':
                        G2_charset = IBMPC_MAP;
                        break;
                case 'K':
                        G2_charset = USER_MAP;
                        break;
                }
                if (charset == 1)
                        translate = set_translate(G2_charset, cons_num);
                vc_state = ESinit;
                return;
        case ESg3d4:
                switch (c) {
                case '0':       /* DEC Special graphics */
                        G3_charset = GRAF_MAP;
                        break;
#if 0
                case '>':       /* DEC Technical */
                        G3_charset = DEC_TECH_MAP;
                        break;
#endif
                case 'A':       /* ISO Latin-1 supplemental */
                        G3_charset = LAT1_MAP;
                        break;
                case 'B':       /* ASCII */
                        G3_charset = LAT1_MAP;
                        break;
                case 'U':
                        G3_charset = IBMPC_MAP;
                        break;
                case 'K':
                        G3_charset = USER_MAP;
                        break;
                }
                if (charset == 1)
                        translate = set_translate(G3_charset, cons_num);
                vc_state = ESinit;
                return;
#endif /* CONFIG_VT_EXTENDED */
        default:
                vc_state = ESinit;
        }
}

/* This is a temporary buffer used to prepare a tty console write
 * so that we can easily avoid touching user space while holding the
 * console spinlock.  It is allocated in vt_console_init and is shared by
 * this code and the vc_screen read/write tty calls.
 *
 * We have to allocate this statically in the kernel data section
 * since console_init (and thus vt_console_init) are called before any
 * kernel memory allocation is available.
 */
char con_buf[PAGE_SIZE];
#define CON_BUF_SIZE    PAGE_SIZE
DECLARE_MUTEX(con_buf_sem);

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
        unsigned int currcons;
        unsigned long draw_from = 0, draw_to = 0;
        struct vt_struct *vt = (struct vt_struct *)tty->driver_data;
	struct vc_data *vc;
        u16 himask, charmask;
        const unsigned char *orig_buf = NULL;
        int orig_count;

        currcons = vt->vc_num;
        if (!vc_cons_allocated(currcons)) {
            /* could this happen? */
            static int error = 0;
            if (!error) {
                error = 1;
                printk("con_write: tty %d not allocated\n", currcons+1);
            }
            return 0;
        }

	vc = vc_cons[currcons].d;
	
        orig_buf = buf;
        orig_count = count;

        if (from_user) {
                down(&con_buf_sem);

again:
                if (count > CON_BUF_SIZE)
                        count = CON_BUF_SIZE;
                if (copy_from_user(con_buf, buf, count)) {
                        n = 0; /* ?? are error codes legal here ?? */
                        goto out;
                }

                buf = con_buf;
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
        if (IS_FG)
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

                if (vc_state == ESinit && ok) {
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
                do_con_trol(tty, vc, c);
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

                up(&con_buf_sem);
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
        /* Runs the task queue outside of the console lock.  These
         * callbacks can come back into the console code and thus
         * will perform their own locking.
         */
        run_task_queue(&con_task_queue);

        spin_lock_irq(&console_lock);

        if (want_console >= 0) {
                if (want_console != fg_console && vc_cons_allocated(want_console)) {
                        hide_cursor(vc_cons[fg_console].d);
                        change_console(want_console);
                        /* we only changed when the console had already
                           been allocated - a new console is not created
                           in an interrupt routine */
                }
                want_console = -1;
        }
        if (do_poke_blanked_console) { /* do not unblank for a LED change */
                do_poke_blanked_console = 0;
                poke_blanked_console();
        }
        if (scrollback_delta) {
                int currcons = fg_console;
                clear_selection();
                if (vcmode == KD_TEXT)
                        sw->con_scrolldelta(vc_cons[currcons].d, scrollback_delta);
                scrollback_delta = 0;
        }

        spin_unlock_irq(&console_lock);
}

/*
 *      Handling of Linux-specific VC ioctls
 */

int tioclinux(struct tty_struct *tty, unsigned long arg)
{
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
                        unblank_screen();
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
                        data = mouse_reporting();
                        return __put_user(data, (char *) arg);
                case 10:
                        set_vesa_blanking(arg);
                        return 0;
                case 11:        /* set kmsg redirect */
                        if (!suser())
                                return -EPERM;
                        if (get_user(data, (char *)arg+1))
                                        return -EFAULT;
                        kmsg_redirect = data;
                        return 0;
                case 12:        /* get fg_console */
                        return fg_console;
        }
        return -EINVAL;
}

/*
 *      /dev/ttyN handling
 */

/*
 * Allocate the console screen memory.
 */
static int con_open(struct tty_struct *tty, struct file * filp)
{
	struct vc_data *vc;
        unsigned int    currcons;
        int i;

        currcons = MINOR(tty->device) - tty->driver.minor_start;

        i = vc_allocate(currcons);
        if (i)
                return i;

        vt_cons[currcons]->vc_num = currcons;
        tty->driver_data = vt_cons[currcons];
	vc = vc_cons[currcons].d;

        if (!tty->winsize.ws_row && !tty->winsize.ws_col) {
                tty->winsize.ws_row = video_num_lines;
                tty->winsize.ws_col = video_num_columns;
        }
        if (tty->count == 1)
                vcs_make_devfs (currcons, 0);
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
        int     retval;

        pm_access(pm_con);
        retval = do_con_write(tty, from_user, buf, count);
        con_flush_chars(tty);

        return retval;
}

static void con_put_char(struct tty_struct *tty, unsigned char ch)
{
        pm_access(pm_con);
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
        struct vt_struct *vt = (struct vt_struct *)tty->driver_data;
	struct vc_data *vc = vc_cons[vt->vc_num].d;
        unsigned long flags;

        pm_access(pm_con);
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
        int console_num;
        if (!tty)
                return;
        console_num = MINOR(tty->device) - (tty->driver.minor_start);
        if (!vc_cons_allocated(console_num))
                return;
        set_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
        set_leds();
}

/*
 * Turn the Scroll-Lock LED off when the console is started
 */
static void con_start(struct tty_struct *tty)
{
        int console_num;
        if (!tty)
                return;
        console_num = MINOR(tty->device) - (tty->driver.minor_start);
        if (!vc_cons_allocated(console_num))
                return;
        clr_vc_kbd_led(kbd_table + console_num, VC_SCROLLOCK);
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
        struct vt_struct *vt = (struct vt_struct *) tty->driver_data;

        wake_up_interruptible(&vt->paste_wait);
}

#ifdef CONFIG_VT_CONSOLE

/*
 *      Console on virtual terminal
 *
 * The console_lock must be held when we get here.
 */

void vt_console_print(struct console *co, const char * b, unsigned count)
{
        int currcons = fg_console;
        unsigned char c;
        static unsigned long printing = 0;
	struct vc_data *vc;
        const ushort *start;
        ushort cnt = 0;
        ushort myx;

        /* console busy or not yet initialized */
        if (!printable || test_and_set_bit(0, &printing))
                return;

        pm_access(pm_con);

        if (kmsg_redirect && vc_cons_allocated(kmsg_redirect - 1))
                currcons = kmsg_redirect - 1;

	vc = vc_cons[currcons].d;

        /* read `x' only after setting currecons properly (otherwise
           the `x' macro will read the x of the foreground console). */
        myx = x;

        if (!vc_cons_allocated(currcons)) {
                /* impossible */
                /* printk("vt_console_print: tty %d not allocated ??\n", currcons+1); */
                goto quit;
        }

        if (vcmode != KD_TEXT)
                goto quit;

        /* undraw cursor first */
        if (IS_FG)
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
        set_cursor(vc);
        poke_blanked_console();

quit:
        clear_bit(0, &printing);
}

static kdev_t vt_console_device(struct console *c)
{
        return MKDEV(TTY_MAJOR, c->index ? c->index : fg_console + 1);
}

struct console vt_console_driver = {
        "tty",
        vt_console_print,
        NULL,
        vt_console_device,
        keyboard_wait_for_keypress,
        unblank_screen,
        NULL,
        CON_PRINTBUFFER,
        -1,
        0,
        NULL
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
        unsigned int currcons = 0;

        if (conswitchp)
                display_desc = conswitchp->con_startup();
        if (!display_desc) {
                fg_console = 0;
                return;
        }

        memset(&console_driver, 0, sizeof(struct tty_driver));
        console_driver.magic = TTY_DRIVER_MAGIC;
        console_driver.name = "vc/%d";
        console_driver.name_base = 1;
        console_driver.major = TTY_MAJOR;
        console_driver.minor_start = 1;
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

        timer_table[BLANK_TIMER].fn = blank_screen;
        timer_table[BLANK_TIMER].expires = 0;
        if (blankinterval) {
                timer_table[BLANK_TIMER].expires = jiffies + blankinterval;
                timer_active |= 1<<BLANK_TIMER;
        }

        /*
         * kmalloc is not running yet - we use the bootmem allocator.
         */
        for (currcons = 0; currcons < MIN_NR_CONSOLES; currcons++) {
                vc_cons[currcons].d = (struct vc_data *)
                                alloc_bootmem(sizeof(struct vc_data));
                vt_cons[currcons] = (struct vt_struct *)
                                alloc_bootmem(sizeof(struct vt_struct));
                visual_init(currcons, 1);
                screenbuf = (unsigned short *) alloc_bootmem(screenbuf_size);
                kmalloced = 0;
                vc_init(currcons, video_num_lines, video_num_columns,
                        currcons || !sw->con_save_screen);
        }
        currcons = fg_console = 0;
        master_display_fg = vc_cons[currcons].d;
        set_origin(vc_cons[currcons].d);
        save_screen(vc_cons[currcons].d);
        gotoxy(vc_cons[currcons].d, x, y);
        vte_ed(vc_cons[currcons].d, 0);
        update_screen(fg_console);
        printk("Console: %s %s %dx%d",
                can_do_color ? "colour" : "mono",
                display_desc, video_num_columns, video_num_lines);
        printable = 1;
        printk("\n");

#ifdef CONFIG_VT_CONSOLE
        register_console(&vt_console_driver);
#endif
        tasklet_enable(&console_tasklet);
        tasklet_schedule(&console_tasklet);
}

#ifndef VT_SINGLE_DRIVER

static void clear_buffer_attributes(int currcons)
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

void take_over_console(struct consw *csw, int first, int last, int deflt)
{
        int i, j = -1;
        const char *desc;

        desc = csw->con_startup();
        if (!desc) return;
        if (deflt)
                conswitchp = csw;

        for (i = first; i <= last; i++) {
                int old_was_color;
                int currcons = i;

                con_driver_map[i] = csw;

                if (!vc_cons[i].d || !vc_cons[i].d->vc_sw)
                        continue;

                j = i;
                if (IS_VISIBLE)
                        save_screen(vc_cons[i].d);
                old_was_color = vc_cons[i].d->vc_can_do_color;
                vc_cons[i].d->vc_sw->con_deinit(vc_cons[i].d);
                visual_init(i, 0);
                update_attr(vc_cons[i].d);

                /* If the console changed between mono <-> color, then
                 * the attributes in the screenbuf will be wrong.  The
                 * following resets all attributes to something sane.
                 */
                if (old_was_color != vc_cons[i].d->vc_can_do_color)
                        clear_buffer_attributes(i);

                if (IS_VISIBLE)
                        update_screen(i);
        }
        printk("Console: switching ");
        if (!deflt)
                printk("consoles %d-%d ", first+1, last+1);
        if (j >= 0)
                printk("to %s %s %dx%d\n",
                       vc_cons[j].d->vc_can_do_color ? "colour" : "mono",
                       desc, vc_cons[j].d->vc_cols, vc_cons[j].d->vc_rows);
        else
                printk("to %s\n", desc);
}

void give_up_console(struct consw *csw)
{
        int i;

        for(i = 0; i < MAX_NR_CONSOLES; i++)
                if (con_driver_map[i] == csw)
                        con_driver_map[i] = NULL;
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
EXPORT_SYMBOL(vc_resize);
EXPORT_SYMBOL(fg_console);
EXPORT_SYMBOL(console_blank_hook);

#ifndef VT_SINGLE_DRIVER
EXPORT_SYMBOL(take_over_console);
EXPORT_SYMBOL(give_up_console);
#endif
