/*
 * linux/drivers/char/keyboard.c
 *
 * Written for linux by Johan Myreen as a translation from
 * the assembly version by Linus (with diacriticals added)
 *
 * Some additional features added by Christoph Niemann (ChN), March 1993
 *
 * Loadable keymaps by Risto Kankkunen, May 1993
 *
 * Diacriticals redone & other small changes, aeb@cwi.nl, June 1993
 * Added decr/incr_console, dynamic keymaps, Unicode support,
 * dynamic function/string keys, led setting,  Sept 1994
 * `Sticky' modifier keys, 951006.
 *
 * 11-11-96: SAK should now work in the raw mode (Martin Mares)
 * 
 * Modified to provide 'generic' keyboard support by Hamish Macdonald
 * Merge with the m68k keyboard driver and split-off of the PC low-level
 * parts by Geert Uytterhoeven, May 1997
 *
 * 27-05-97: Added support for the Magic SysRq Key (Martin Mares)
 * 30-07-98: Dead keys redone, aeb@cwi.nl.
 *
 * Rewritten for the new generic input layer by Martin Mares, May 1999.
 */

#include <linux/config.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/kbd_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/vt_kern.h>
#include <linux/sysrq.h>
#include <linux/pm.h>

#include <asm/bitops.h>

#ifdef CONFIG_MAGIC_SYSRQ
unsigned char kbd_sysrq_xlate[128] =
       "\000\0331234567890-=\177\t"                    /* 0x00 - 0x0f */
       "qwertyuiop[]\r\000as"                          /* 0x10 - 0x1f */
       "dfghjkl;'`\000\\zxcv"                          /* 0x20 - 0x2f */
       "bnm,./\000*\000 \000\201\202\203\204\205"      /* 0x30 - 0x3f */
       "\206\207\210\211\212\000\000789-456+1"         /* 0x40 - 0x4f */
       "230\177\000\000\213\214\000\000\000\000\000\000\000\000\000\000" /* 0x50 - 0x5f */
       "\r\000/";                                      /* 0x60 - 0x6f */
#endif

#define SIZE(x) (sizeof(x)/sizeof((x)[0]))

#ifndef KBD_DEFMODE
#define KBD_DEFMODE ((1 << VC_REPEAT) | (1 << VC_META))
#endif

#ifndef KBD_DEFLEDS
/*
 * Some laptops take the 789uiojklm,. keys as number pad when NumLock
 * is on. This seems a good reason to start with NumLock off.
 */
#define KBD_DEFLEDS 0
#endif

#ifndef KBD_DEFLOCK
#define KBD_DEFLOCK 0
#endif

void (*kbd_ledfunc)(unsigned int led) = NULL;

DECLARE_WAIT_QUEUE_HEAD(keypress_wait);

int keyboard_wait_for_keypress(struct console *co)
{
	sleep_on(&keypress_wait);
	return 0;
}

struct kbd_struct kbd_table[MAX_NR_CONSOLES];

/* Dummy keyboard we use before the first real keyboard gets connected */
static struct keyboard dummy_keyboard;

/* Key handlers */
typedef void (k_handfn)(struct keyboard *keyb, struct kbd_struct *kbd, unsigned int value, int down);
typedef k_handfn *k_hand;
#define K_HANDLER(name) void name(struct keyboard *keyb, struct kbd_struct *kbd, unsigned int value, int down)

#define KEY_HANDLERS
\
       do_self, do_fn, do_spec, do_pad, do_dead, do_cons, do_cur, do_shift,
\
       do_meta, do_ascii, do_lock, do_lowercase, do_slock, do_dead2, do_ignore

static k_handfn KEY_HANDLERS;
static k_hand key_handler[16] = { KEY_HANDLERS };

typedef void (t_handfn)(struct keyboard *, struct kbd_struct *, int down);
typedef t_handfn *t_hand;
#define T_HANDLER(name) void name(struct keyboard *keyb, struct kbd_struct *kbd, int repeat)

#define SPEC_HANDLERS
\
       do_null,        enter,          show_ptregs,    kbd_show_mem,
\
       kbd_show_state, send_intr,      lastcons,       caps_toggle,
\
       num,            hold,           scroll_forw,    scroll_back,
\
       boot_it,        caps_on,        compose,        SAK,
\
        decr_console,   incr_console,   spawn_console,  bare_num

static t_handfn SPEC_HANDLERS;
static t_hand spec_fn_table[] = { SPEC_HANDLERS };

