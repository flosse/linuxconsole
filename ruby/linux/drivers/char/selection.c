/*
 * linux/drivers/char/selection.c
 *
 * This module exports the functions:
 *
 *     'int set_selection(const unsigned long arg)'
 *     'void clear_selection(void)'
 *     'int paste_selection(struct tty_struct *tty)'
 *     'int sel_loadlut(const unsigned long arg)'
 *
 * Now that /dev/vcs exists, most of this can disappear again.
 *
 * Adapted for selection in Unicode by <edmund@rano.demon.co.uk>, January 1999
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <asm/uaccess.h>

#include <linux/vt_kern.h>
#include <linux/consolemap.h>
#include <linux/selection.h>

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
#endif

/* Don't take this from <ctype.h>: 011-015 on the screen aren't spaces */
#define isspace(c)	((c) == ' ')

/* Variables for selection control. */
/* Use a dynamic buffer, instead of static (Dec 1994) */
int sel_cons;				/* must not be disallocated */
static volatile int sel_start = -1; 	/* cleared by clear_selection */
static int sel_end;
static int sel_buffer_lth;
static u16 *sel_buffer;

/* clear_selection, highlight and highlight_pointer can be called
   from interrupt (via scrollback/front) */

/* set reverse video on characters s-e of console with selection. */
inline static void highlight(const int s, const int e) {
	invert_screen(find_vc(sel_cons), s, e-s+2, 1);
}

/* use complementary color to show the pointer */
inline static void highlight_pointer(const int where) {
	complement_pos(find_vc(sel_cons), where);
}

/* used by selection */
u16 screen_glyph(struct vc_data *vc, int offset)
{
        u16 w = scr_readw(screenpos(vc, offset, 1));                                    u16 c = w & 0xff;

        if (w & vc->vc_hi_font_mask)
                c |= 0x100;
        return c;
}             

static u16 sel_pos(int n)
{
	return inverse_convert(find_vc(sel_cons), screen_glyph(find_vc(sel_cons), n));
}

/* 
 * remove the current selection highlight, if any,
 * from the console holding the selection. 
 */
void clear_selection(void) {
	highlight_pointer(-1); /* hide the pointer */
	if (sel_start != -1) {
		highlight(sel_start, sel_end);
		sel_start = -1;
	}
}

/*
 * User settable table: what characters are to be considered alphabetic?
 * 256 bits
 */
static u32 inwordLut[8]={
  0x00000000, /* control chars     */
  0x03FF0000, /* digits            */
  0x87FFFFFE, /* uppercase and '_' */
  0x07FFFFFE, /* lowercase         */
  0x00000000,
  0x00000000,
  0xFF7FFFFF, /* latin-1 accented letters, not multiplication sign */
  0xFF7FFFFF  /* latin-1 accented letters, not division sign */
};

static inline int inword(const u16 c) {
	/* Everything over 0xff is considered alphabetic! */
	return c >= 0x100 || ( inwordLut[c>>5] >> (c & 0x1F) ) & 1;
}

/* set inwordLut contents. Invoked by ioctl(). */
int sel_loadlut(const unsigned long arg)
{
	int err = -EFAULT;

	if (!copy_from_user(inwordLut, (u32 *)(arg+4), 32))
		err = 0;
	return err;
}

/* does screen address p correspond to character at LH/RH edge of screen? */
static inline int atedge(const int p, int size_row)
{
	return (!(p % size_row)	|| !((p + 2) % size_row));
}

/* constrain v such that v <= u */
static inline unsigned short limit(const unsigned short v, const unsigned short u)
{
	return (v > u) ? u : v;
}

