/*
 * $Id$
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Linus Torvalds		Johan Myreen
 *	Christoph Niemann	Risto Kankkunen
 *	Andries Brouwer		Martin Mares
 *	Hamish MacDonald	Geert Uytterhoeven
 *
 *  Sponsored by SuSE
 */

/*
 * Keyboard input for VT's
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/pm.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/vt_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/sysrq.h>
#include <linux/input.h>

extern void ctrl_alt_del(void);

#define SIZE(x)		(sizeof(x)/sizeof((x)[0]))

/*
 * Exported functions/variables
 */

struct pt_regs *kbd_pt_regs;
void compute_shiftstate(void);

/*
 * Handler tables.
 */

#define K_HANDLERS\
	k_self,		k_fn,		k_spec,		k_pad,\
	k_dead,		k_cons,		k_cur,		k_shift,\
	k_meta,		k_ascii,	k_lock,		k_lowercase,\
	k_slock,	k_dead2,	k_ignore,	k_ignore

typedef void (k_handler_fn)(struct tty_struct *tty, unsigned char value, 
			    char up_flag);
static k_handler_fn K_HANDLERS;
static k_handler_fn *k_handler[16] = { K_HANDLERS };

#define FN_HANDLERS\
	fn_null,	fn_enter,	fn_show_ptregs,	fn_show_mem,\
	fn_show_state,	fn_send_intr,	fn_lastcons,	fn_caps_toggle,\
	fn_num,		fn_hold,	fn_scroll_forw,	fn_scroll_back,\
	fn_boot_it,	fn_caps_on,	fn_compose,	fn_SAK,\
	fn_dec_console,	fn_inc_console,	fn_spawn_con,	fn_bare_num

typedef void (fn_handler_fn)(struct tty_struct *tty);
static fn_handler_fn FN_HANDLERS;
static fn_handler_fn *fn_handler[] = { FN_HANDLERS };

/*
 * Variables/functions exported for vt_ioctl.c
 */

const int max_vals[] = {
	255, SIZE(func_table) - 1, SIZE(fn_handler) - 1, NR_PAD - 1,
	NR_DEAD - 1, 255, 3, NR_SHIFT - 1, 255, NR_ASCII - 1, NR_LOCK - 1,
	255, NR_LOCK - 1, 255
};
const int NR_TYPES = SIZE(max_vals);

int spawnpid, spawnsig;

int getkeycode(unsigned int scancode)
{
	return 0;
}
int setkeycode(unsigned int scancode, unsigned int keycode)
{
	return 0;
}

/*
 * Variables/function exported for vt.c
 */

int shift_state = 0;
DECLARE_WAIT_QUEUE_HEAD(keypress_wait);

int keyboard_wait_for_keypress(struct console *console)
{
	sleep_on(&keypress_wait);
	return 0;
}

/*
 * Internal data.
 */

static struct tty_struct **ttytab;

static unsigned long key_down[256/BITS_PER_LONG];
static unsigned char shift_down[NR_SHIFT];	/* shift state counters.. */
static int dead_key_next;
static int npadch = -1;				/* -1 or number assembled on pad */
static unsigned char diacr;
static char rep;				/* flag telling character repeat */
static struct pm_dev *pm_kbd;

static unsigned char ledstate = 0xff;		/* undefined */
static unsigned char ledioctl;

static struct ledptr {
	unsigned int *addr;
	unsigned int mask;
	unsigned char valid:1;
} ledptrs[3];

/*
 * Helper functions
 */

static void put_queue(struct tty_struct *tty, int ch)
{
	wake_up(&keypress_wait);
	if (tty) {
		tty_insert_flip_char(tty, ch, 0);
		tty_schedule_flip(tty);
	}
}

static void puts_queue(struct tty_struct *tty, char *cp)
{
	wake_up(&keypress_wait);
	if (!tty)
		return;
	while (*cp)
		tty_insert_flip_char(tty, *cp++, 0);
	tty_schedule_flip(tty);
}

static void applkey(struct tty_struct *tty, int key, char mode)
{
	static char buf[] = { 0x1b, 'O', 0x00, 0x00 };

	buf[1] = (mode ? 'O' : '[');
	buf[2] = key;
	puts_queue(tty, buf);
}