/* Key types processed even in raw modes */

#define TYPES_ALLOWED_IN_RAW_MODE ((1 << KT_SPEC) | (1 << KT_SHIFT))
#define SPECIALS_ALLOWED_IN_RAW_MODE (1 << KVAL(K_SAK))

/* Maximum values each key_handler can handle */
const int max_vals[] = {
	255, SIZE(func_table) - 1, SIZE(spec_fn_table) - 1, NR_PAD - 1,
	NR_DEAD - 1, 255, 3, NR_SHIFT - 1,
	255, NR_ASCII - 1, NR_LOCK - 1, 255,
	NR_LOCK - 1, 255
};

const int NR_TYPES = SIZE(max_vals);

/* Prototypes */
static unsigned char handle_diacr(struct keyboard *keyb, unsigned char);
static void put_utf8(struct kbd_struct *, __u16);

/* kbd_pt_regs - set by keyboard interrupt, used by show_ptregs() */
struct pt_regs *kbd_pt_regs;
EXPORT_SYMBOL(kbd_pt_regs);

static unsigned char keyboard_e0s[] =
               { 0x1c, 0x1d, 0x35, 0x2a, 0x38, 0x39, 0x47, 0x48,
                 0x49, 0x4b, 0x4d, 0x4f, 0x50, 0x51, 0x52, 0x53 };

static void keyboard_emulate_raw(struct kbd_struct *kbd, unsigned char keycode, 				 unsigned char upflag)
{
       if (keycode >= 125) {
               put_queue(kbd, 0xe0);
               put_queue(kbd, (keycode - 34) | upflag);
       } else if (keycode == 119) {
               put_queue(kbd, 0xe1);
               put_queue(kbd, 0x1d | upflag);
               put_queue(kbd, 0x45 | upflag);
       } else if (keycode >= 96) {
               put_queue(kbd, 0xe0);
               put_queue(kbd, keyboard_e0s[keycode - 96] | upflag);
               if (keycode == 99) {
                       put_queue(kbd, 0xe0);
                       put_queue(kbd, 0x37 | upflag);
               }
       } else put_queue(kbd, keycode | upflag);
}

static inline struct tty_struct* kbd_tty(struct kbd_struct *kbd)
{                                                          /* FIXME */
       int cons = kbd - kbd_table;
       struct tty_struct *tty = console_driver.table[cons];

       if (tty && (!tty->driver_data)) {
		/*
		 * We touch the tty structure via the the ttytab array
		 * without knowing whether or not tty is open, which
		 * is inherently dangerous.  We currently rely on that
		 * fact that console_open sets tty->driver_data when
		 * it opens it, and clears it when it closes it.
		 */
		tty = NULL;
	}
	return tty;
}