/* set the current selection. Invoked by ioctl() or by kernel code. */
int set_selection(const unsigned long arg, struct tty_struct *tty, int user)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	int sel_mode, new_sel_start, new_sel_end, spc;
	u16 *bp, *obp;
	int i, ps, pe;
	u16 ucs;

	poke_blanked_console(vc->display_fg);

	{ unsigned short *args, xs, ys, xe, ye;

	  args = (unsigned short *)(arg + 1);
	  if (user) {
	  	  int err;
		  err = verify_area(VERIFY_READ, args, sizeof(short) * 5);
		  if (err)
		  	return err;
		  get_user(xs, args++);
		  get_user(ys, args++);
		  get_user(xe, args++);
		  get_user(ye, args++);
		  get_user(sel_mode, args);
	  } else {
		  xs = *(args++); /* set selection from kernel */
		  ys = *(args++);
		  xe = *(args++);
		  ye = *(args++);
		  sel_mode = *args;
	  }
	  xs--; ys--; xe--; ye--;
	  xs = limit(xs, vc->vc_cols - 1);
	  ys = limit(ys, vc->vc_rows - 1);
	  xe = limit(xe, vc->vc_cols - 1);
	  ye = limit(ye, vc->vc_rows - 1);
	  ps = ys * vc->vc_size_row + (xs << 1);
	  pe = ye * vc->vc_size_row + (xe << 1);

	  if (sel_mode == 4) {
	      /* useful for screendump without selection highlights */
	      clear_selection();
	      return 0;
	  }

	  if (mouse_reporting(tty) && (sel_mode & 16)) {
	      mouse_report(tty, sel_mode & 15, xs, ys);
	      return 0;
	  }
        }

	if (ps > pe)	/* make sel_start <= sel_end */
	{
		int tmp = ps;
		ps = pe;
		pe = tmp;
	}

	if (sel_cons != vc->display_fg->fg_console->vc_num) {
		clear_selection();
		sel_cons = vc->display_fg->fg_console->vc_num;
	}

	switch (sel_mode)
	{
		case 0:	/* character-by-character selection */
			new_sel_start = ps;
			new_sel_end = pe;
			break;
		case 1:	/* word-by-word selection */
			spc = isspace(sel_pos(ps));
			for (new_sel_start = ps; ; ps -= 2)
			{
				if ((spc && !isspace(sel_pos(ps))) ||
				    (!spc && !inword(sel_pos(ps))))
					break;
				new_sel_start = ps;
				if (!(ps % vc->vc_size_row))
					break;
			}
			spc = isspace(sel_pos(pe));
			for (new_sel_end = pe; ; pe += 2)
			{
				if ((spc && !isspace(sel_pos(pe))) ||
				    (!spc && !inword(sel_pos(pe))))
					break;
				new_sel_end = pe;
				if (!((pe + 2) % vc->vc_size_row))
					break;
			}
			break;
		case 2:	/* line-by-line selection */
			new_sel_start = ps - ps % vc->vc_size_row;
			new_sel_end = pe + vc->vc_size_row
				    - pe % vc->vc_size_row - 2;
			break;
		case 3:
			highlight_pointer(pe);
			return 0;
		default:
			return -EINVAL;
	}

	/* remove the pointer */
	highlight_pointer(-1);

	/* select to end of line if on trailing space */
	if (new_sel_end > new_sel_start &&
		!atedge(new_sel_end, vc->vc_size_row) &&
		isspace(sel_pos(new_sel_end))) {
		for (pe = new_sel_end + 2; ; pe += 2)
			if (!isspace(sel_pos(pe)) ||
			    atedge(pe, vc->vc_size_row))
				break;
		if (isspace(sel_pos(pe)))
			new_sel_end = pe;
	}
	if (sel_start == -1)	/* no current selection */
		highlight(new_sel_start, new_sel_end);
	else if (new_sel_start == sel_start)
	{
		if (new_sel_end == sel_end)	/* no action required */
			return 0;
		else if (new_sel_end > sel_end)	/* extend to right */
			highlight(sel_end + 2, new_sel_end);
		else				/* contract from right */
			highlight(new_sel_end + 2, sel_end);
	}
	else if (new_sel_end == sel_end)
	{
		if (new_sel_start < sel_start)	/* extend to left */
			highlight(new_sel_start, sel_start - 2);
		else				/* contract from left */
			highlight(sel_start, new_sel_start - 2);
	}
	else	/* some other case; start selection from scratch */
	{
		clear_selection();
		highlight(new_sel_start, new_sel_end);
	}
	sel_start = new_sel_start;
	sel_end = new_sel_end;

	/* Allocate a new buffer before freeing the old one ... */
	bp = kmalloc(((sel_end-sel_start)/2+1) * sizeof(u16), GFP_KERNEL);
	if (!bp) {
		printk(KERN_WARNING "selection: kmalloc() failed\n");
		clear_selection();
		return -ENOMEM;
	}
	if (sel_buffer)
		kfree(sel_buffer);
	sel_buffer = bp;

	obp = bp;
	for (i = sel_start; i <= sel_end; i += 2) {
		ucs = sel_pos(i);
		*bp++ = ucs;
		if (!isspace(ucs))
			obp = bp;
		if (! ((i + 2) % vc->vc_size_row)) {
			/* strip trailing blanks from line and add newline,
			   unless non-space at end of line. */
			if (obp != bp) {
				bp = obp;
				*bp++ = '\r';
			}
			obp = bp;
		}
	}
	sel_buffer_lth = bp - sel_buffer;
	return 0;
}