/*
 * Many other routines do put_queue, but I think either
 * they produce ASCII, or they produce some user-assigned
 * string, and in both cases we might assume that it is
 * in utf-8 already. UTF-8 is defined for words of up to 31 bits,
 * but we need only 16 bits here
 */

void to_utf8(struct tty_struct *tty, ushort c)
{
	if (c < 0x80)
		/* 0*******  */
		put_queue(tty, c);
	else if (c < 0x800) {
		/* 110***** 10****** */
		put_queue(tty, 0xc0 | (c >> 6));	
		put_queue(tty, 0x80 | (c & 0x3f));
	} else {
		/* 1110**** 10****** 10****** */
		put_queue(tty, 0xe0 | (c >> 12));
		put_queue(tty, 0x80 | ((c >> 6) & 0x3f));
		put_queue(tty, 0x80 | (c & 0x3f));
	}
}

/*
 * called after returning from RAW mode or when changing consoles -
 * recompute shift_down[] and shift_state from key_down[] 
 * maybe called when keymap is undefined, so that shiftkey release is seen 
 */

void compute_shiftstate(void)
{
	int i, j, sym, val;

	shift_state = 0;
	memset(shift_down, 0, sizeof(shift_down));

	for(i = 0; i < SIZE(key_down); i += BITS_PER_LONG) {

		if (!key_down[i / BITS_PER_LONG])
			continue;

		for(j = 0; j < BITS_PER_LONG; j++) {

			if (!test_bit(i + j, key_down))
				continue;

			sym = U(key_maps[0][i + j]);
			if (KTYP(sym) != KT_SHIFT && KTYP(sym) != KT_SLOCK)
				continue;
			
			val = KVAL(sym);
			if (val == KVAL(K_CAPSSHIFT))
				val = KVAL(K_SHIFT);

			shift_down[val]++;
			shift_state |= (1 << val);
		}
	}
}

/*
 * We have a combining character DIACR here, followed by the character CH.
 * If the combination occurs in the table, return the corresponding value.
 * Otherwise, if CH is a space or equals DIACR, return DIACR.
 * Otherwise, conclude that DIACR was not combining after all,
 * queue it and return CH.
 */

unsigned char handle_diacr(struct tty_struct *tty, unsigned char ch)
{
	int d = diacr;
	int i;

	diacr = 0;

	for (i = 0; i < accent_table_size; i++) {
		if (accent_table[i].diacr == d && accent_table[i].base == ch)
			return accent_table[i].result;
	}

	if (ch == ' ' || ch == d)
		return d;

	put_queue(tty, d);
	return ch;
}

/*
 * Special function handlers
 */

static void fn_enter(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (diacr) {
		put_queue(tty, diacr);
		diacr = 0;
	}
	put_queue(tty, 13);
	if (vc_kbd_mode(&vc->kbd_table, VC_CRLF))
		put_queue(tty, 10);
}

static void fn_caps_toggle(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	
	if (rep)
		return;
	chg_vc_kbd_led(&vc->kbd_table, VC_CAPSLOCK);
}

static void fn_caps_on(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (rep)
		return;
	set_vc_kbd_led(&vc->kbd_table, VC_CAPSLOCK);
}

static void fn_show_ptregs(struct tty_struct *tty)
{
	if (kbd_pt_regs)
		show_regs(kbd_pt_regs);
}

static void fn_hold(struct tty_struct *tty)
{
	if (rep || !tty)
		return;
	/*
	 * Note: SCROLLOCK will be set (cleared) by stop_tty (start_tty);
	 * these routines are also activated by ^S/^Q.
	 * (And SCROLLOCK can also be set by the ioctl KDSKBLED.)
	 */

	if (tty->stopped)
		start_tty(tty);
	else
		stop_tty(tty);
}

static void fn_num(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;	

	if (vc_kbd_mode(&vc->kbd_table, VC_APPLIC))
		applkey(tty, 'P', 1);
	else
		fn_bare_num(tty);
}

/*
 * NumLock code version unaffected by application keypad mode
 */

static void fn_bare_num(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (!rep)
		chg_vc_kbd_led(&vc->kbd_table, VC_NUMLOCK);
}

static void fn_lastcons(struct tty_struct *tty)
{
       struct vc_data *vc = (struct vc_data *) tty->driver_data;
	
       set_console(vc->display_fg->last_console);
}