void keyboard_event(struct input_handle *handle, unsigned int event_type, 
		    unsigned int code, int down)
{
	struct keyboard *keyb = handle->private;
       	unsigned int keycode, upflag, raw_mode;
       	struct kbd_struct *kbd = keyb->current_kbd;
       	struct tty_struct *tty;

       	/* FIXME - we can't handle > 127 yet */
       	if (event_type != EV_KEY || code > 127) 
		return;

       	keycode = code & 0xff;
       	upflag = !down << 7;

       	if (!kbd)
               return;
       	tty = kbd_tty(kbd);

       	do_poke_blanked_console = 1;
	tasklet_schedule(&console_tasklet);

       	if ((raw_mode = (kbd->kbdmode == VC_RAW)))
		keyboard_emulate_raw(kbd, keycode, upflag);
	
#ifdef CONFIG_MAGIC_SYSRQ		
	/* Handle the SysRq Hack */
	/* FIXME: Make KEY_SYSRQ kbd-local? */
	if (keycode == KEY_SYSRQ) {
		keyb->sysrq_pressed = down;	
		return;
	} else if (keyb->sysrq_pressed) {
		if (down) {
			handle_sysrq(kbd_sysrq_xlate[keycode], kbd_pt_regs, 
				     kbd, tty);
			return;  /* FIXME !!!!!!!!!!! */
		}
	}
#endif

	if (kbd->kbdmode == VC_MEDIUMRAW) {
		/* soon keycodes will require more than one byte */
		put_queue(kbd, keycode | up_flag);
		raw_mode = 1;	/* Most key classes will be ignored */
	}

	/*
	 *  Repeat a key only if the input buffers are empty or the
	 *  characters get echoed locally. This makes key repeat usable
	 *  with slow applications and under heavy loads.
	 */
	if (down != 2 || (vc_kbd_mode(kbd, VC_REPEAT) && tty &&      
	    (L_ECHO(tty) || (tty->driver.chars_in_buffer(tty) == 0)))) {
		u16 keysym;
		u8 type;

		unsigned int shift_final = 
			(keyb->shift_state | kbd->slockstate) ^ kbd->lockstate;
                /* FIXME: Per-keyboard keymaps. Ugh. */
		u16 *key_map = key_maps[shift_final];       

		if (key_map) {
			keysym = key_map[keycode];
			type = KTYP(keysym);

			if (type >= 0xf0) {
			    type -= 0xf0;
			    if (raw_mode && ! (TYPES_ALLOWED_IN_RAW_MODE & (1 << type)))
				return;
			    if (type == KT_LETTER) {
				type = KT_LATIN;
				if (vc_kbd_led(kbd, VC_CAPSLOCK)) {
				    key_map = key_maps[shift_final ^ (1<<KG_SHIFT)];
				    if (key_map)
				      keysym = key_map[keycode];
				}
			    }
			    (*key_handler[type])(keyb, kbd, keysym & 0xff,down);
			    if (type != KT_SLOCK)
			      kbd->slockstate = 0;
			} else {
			    /* maybe only if (kbd->kbdmode == VC_UNICODE) ? */
			    if (down && !raw_mode)
			      put_utf8(kbd, keysym);
			}
		} else {
			/* maybe beep? */
			/* we have at least to update shift_state */
			compute_shiftstate(keyb);
			kbd->slockstate = 0; /* play it safe */
		}
	}
}

void put_queue(struct kbd_struct *kbd, int ch)
{
	struct tty_struct *tty = kbd_tty(kbd);

	wake_up(&keypress_wait);
	if (tty) {
		tty_insert_flip_char(tty, ch, 0);
		con_schedule_flip(tty);
	}
}

static void puts_queue(struct kbd_struct *kbd, char *cp)
{
	struct tty_struct *tty = kbd_tty(kbd);

	wake_up(&keypress_wait);
	if (!tty)
		return;

	while (*cp) {
		tty_insert_flip_char(tty, *cp, 0);
		cp++;
	}
	con_schedule_flip(tty);
}

/*
 * Many other routines do put_queue, but I think either
 * they produce ASCII, or they produce some user-assigned
 * string, and in both cases we might assume that it is
 * in utf-8 already.
 */
static void put_utf8(struct kbd_struct *kbd, u16 c)
{
       if (c < 0x80)
               put_queue(kbd, c);                      /*  0*******  */
       else if (c < 0x800) {
               put_queue(kbd, 0xc0 | (c >> 6));        /*  110***** 10******  */
               put_queue(kbd, 0x80 | (c & 0x3f));
       } else {
               put_queue(kbd, 0xe0 | (c >> 12));       /*  1110**** 10****** 10******  */
               put_queue(kbd, 0x80 | ((c >> 6) & 0x3f));
               put_queue(kbd, 0x80 | (c & 0x3f));
       }
       /* UTF-8 is defined for words of up to 31 bits,
          but we need only 16 bits here */
}

static void applkey(struct kbd_struct *kbd, int key, char mode)
{
	/* FIXME: Re-enterable? */
	static char buf[] = { 0x1b, 'O', 0x00, 0x00 };

	buf[1] = (mode ? 'O' : '[');
	buf[2] = key;
	puts_queue(kbd, buf);
}

static void enter(void)
{
	if (keyb->diacr) {
		put_queue(kbd, keyb->diacr);
		keyb->diacr = 0;
	}
	put_queue(kbd, 13);
	if (vc_kbd_mode(kbd, VC_CRLF))
		put_queue(kbd, 10);
}

T_HANDLER(caps_toggle)
{
	if (!repeat)
	    chg_vc_kbd_led(kbd, VC_CAPSLOCK);
}

T_HANDLER(caps_on)
{
	if (!repeat)
	    set_vc_kbd_led(kbd, VC_CAPSLOCK);
}

T_HANDLER(show_ptregs)
{
	if (kbd_pt_regs)
		show_regs(kbd_pt_regs);
}

T_HANDLER(kbd_show_mem)
{
       show_mem();
}

