#ifndef _KBD_KERN_H
#define _KBD_KERN_H

#include <linux/interrupt.h>
#include <linux/keyboard.h>
#include <linux/input.h>

extern struct tasklet_struct keyboard_tasklet;

/*
 * Per-keyboard state.
 */

struct keyboard {
	unsigned char k_down[NR_SHIFT];    /* Shift state counters */
       	int dead_key_next;
       	int shift_state;
       	int npadch;                        /* -1 or number assembled on pad */
       	unsigned char diacr;
       	unsigned char ledstate;            /* 0xff = undefined */
       	unsigned char ledioctl;
#ifdef CONFIG_MAGIC_SYSRQ
       	unsigned char sysrq_pressed;
#endif
       	int spawnpid, spawnsig;

       	/* Consoles this keyboard has access to */
       	unsigned long consoles[256/BITS_PER_LONG]; 
      	/* Foreground console for this kbd */
       	int fg_console;    
       	/* Console we want to switch to */
       	int want_console;  
	/* Last console we've used */
       	int last_console;
	/* kbd_struct of our fg console */
	struct kbd_struct *current_kbd;
	/* Our connection to input layer */
        struct input_handle handle;                    
};

extern char *func_table[MAX_NR_FUNC]; /* FIXME: Move ioctl's to keyboard.c? */
extern char func_buf[];  	/* FIXME: Keymaps should be local to consoles */
extern char *funcbufptr;
extern int funcbufsize, funcbufleft;

/* Where did keyboard interrupt get us */
extern struct pt_regs *kbd_pt_regs;

/*
 * kbd->xxx contains the VC-local things (flag settings etc..)
 *
 * Note: externally visible are LED_SCR, LED_NUM, LED_CAP defined in kd.h
 *       The code in KDGETLED / KDSETLED depends on the internal and
 *       external order being the same.
 *
 * Note: lockstate is used as index in the array key_map.
 */
struct kbd_struct {
	/* FIXME: Kill bitfields? */
	/* keyboard we were connected to last time */
	struct keyboard *keyb;     
	/* number of keyboards attached to this console */
	unsigned char kbd_count;  
	unsigned char lockstate;
	/* 8 modifiers - the names do not have any meaning at all;
	   they can be associated to arbitrarily chosen keys */
#define VC_SHIFTLOCK	KG_SHIFT	/* shift lock mode */
#define VC_ALTGRLOCK	KG_ALTGR	/* altgr lock mode */
#define VC_CTRLLOCK	KG_CTRL 	/* control lock mode */
#define VC_ALTLOCK	KG_ALT  	/* alt lock mode */
#define VC_SHIFTLLOCK	KG_SHIFTL	/* shiftl lock mode */
#define VC_SHIFTRLOCK	KG_SHIFTR	/* shiftr lock mode */
#define VC_CTRLLLOCK	KG_CTRLL 	/* ctrll lock mode */
#define VC_CTRLRLOCK	KG_CTRLR 	/* ctrlr lock mode */
	unsigned char slockstate; 	/* for `sticky' Shift, Ctrl, etc. */

	unsigned char ledmode; 	/* one 2-bit value */
#define LED_SHOW_FLAGS 0        /* traditional state */
#define LED_SHOW_IOCTL 1        /* only change leds upon ioctl */

	unsigned char ledflagstate;	/* flags, not lights */
	unsigned char default_ledflagstate;
#define VC_SCROLLOCK	0	/* scroll-lock mode */
#define VC_NUMLOCK	1	/* numeric lock mode */
#define VC_CAPSLOCK	2	/* capslock mode */

	unsigned char kbdmode;	/* one 2-bit value */
#define VC_XLATE	0	/* translate keycodes using keymap */
#define VC_MEDIUMRAW	1	/* medium raw (keycode) mode */
#define VC_RAW		2	/* raw (scancode) mode */
#define VC_UNICODE	3	/* Unicode mode */

	unsigned char modeflags;
#define VC_APPLIC	0	/* application key mode */
#define VC_CKMODE	1	/* cursor key mode */
#define VC_REPEAT	2	/* keyboard repeat */
#define VC_CRLF		3	/* 0 - enter sends CR, 1 - enter sends CRLF */
#define VC_META		4	/* 0 - meta, 1 - meta=prefix with ESC */
};

extern struct kbd_struct kbd_table[];
extern struct tasklet_struct console_tasklet;

extern int do_poke_blanked_console;

static inline void kbd_set_console(struct keyboard *keyb, int cons)
{
	keyb->want_console = cons;
	tasklet_schedule(&console_tasklet);
}

extern inline void set_leds(void)
{
	tasklet_schedule(&console_tasklet);
}

extern inline int vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	return ((kbd->modeflags >> flag) & 1);
}

extern inline int vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	return ((kbd->ledflagstate >> flag) & 1);
}

extern inline void set_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags |= 1 << flag;
}

extern inline void set_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate |= 1 << flag;
}

extern inline void clr_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags &= ~(1 << flag);
}

extern inline void clr_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate &= ~(1 << flag);
}

extern inline void chg_vc_kbd_lock(struct kbd_struct * kbd, int flag)
{
	kbd->lockstate ^= 1 << flag;
}

extern inline void chg_vc_kbd_slock(struct kbd_struct * kbd, int flag)
{
	kbd->slockstate ^= 1 << flag;
}

extern inline void chg_vc_kbd_mode(struct kbd_struct * kbd, int flag)
{
	kbd->modeflags ^= 1 << flag;
}

extern inline void chg_vc_kbd_led(struct kbd_struct * kbd, int flag)
{
	kbd->ledflagstate ^= 1 << flag;
}

#define U(x) ((x) ^ 0xf000)

/* keyboard.c */

struct console;

void compute_shiftstate(struct keyboard *keyb);
int keyboard_wait_for_keypress(struct console *);
int kbd_init(void);
unsigned char getledstate(struct kbd_struct *kbd);
void setledstate(struct kbd_struct *kbd, unsigned int led);
/* FIXME: Who's calling it? */
void put_queue(struct kbd_struct *kbd, int data);      
void keyboard_bh(void);


/* defkeymap.c */

extern unsigned int keymap_count;

/* console.c */

extern task_queue con_task_queue;

extern inline void con_schedule_flip(struct tty_struct *t)
{
	queue_task(&t->flip.tqueue, &con_task_queue);
	tasklet_schedule(&console_tasklet);
}

/* misc */

extern void ctrl_alt_del(void);

#endif