static void fn_dec_console(struct tty_struct *tty)
{
       struct vc_data *vc = (struct vc_data *) tty->driver_data;  	
       int i, j = vc->display_fg->fg_console->vc_num;

       for (i = j-1; i != j; i--) {
		if (i < vc->display_fg->vcs.first_vc)
                	i = vc->display_fg->vcs.first_vc + MAX_NR_USER_CONSOLES;
		vc = find_vc(i);
                if (vc)
			break;
       }
       set_console(vc);
}

static void fn_inc_console(struct tty_struct *tty)
{
       struct vc_data *vc = (struct vc_data *) tty->driver_data; 	
       int i, j = vc->display_fg->fg_console->vc_num;

       for (i = j+1; i != j; i++) {
		if (i == vc->display_fg->vcs.first_vc + MAX_NR_USER_CONSOLES)
                	i = vc->display_fg->vcs.first_vc;
		vc = find_vc(i);
		if (vc)
                       	break;
       }
       set_console(vc);
}

static void fn_send_intr(struct tty_struct *tty)
{
	if (!tty)
		return;
	tty_insert_flip_char(tty, 0, TTY_BREAK);
	tty_schedule_flip(tty);
}

static void fn_scroll_forw(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	scrollfront(vc, 0);
}

static void fn_scroll_back(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	scrollback(vc, 0);
}

static void fn_compose(struct tty_struct *tty)
{
	/* ???? */
	dead_key_next = 1;
}

static void fn_spawn_con(struct tty_struct *tty)
{
	if (spawnpid)
		if (kill_proc(spawnpid, spawnsig, 1))
			spawnpid = 0;
}

static void fn_SAK(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	do_SAK(tty);
	reset_vc(vc);
}

static void fn_show_mem(struct tty_struct *tty)
{
	show_mem();
}

static void fn_show_state(struct tty_struct *tty)
{
	show_state();
}

static void fn_boot_it(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (vc->display_fg == admin_vt)	
		ctrl_alt_del();
}

static void fn_null(struct tty_struct *tty)
{
	compute_shiftstate();
}

/*
 * Special key handlers
 */

static void k_ignore(struct tty_struct *tty, unsigned char value, char up_flag)
{
}

static void k_spec(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (up_flag)
		return;
	if (value >= SIZE(fn_handler))
		return;
	if ((vc->kbd_table.kbdmode == VC_RAW || vc->kbd_table.kbdmode == VC_MEDIUMRAW) && value != K_SAK)
		return;		/* SAK is allowed even in raw mode */
	fn_handler[value](tty);
}

static void k_lowercase(struct tty_struct *tty, unsigned char value, 
			char up_flag)
{
	printk(KERN_ERR "keyboard.c: k_lowercase was called - impossible\n");
}

static void k_self(struct tty_struct *tty, unsigned char value, char up_flag)
{
	if (up_flag)
		return;		/* no action, if this is a key release */

	if (diacr)
		value = handle_diacr(tty, value);

	if (dead_key_next) {
		dead_key_next = 0;
		diacr = value;
		return;
	}
	put_queue(tty, value);
}

/*
 * Handle dead key. Note that we now may have several
 * dead keys modifying the same character. Very useful
 * for Vietnamese.
 */
static void k_dead2(struct tty_struct *tty, unsigned char value, char up_flag)
{
	if (up_flag)
		return;

	diacr = (diacr ? handle_diacr(tty, value) : value);
}

/*
 * Obsolete - for backwards compatibility only
 */
static void k_dead(struct tty_struct *tty, unsigned char value, char up_flag)
{
	static unsigned char ret_diacr[NR_DEAD] = {'`', '\'', '^', '~', '"', ',' };
	value = ret_diacr[value];
	k_dead2(tty, value, up_flag);
}

static void k_cons(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *tmp = (struct vc_data *) tty->driver_data; 
	struct vc_data *vc = tmp->display_fg->vcs.vc_cons[value];

	if (up_flag || !vc)
		return;	
	set_console(vc);
}

static void k_fn(struct tty_struct *tty, unsigned char value, char up_flag)
{
	if (up_flag)
		return;
	if (value < SIZE(func_table)) {
		if (func_table[value])
			puts_queue(tty, func_table[value]);
	} else
		printk(KERN_ERR "k_fn called with value=%d\n", value);
}