T_HANDLER(kbd_show_state)
{
       show_state();
}

T_HANDLER(hold)
{
	struct tty_struct *tty = kbd_tty(kbd);	

	if (repeat || !tty)
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

T_HANDLER(num)
{
	if (vc_kbd_mode(kbd, VC_APPLIC))
		applkey(kbd, 'P', 1);
	else
		bare_num(keyb, kbd, repeat);
}

/*
 * Bind this to Shift-NumLock if you work in application keypad mode
 * but want to be able to change the NumLock flag.
 * Bind this to NumLock if you prefer that the NumLock key always
 * changes the NumLock flag.
 */
T_HANDLER(bare_num)
{
	if (!repeat)
		chg_vc_kbd_led(kbd, VC_NUMLOCK);
}

T_HANDLER(lastcons)
{
	kbd_set_console(keyb, keyb->last_console);
}

T_HANDLER(decr_console)
{
	int i;
 
	for (i = keyb->fg_console-1; i != keyb->fg_console; i--) {
		if (i == -1)
			i = MAX_NR_CONSOLES-1;
		if (test_bit(i, keyb->consoles) && vc_cons_allocated(i))	
			break;
	}
	kbd_set_console(keyb, i);
}

T_HANDLER(incr_console)
{
	int i;

	for (i = keyb->fg_console + 1; i != keyb->fg_console; i++) {
		if (i == MAX_NR_CONSOLES)
			i = 0;
		if (test_bit(i, keyb->consoles) && vc_cons_allocated(i))	
			break;
	}
	kbd_set_console(keyb, i);
}

T_HANDLER(send_intr)
{
	struct tty_struct *tty = kbd_tty(kbd);

	if (!tty)
		return;
	tty_insert_flip_char(tty, 0, TTY_BREAK);
	con_schedule_flip(tty);
}

T_HANDLER(scroll_forw)                             /* FIXME */
{
	scrollfront(0);
}

T_HANDLER(scroll_back)                             /* FIXME */
{
	scrollback(0);
}

T_HANDLER(boot_it)
{
	ctrl_alt_del();
}

T_HANDLER(compose)
{
	keyb->dead_key_next = 1;
}

T_HANDLER(spawn_console)
{
        if (keyb->spawnpid)
	   if (kill_proc(keyb->spawnpid, keyb->spawnsig, 1))
	       keyb->spawnpid = 0;
}

static void SAK(void)
{
	struct tty_struct *tty = kbd_tty(kbd);

	if (tty) {
		do_SAK(tty);
		/* FIXME: Does this reset raw mode? */
		reset_vc(fg_console);
#if 0
		/* not in interrupt routine? FIXME!!!!!!!!!! */
		do_unblank_screen();	
#endif
	}
}

K_HANDLER(do_ignore)
{
}

T_HANDLER(do_null)
{
	compute_shiftstate(keyb);
}

K_HANDLER(do_spec)
{
	if (!down)
		return;
	if (value >= SIZE(spec_fn_table))
		return;
	if ((kbd->kbdmode == VC_RAW || kbd->kbdmode == VC_MEDIUMRAW) &&
	    !(SPECIALS_ALLOWED_IN_RAW_MODE & (1 << value)))
		return;
	spec_fn_table[value](keyb, kbd, down == 2);
}

K_HANDLER(do_lowercase)
{
	printk(KERN_ERR "keyboard.c: do_lowercase was called - impossible\n");
}

K_HANDLER(do_self)
{
	if (!down)
		return;		/* no action, if this is a key release */

	if (keyb->diacr)
		value = handle_diacr(keyb, value);

	if (keyb->dead_key_next) {
		keyb->dead_key_next = 0;
		keyb->diacr = value;
		return;
	}
	put_queue(kbd, value);
}

#define A_GRAVE  '`'
#define A_ACUTE  '\''
#define A_CFLEX  '^'
#define A_TILDE  '~'
#define A_DIAER  '"'
#define A_CEDIL  ','

/* Obsolete - for backwards compatibility only */
K_HANDLER(do_dead)
{
	static unsigned char ret_diacr[NR_DEAD] =
               {A_GRAVE, A_ACUTE, A_CFLEX, A_TILDE, A_DIAER, A_CEDIL };
	do_dead2(keyb, kbd, ret_diacr[value], down);
}

/*
 * Handle dead key. Note that we now may have several
 * dead keys modifying the same character. Very useful
 * for Vietnamese.
 */
K_HANDLER(do_dead2)
{
	if (down)
            keyb->diacr = (keyb->diacr ? handle_diacr(keyb, value) : value);
}

/*
 * We have a combining character DIACR here, followed by the character CH.
 * If the combination occurs in the table, return the corresponding value.
 * Otherwise, if CH is a space or equals DIACR, return DIACR.
 * Otherwise, conclude that DIACR was not combining after all,
 * queue it and return CH.
 */
unsigned char handle_diacr(struct keyboard *keyb, unsigned char ch)
{
	int d = keyb->diacr;
	int i;

	keyb->diacr = 0;

	for (i = 0; i < accent_table_size; i++) {
		if (accent_table[i].diacr == d && accent_table[i].base == ch)
			return accent_table[i].result;
	}

	if (ch == ' ' || ch == d)
		return d;

	put_queue(keyb->current_kbd, d);
	return ch;
}

K_HANDLER(do_cons)
{
	if (down && test_bit(value, keyb->consoles))
               kbd_set_console(keyb, value);
}

K_HANDLER(do_fn)
{
	if (!down)
		return;
	if (value < SIZE(func_table)) {
		if (func_table[value])
			puts_queue(kbd, func_table[value]);
	} else
		printk(KERN_ERR "do_fn called with value=%d\n", value);
}

K_HANDLER(do_pad)
{
	static const char *pad_chars = "0123456789+-*/\015,.?()";
	static const char *app_map = "pqrstuvwxylSRQMnnmPQ";

	if (!down)
		return;		/* no action, if this is a key release */

	/* kludge... shift forces cursor/number keys */
	if (vc_kbd_mode(kbd, VC_APPLIC) && !keyb->k_down[KG_SHIFT]) {
		applkey(kbd, app_map[value], 1);
		return;
	}

	if (!vc_kbd_led(kbd, VC_NUMLOCK))
		switch (value) {
			case KVAL(K_PCOMMA):
			case KVAL(K_PDOT):
				do_fn(keyb, kbd, KVAL(K_REMOVE), 0);
				return;
			case KVAL(K_P0):
				do_fn(keyb, kbd, KVAL(K_INSERT), 0);
				return;
			case KVAL(K_P1):
				do_fn(keyb, kbd, KVAL(K_SELECT), 0);
				return;
			case KVAL(K_P2):
				do_cur(keyb, kbd, KVAL(K_DOWN), 0);
				return;
			case KVAL(K_P3):
				do_fn(keyb, kbd, KVAL(K_PGDN), 0);
				return;
			case KVAL(K_P4):
				do_cur(keyb, kbd, KVAL(K_LEFT), 0);
				return;
			case KVAL(K_P6):
				do_cur(keyb, kbd, KVAL(K_RIGHT), 0);
				return;
			case KVAL(K_P7):
				do_fn(keyb, kbd, KVAL(K_FIND), 0);
				return;
			case KVAL(K_P8):
				do_cur(keyb, kbd, KVAL(K_UP), 0);
				return;
			case KVAL(K_P9):
				do_fn(keyb, kbd, KVAL(K_PGUP), 0);
				return;
			case KVAL(K_P5):
				applkey(kbd, 'G', vc_kbd_mode(kbd, VC_APPLIC));
				return;
		}

	put_queue(kbd, pad_chars[value]);
	if (value == KVAL(K_PENTER) && vc_kbd_mode(kbd, VC_CRLF))
		put_queue(kbd, 10);
}

K_HANDLER(do_cur)
{
	static const char *cur_chars = "BDCA";

	if (down)
	    applkey(kbd, cur_chars[value], vc_kbd_mode(kbd,VC_CKMODE));
}

K_HANDLER(do_shift)
{
	int old_state = keyb->shift_state;

	if (down == 2)
		return;

	/* Mimic typewriter:
	   a CapsShift key acts like Shift but undoes CapsLock */
	if (value == KVAL(K_CAPSSHIFT)) {
		value = KVAL(K_SHIFT);
		if (down)
			clr_vc_kbd_led(kbd, VC_CAPSLOCK);
	}

	if (!down) {
		/* handle the case that two shift or control
		   keys are depressed simultaneously */
		if (keyb->k_down[value])
			keyb->k_down[value]--;
	} else
		keyb->k_down[value]++;

	if (keyb->k_down[value])
		keyb->shift_state |= (1 << value);
	else
		keyb->shift_state &= ~ (1 << value);

	/* kludge */
	if (!down && keyb->shift_state != old_state && keyb->npadch != -1) {
		if (kbd->kbdmode == VC_UNICODE)
		  put_utf8(kbd, keyb->npadch & 0xffff);
		else
		  put_queue(kbd, keyb->npadch & 0xff);
		keyb->npadch = -1;
	}
}

/* called after returning from RAW mode or when changing consoles -
   recompute k_down[] and shift_state from key_down[] 
   maybe called when keymap is undefined, so that shiftkey release is seen */
void compute_shiftstate(struct keyboard *keyb)
{
	struct input_dev *dev = keyb->handle.dev;
	int i, j, k, sym, val;

	keyb->shift_state = 0;
	for(i = 0; i < SIZE(keyb->k_down); i++)
		keyb->k_down[i] = 0;
        if (!dev)
               return;

       for(i=0; i < 256 / BITS_PER_LONG; i++)
               if (dev->key[i]) {              /* Skip this word if not a single bit on */
                       k = i*BITS_PER_LONG;
                       for(j=0; j<BITS_PER_LONG; j++,k++)
                               if (test_bit(k, dev->key)) {
                                       sym = U(plain_map[k]);
                                       if(KTYP(sym) == KT_SHIFT || KTYP(sym) == KT_SLOCK) {
                                               val = KVAL(sym);
                                              if (val == KVAL(K_CAPSSHIFT))
                                                       val = KVAL(K_SHIFT);
                                               keyb->k_down[val]++;
                                               keyb->shift_state |= (1<<val);
                                       }
                               }
		}
	}
}