/* Insert the contents of the selection buffer into the
 * queue of the tty associated with the current console.
 * Invoked by ioctl().
 */
int paste_selection(struct tty_struct *tty)
{
	struct vc_data *vc = (struct vc_data *) tty->driver_data;
	unsigned char *paste_buffer, *bp, *p;
	DECLARE_WAITQUEUE(wait, current);
	int utf, count;

      	if (!sel_buffer || !sel_buffer_lth) return 0;

        /* Paste UTF-8 iff the keyboard is in UTF-8 mode */
        utf = (vc->display_fg->fg_console->kbd_table.kbdmode == VC_UNICODE);

        /* Make a paste buffer containing an appropriate translation
           of the selection buffer */
        paste_buffer = kmalloc(utf ? sel_buffer_lth*3 : sel_buffer_lth, GFP_KERNEL);
        if (!paste_buffer) {
        	printk(KERN_WARNING "selection: kmalloc() failed\n");
               	return -ENOMEM;
       	}

       	bp = paste_buffer;
       	if (utf) {
        	/* convert to UTF-8 */
               	int i, ucs;

               	for (i = 0; i < sel_buffer_lth; i++) {
                	ucs = sel_buffer[i];
                       	/* The following code should at some point
                           be merged with to_utf8() in keyboard.c */
                       	if (!(ucs & ~0x7f)) /* 0?????? */
                        	*bp++ = ucs;
                       	else if (!(ucs & ~0x7ff)) { /* 110????? 10?????? */
                        	*bp++ = 0xc0 | (ucs >> 6);
                                *bp++ = 0x80 | (ucs & 0x3f);
                        }
                        else { /*  1110???? 10?????? 10?????? */
                                *bp++ = 0xe0 | (ucs >> 12);
                                *bp++ = 0x80 | ((ucs >> 6) & 0x3f);
                                *bp++ = 0x80 | (ucs & 0x3f);
                        }
                        /* UTF-8 is defined for words of up to 31 bits,
                           but we need only 16 bits here */
		}
	} else {
		/* convert to 8-bit */
                int inv_translate = vc->display_fg->fg_console->vc_translate;
                int i;
                unsigned char c;

                for (i = 0; i < sel_buffer_lth; i++) {
                	c = inverse_translate(inv_translate, sel_buffer[i]);
                       	if (c) *bp++ = c;
               	}
	}

	poke_blanked_console(vc->display_fg);
	add_wait_queue(&vc->paste_wait, &wait);
	p = paste_buffer;
	while (bp > p) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (test_bit(TTY_THROTTLED, &tty->flags)) {
			schedule();
			continue;
		}
		count = bp - p;
		count = MIN(count, tty->ldisc.receive_room(tty));
		tty->ldisc.receive_buf(tty, p, 0, count);
		p += count;
	}
	remove_wait_queue(&vc->paste_wait, &wait);
	current->state = TASK_RUNNING;
	kfree(paste_buffer);
	return 0;
}

EXPORT_SYMBOL(set_selection);
EXPORT_SYMBOL(paste_selection);