static void k_cur(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	static const char *cur_chars = "BDCA";

	if (up_flag)
		return;
	applkey(tty, cur_chars[value], vc_kbd_mode(&vc->kbd_table, VC_CKMODE));
}

static void k_pad(struct tty_struct *tty, unsigned char value, char up_flag)
{
	static const char *pad_chars = "0123456789+-*/\015,.?()";
	static const char *app_map = "pqrstuvwxylSRQMnnmPQ";
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (up_flag)
		return;		/* no action, if this is a key release */

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_mode(&vc->kbd_table, VC_APPLIC) && !shift_down[KG_SHIFT]) {
		applkey(tty, app_map[value], 1);
		return;
	}

	if (!vc_kbd_led(&vc->kbd_table, VC_NUMLOCK))
		switch (value) {
			case KVAL(K_PCOMMA):
			case KVAL(K_PDOT):
				k_fn(tty, KVAL(K_REMOVE), 0);
				return;
			case KVAL(K_P0):
				k_fn(tty, KVAL(K_INSERT), 0);
				return;
			case KVAL(K_P1):
				k_fn(tty, KVAL(K_SELECT), 0);
				return;
			case KVAL(K_P2):
				k_cur(tty, KVAL(K_DOWN), 0);
				return;
			case KVAL(K_P3):
				k_fn(tty, KVAL(K_PGDN), 0);
				return;
			case KVAL(K_P4):
				k_cur(tty, KVAL(K_LEFT), 0);
				return;
			case KVAL(K_P6):
				k_cur(tty, KVAL(K_RIGHT), 0);
				return;
			case KVAL(K_P7):
				k_fn(tty, KVAL(K_FIND), 0);
				return;
			case KVAL(K_P8):
				k_cur(tty, KVAL(K_UP), 0);
				return;
			case KVAL(K_P9):
				k_fn(tty, KVAL(K_PGUP), 0);
				return;
			case KVAL(K_P5):
				applkey(tty,'G', vc_kbd_mode(&vc->kbd_table, VC_APPLIC));
				return;
		}

	put_queue(tty, pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_mode(&vc->kbd_table, VC_CRLF))
		put_queue(tty, 10);
}


static void k_shift(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	int old_state = shift_state;

	if (rep)
		return;
	/*
	 * Mimic typewriter:
	 * a CapsShift key acts like Shift but undoes CapsLock
	 */

	if (value == KVAL(K_CAPSSHIFT)) {
		value = KVAL(K_SHIFT);
		if (!up_flag)
			clr_vc_kbd_led(&vc->kbd_table, VC_CAPSLOCK);
	}

	if (up_flag) {
		/*
		 * handle the case that two shift or control
		 * keys are depressed simultaneously
		 */
		if (shift_down[value])
			shift_down[value]--;
	} else
		shift_down[value]++;

	if (shift_down[value])
		shift_state |= (1 << value);
	else
		shift_state &= ~(1 << value);

	/* kludge */
	if (up_flag && shift_state != old_state && npadch != -1) {
		if (vc->kbd_table.kbdmode == VC_UNICODE)
			to_utf8(tty, npadch & 0xffff);
		else
			put_queue(tty, npadch & 0xff);
		npadch = -1;
	}
}

static void k_meta(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (up_flag)
		return;
	if (vc_kbd_mode(&vc->kbd_table, VC_META)) {
		put_queue(tty, '\033');
		put_queue(tty, value);
	} else
		put_queue(tty, value | 0x80);
}

static void k_ascii(struct tty_struct *tty, unsigned char value, char up_flag)
{
	int base;

	if (up_flag)
		return;

	if (value < 10) {		
		/* decimal input of code, while Alt depressed */
		base = 10;
	} else {		
		/* hexadecimal input of code, while AltGr depressed */
		value -= 10;
		base = 16;
	}

	if (npadch == -1)
		npadch = value;
	else
		npadch = npadch * base + value;
}

static void k_lock(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	if (up_flag || rep)
		return;
	chg_vc_kbd_lock(&vc->kbd_table, value);
}

static void k_slock(struct tty_struct *tty, unsigned char value, char up_flag)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;

	k_shift(tty, value,up_flag);
	if (up_flag || rep)
		return;
	chg_vc_kbd_slock(&vc->kbd_table, value);
	/* try to make Alt, oops, AltGr and such work */
	if (!key_maps[vc->kbd_table.lockstate ^ vc->kbd_table.slockstate]) {
		vc->kbd_table.slockstate = 0;
		chg_vc_kbd_slock(&vc->kbd_table, value);
	}
}