K_HANDLER(do_meta)
{
	if (!down)
		return;

	if (vc_kbd_mode(kbd, VC_META)) {
		put_queue(kbd, '\033');
		put_queue(kbd, value);
	} else
		put_queue(kbd, value | 0x80);
}

K_HANDLER(do_ascii)
{
	int base;

	if (!down)
		return;

	if (value < 10)    /* decimal input of code, while Alt depressed */
	    base = 10;
	else {       /* hexadecimal input of code, while AltGr depressed */
	    value -= 10;
	    base = 16;
	}

	if (keyb->npadch == -1)
	    keyb->npadch = value;
	else
	    keyb->npadch = keyb->npadch * base + value;
}

K_HANDLER(do_lock)
{
	if (down == 1)
               chg_vc_kbd_lock(kbd, value);
}

K_HANDLER(do_slock)
{
	do_shift(keyb, kbd, value, down);
	if (down != 1)
		return;
	chg_vc_kbd_slock(kbd, value);
	/* try to make Alt, oops, AltGr and such work */
	if (!key_maps[kbd->lockstate ^ kbd->slockstate]) {
		kbd->slockstate = 0;
		chg_vc_kbd_slock(kbd, value);
	}
}

unsigned char getledstate(struct kbd_struct *kbd)
{
       return kbd->keyb ? kbd->keyb->ledstate : 0;
}