/*
 * The leds display either (i) the status of NumLock, CapsLock, ScrollLock,
 * or (ii) whatever pattern of lights people want to show using KDSETLED,
 * or (iii) specified bits of specified words in kernel memory.
 */

unsigned char getledstate(void) {
	return ledstate;
}

void setledstate(struct kbd_struct *kbd, unsigned int led)
{
	if (!(led & ~7)) {
		ledioctl = led;
		kbd->ledmode = LED_SHOW_IOCTL;
	} else
		kbd->ledmode = LED_SHOW_FLAGS;
	set_leds();
}

void register_leds(struct kbd_struct *kbd, unsigned int led,unsigned int *addr,
		   unsigned int mask)
{
	if (led < 3) {
		ledptrs[led].addr = addr;
		ledptrs[led].mask = mask;
		ledptrs[led].valid = 1;
		kbd->ledmode = LED_SHOW_MEM;
	} else
		kbd->ledmode = LED_SHOW_FLAGS;
}

static inline unsigned char getleds(struct vt_struct *vt)
{
	struct kbd_struct *kbd = &vt->fg_console->kbd_table;
	unsigned char leds;
	int i;

	if (kbd->ledmode == LED_SHOW_IOCTL)
		return ledioctl;

	leds = kbd->ledflagstate;

	if (kbd->ledmode == LED_SHOW_MEM) {
		for (i = 0; i < 3; i++)
			if (ledptrs[i].valid) {
				if (*ledptrs[i].addr & ledptrs[i].mask)
					leds |= (1 << i);
				else
					leds &= ~(1 << i);
			}
	}
	return leds;
}

static struct input_handler kbd_handler;

/*
 * This routine is the bottom half of the keyboard interrupt
 * routine, and runs with all interrupts enabled. It does
 * console changing, led setting and copy_to_cooked, which can
 * take a reasonably long time.
 *
 * Aside from timing (which isn't really that important for
 * keyboard interrupts as they happen often), using the software
 * interrupt routines for this thing allows us to easily mask
 * this when we don't want any of the above to happen. Not yet
 * used, but this allows for easy and efficient race-condition
 * prevention later on.
 */

static void kbd_bh(unsigned long dummy)
{
	struct input_handle *handle;	
	unsigned char leds;

#ifdef CONFIG_INPUT
	for (handle = kbd_handler.handle; handle; handle = handle->hnext) {
		if (handle->private) {
			leds = getleds(handle->private);
			if (leds != ledstate) {
                		ledstate = leds;
				input_event(handle->dev, EV_LED, LED_SCROLLL, !!(leds & 0x01));
				input_event(handle->dev, EV_LED, LED_NUML,    !!(leds & 0x02));
				input_event(handle->dev, EV_LED, LED_CAPSL,   !!(leds & 0x04));
			}
		}	
	}
#endif
}

DECLARE_TASKLET_DISABLED(keyboard_tasklet, kbd_bh, 0);

#ifdef CONFIG_INPUT

#if defined(CONFIG_X86) || defined(CONFIG_IA64) || defined(CONFIG_ALPHA) || defined(CONFIG_MIPS) || defined(CONFIG_PPC)

static int x86_sysrq_alt = 0;

static unsigned short x86_keycodes[256] =
	{ 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
	 80, 81, 82, 83, 43, 85, 86, 87, 88,115,119,120,121,375,123, 90,
	284,285,309,298,312, 91,327,328,329,331,333,335,336,337,338,339,
	367,294,293,286,350, 92,334,512,116,377,109,111,259,347,348,349,
	360, 93, 94, 95, 98,376,100,101,357,316,354,304,289,102,351,355,
	103,104,105,275,281,272,306,106,274,107,288,364,358,363,362,361,
	291,108,381,290,287,292,279,305,280, 99,112,257,258,113,270,114,
	118,117,125,374,379,373,260,261,262,263,264,265,266,267,268,269,
	271,273,276,277,278,282,283,295,296,297,299,300,301,302,303,307,
	308,310,313,314,315,317,318,319,320,321,322,323,324,325,326,330,
	332,340,341,342,343,344,345,346,356,359,365,368,369,370,371,372 };

#ifdef CONFIG_MAC_ADBKEYCODES
extern int mac_hid_keyboard_sends_linux_keycodes(void);
static unsigned char mac_keycodes[256] =
	{ 0, 53, 18, 19, 20, 21, 23, 22, 26, 28, 25, 29, 27, 24, 51, 48,
	 12, 13, 14, 15, 17, 16, 32, 34, 31, 35, 33, 30, 36, 54,128,  1,
	  2,  3,  5,  4, 38, 40, 37, 41, 39, 50, 56, 42,  6,  7,  8,  9,
	 11, 45, 46, 43, 47, 44,123, 67, 58, 49, 57,122,120, 99,118, 96,
	 97, 98,100,101,109, 71,107, 89, 91, 92, 78, 86, 87, 88, 69, 83,
	 84, 85, 82, 65, 42,  0, 10,103,111,  0,  0,  0,  0,  0,  0,  0,
	 76,125, 75,105,124,110,115, 62,116, 59, 60,119, 61,121,114,117,
	  0,  0,  0,  0,127, 81,  0,113,  0,  0,  0,  0, 95, 55, 55,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0, 94,  0, 93,  0,  0,  0,  0,  0,  0,104,102 };
#endif

static int emulate_raw(struct tty_struct *tty, unsigned int keycode, 
		       unsigned char up_flag)
{
#ifdef CONFIG_MAC_ADBKEYCODES
	if (!mac_hid_keyboard_sends_linux_keycodes()) {
		if (keycode > 255 || !mac_keycodes[keycode])
			return -1;

		put_queue((mac_keycodes[keycode] & 0x7f) | up_flag);

		return 0;
	}
#endif
	if (keycode > 255 || !x86_keycodes[keycode])
		return -1; 

	if (keycode == KEY_PAUSE) {
		put_queue(tty, 0xe1);
		put_queue(tty, 0x1d | up_flag);
		put_queue(tty, 0x45 | up_flag);
		return 0;
	} 

	if (keycode == KEY_SYSRQ && x86_sysrq_alt) {
		put_queue(tty, 0x54 | up_flag);
		return 0;
	}

	if (x86_keycodes[keycode] & 0x100)
		put_queue(tty, 0xe0);

	put_queue(tty, (x86_keycodes[keycode] & 0x7f) | up_flag);

	if (keycode == KEY_SYSRQ) {
		put_queue(tty, 0xe0);
		put_queue(tty, 0x37 | up_flag);
	}

	if (keycode == KEY_LEFTALT || keycode == KEY_RIGHTALT)
		x86_sysrq_alt = !up_flag;

	return 0;
}

#else

#warning "Cannot generate rawmode keyboard for your architecture yet."

void emulate_raw(struct tty_struct *tty,unsigned int code,unsigned char up_flag)
{
	if (keycode > 127)
		return -1;

	put_queue(tty, keycode | up_flag);
	return 0;
}
#endif