void setledstate(struct kbd_struct *kbd, unsigned int led) 
{
	/* FIXME: What about multiple keyboards? */
	struct keyboard *keyb = kbd->keyb;

	if (!(led & ~7)) {
		keyb->ledioctl = led;
		/* FIXME: Kill led modes? */
		kbd->ledmode = LED_SHOW_IOCTL;
    	} else
		kbd->ledmode = LED_SHOW_FLAGS;
	set_leds();
}

static inline unsigned char getleds(struct keyboard *keyb)
{
       struct kbd_struct *kbd = keyb->current_kbd;

       if (!kbd)
               return 0;
       /* FIXME: This is obscure, fix it */	
       if (kbd->ledmode == LED_SHOW_IOCTL)                 
               return keyb->ledioctl;
       else
               return kbd->ledflagstate;
 }

#ifdef CONFIG_INPUT                                        /* FIXME */
/*
 *     This connect us to the input layer.	 
 */
static int keyboard_connect(struct input_handler *handler,struct input_dev *dev)
{
	struct keyboard *keyb;
	struct input_handle *handle;
        int i;
		
        if (!test_bit(EV_KEY, dev->evbit) ||
            !test_bit(KEY_A, dev->keybit) ||
            !test_bit(KEY_Z, dev->keybit))
                return -1;
        if (!(keyb = kmalloc(sizeof(struct keyboard), GFP_KERNEL)))
                return -1;
        memset(keyb, 0, sizeof(struct keyboard));
        memset(keyb->consoles, 0xff, sizeof(keyb->consoles));
        keyb->ledstate = 0xff;
        keyb->npadch = -1;
        keyb->fg_console = fg_console;
        keyb->want_console = -1;
        keyb->current_kbd = &kbd_table[fg_console];
        keyb->current_kbd->kbd_count++;
        handle = &keyb->handle;
        handle->dev = dev;
        handle->handler = handler;
        handle->private = keyb;
        compute_shiftstate(keyb);
        input_open_device(handle);
        printk("keyboard.c: Adding keyboard: input%d\n", dev->number);
	/* First real keyboard takes over dummykbd */
        for(i=0; i<MAX_NR_CONSOLES; i++) {
		struct kbd_struct *kbd = &kbd_table[i];
                if (kbd->keyb == &dummy_keyboard)
                        kbd->keyb = keyb;
        }
        tasklet_schedule(&console_tasklet);
	return 0;
}