void kbd_keycode(struct vt_struct *vt, unsigned int keycode, int down)
{
	unsigned short keysym, *key_map;
	unsigned char type, raw_mode;
	struct tty_struct *tty;
	struct vc_data *vc = vt->fg_console;
	int shift_final;

	pm_access(pm_kbd);

	add_keyboard_randomness(keycode ^ (down << 7));

	tty = ttytab ? ttytab[vc->vc_num] : NULL;

	if (tty && (!tty->driver_data)) {
		/*
		 * We touch the tty structure via the the ttytab array
		 * without knowing whether or not tty is open, which
		 * is inherently dangerous. We currently rely on that
		 * fact that console_open sets tty->driver_data when
		 * it opens it, and clears it when it closes it.
		 */
		tty = NULL;
	}

	/* If the console is blanked unblank it */
	want_vc = vc;
        tasklet_schedule(&console_tasklet);

	if ((raw_mode = (vc->kbd_table.kbdmode == VC_RAW)))
		if (emulate_raw(tty, keycode, !down << 7))
			printk(KERN_WARNING "keyboard.c: can't emulate rawmode for keycode %d\n", keycode);

	if (vc->kbd_table.kbdmode == VC_MEDIUMRAW) {
		/*
		 * This is extended medium raw mode, with keys above 127
		 * encoded as 0, high 7 bits, low 7 bits, with the 0 bearing
		 * the 'up' flag if needed. 0 is reserved, so this shouldn't
		 * interfere with anything else. The two bytes after 0 will
		 * always have the up flag set not to interfere with older
		 * applications. This allows for 16384 different keycodes,
		 * which should be enough.
		 */
		if (keycode < 128) {
			put_queue(tty, keycode | (!down << 7));
		} else {
			put_queue(tty, !down << 7);
			put_queue(tty, (keycode >> 7) | 0x80);
			put_queue(tty, keycode | 0x80);
		}
		raw_mode = 1;
	}

	rep = (down == 2);

	if (rep && (!vc_kbd_mode(&vc->kbd_table, VC_REPEAT) || !tty || 
		(!L_ECHO(tty) && tty->driver.chars_in_buffer(tty)))) {
		/*
		 * Don't repeat a key if the input buffers are not empty and the
		 * characters get aren't echoed locally. This makes key repeat usable
		 * with slow applications and under heavy loads.
		 */
		return;
	}

	shift_final = (shift_state | vc->kbd_table.slockstate) ^ vc->kbd_table.lockstate;
	key_map = key_maps[shift_final];

	if (!key_map) {
		compute_shiftstate();
		vc->kbd_table.slockstate = 0;
		return;
	}

	keysym = key_map[keycode];
	type = KTYP(keysym);

	if (type < 0xf0) {
		if (down && !raw_mode) to_utf8(tty, keysym);
		return;
	}

	type -= 0xf0;

	if (raw_mode && type != KT_SPEC && type != KT_SHIFT)
		return;

	if (type == KT_LETTER) {
		type = KT_LATIN;
		if (vc_kbd_led(&vc->kbd_table, VC_CAPSLOCK)) {
			key_map = key_maps[shift_final ^ (1 << KG_SHIFT)];
			if (key_map)
				keysym = key_map[keycode];
		}
	}

	(*k_handler[type])(tty, keysym & 0xff, !(down << 7));
	if (type != KT_SLOCK)
		vc->kbd_table.slockstate = 0;
}

void kbd_event(struct input_handle *handle, unsigned int event_type, unsigned int keycode, int down)
{
	if (event_type != EV_KEY) return;
	if (handle->private)
		kbd_keycode(handle->private, keycode, down);
	tasklet_schedule(&keyboard_tasklet);
}

/*
 * When a keyboard (or other input device) is found, the kbd_connect
 * function is called. The function then looks at the device, and if it
 * likes it, it can open it and get events from it. In this (kbd_connect)
 * function, we should decide which VT to bind that keyboard to initially.
 */
static struct input_handle *kbd_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct input_handle *handle;
	struct vt_struct *vt = vt_cons;
	int i;

	if (!test_bit(EV_KEY, dev->evbit)) return NULL;
	for (i = KEY_RESERVED; i < BTN_MISC; i++) if (test_bit(i, dev->keybit)) break;
	if (i == BTN_MISC) return NULL;

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL))) return NULL;
	memset(handle, 0, sizeof(struct input_handle));

	while (vt) {
                if (!vt->keyboard) {
                        vt->keyboard = handle;
			handle->private = vt;
			break;
		} else 
			vt = vt->next;
	}	
	handle->dev = dev;
	handle->handler = handler;
	input_open_device(handle);
	printk(KERN_INFO "keyboard.c: Adding keyboard: input%d\n", dev->number);
	return handle;
}

static void kbd_disconnect(struct input_handle *handle)
{
	struct vt_struct *vt = vt_cons;

	printk(KERN_INFO "keyboard.c: Removing keyboard: input%d\n", handle->dev->number);
	while (vt) {
		if (vt->keyboard == handle) {
			vt->keyboard = NULL;
			break;
		}
		vt = vt->next;	
	}	
	input_close_device(handle);
	kfree(handle);
}
	
static struct input_handler kbd_handler = {
	event:		kbd_event,
	connect:	kbd_connect,
	disconnect:	kbd_disconnect,
};

#endif

int __init kbd_init(void)
{
        extern struct tty_driver console_driver;

        ttytab = console_driver.table;

	tasklet_enable(&keyboard_tasklet);
	tasklet_schedule(&keyboard_tasklet);

	pm_kbd = pm_register(PM_SYS_DEV, PM_SYS_KBC, NULL);

#ifdef CONFIG_INPUT
	input_register_handler(&kbd_handler);
#endif
	return 0;
}