static void keyboard_disconnect(struct input_handle *handle)
{
       printk("keyboard.c: Removing keyboard: input%d\n", handle->dev->number);
       input_close_device(handle);
       /* FIXME: We should free struct keyboard, but it should be locked 
	         somehow. */
}

struct input_handler keyboard_handler = {
       event:          keyboard_event,
       connect:        keyboard_connect,
       disconnect:     keyboard_disconnect,
};

#endif

/*
 *  This once was a separate BH handler for deferred keyboard communication
 *  (primarily setting of LED's).  Now it's just a subroutine of console_bh
 *  as we need to synchronize most of our actions with console switching.
 *
 *  Actually, switching of keyboards is now completely independent on
 *  visibility of the consoles -- keyboard switch invokes console
 *  switch, but the keystrokes are received by the new console even
 *  before the change is visible.
 */

void keyboard_bh(void)
{
       struct input_handle *handle;

      for(handle = keyboard_handler.handle; handle; handle = handle->hnext) {
               struct keyboard *keyb = handle->private;
               int want;
               u8 leds;

               if ((want = keyb->want_console) >= 0) {
                       if (vc_cons_allocated(want)) {
                               struct kbd_struct *old_kbd, *new_kbd;
                               old_kbd = &kbd_table[keyb->fg_console];
                               new_kbd = &kbd_table[want];
                               old_kbd->kbd_count--;
                               new_kbd->kbd_count++;
                               if (!old_kbd->kbd_count)
                                       hide_cursor(keyb->fg_console);
                               keyb->last_console = keyb->fg_console;
                               keyb->fg_console = want;
                               keyb->current_kbd = new_kbd;
                               new_kbd->keyb = keyb;
                               /* Schedule console switch on current display */+                               want_console = want;
                               /* FIXME: What shall we do if somebody decides to forbid console switch? */
                       }
                       keyb->want_console = -1;
               }
               leds = getleds(keyb);
               if (leds != keyb->ledstate) {
                   input_event(handle->dev,EV_LED, LED_SCROLLL,!!(leds & 0x01));
                   input_event(handle->dev, EV_LED, LED_NUML, !!(leds & 0x02));
                   input_event(handle->dev, EV_LED, LED_CAPSL, !!(leds & 0x04));
                   keyb->ledstate = leds;
               }
        }
}

EXPORT_SYMBOL(keyboard_tasklet);
DECLARE_TASKLET_DISABLED(keyboard_tasklet, kbd_bh, 0);

int __init kbd_init(void)
{
	int i;
	struct kbd_struct kbd0;
        memset(&dummy_keyboard.consoles,0xff, sizeof(dummy_keyboard.consoles));
        memset(&kbd0, 0, sizeof(kbd0));
        kbd0.keyb = &dummy_keyboard;
	kbd0.ledflagstate = kbd0.default_ledflagstate = KBD_DEFLEDS;
	kbd0.ledmode = LED_SHOW_FLAGS;
	kbd0.lockstate = KBD_DEFLOCK;
	kbd0.modeflags = KBD_DEFMODE;
	kbd0.kbdmode = VC_XLATE;
 
	for (i = 0 ; i < MAX_NR_CONSOLES ; i++)
		kbd_table[i] = kbd0;

	input_register_handler(&keyboard_handler);
#if 0	
	pm_kbd = pm_register(PM_SYS_DEV, PM_SYS_KBC, NULL);
#endif
	return 0;
}
