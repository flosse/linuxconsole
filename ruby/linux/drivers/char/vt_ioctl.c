/*
 *  linux/drivers/char/vt_ioctl.c
 *
 *  Copyright (C) 1992 obz under the linux copyright
 *
 *  Dynamic diacritical handling - aeb@cwi.nl - Dec 1993
 *  Dynamic keymap and string allocation - aeb@cwi.nl - May 1994
 *  Restrict VT switching via ioctl() - grif@cs.ucr.edu - Dec 1995
 *  Some code moved for less code duplication - Andi Kleen - Mar 1997
 *  Made VC ioctls truly SYSV complient. Rewritten to support 
 *  multihead systems - James Simmons - Sept 2000
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/console.h>
#include <linux/spinlock.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/vt_kern.h>
#include <linux/kbd_diacr.h>
#include <linux/selection.h>

#ifdef CONFIG_FB_COMPAT_XPMAC
#include <asm/vc_ioctl.h>
#endif /* CONFIG_FB_COMPAT_XPMAC */

extern struct tty_driver console_driver;

#define VT_IS_IN_USE(vc) (vc->vc_tty && vc->vc_tty->count)
#define VT_BUSY(vc)	(VT_IS_IN_USE(vc) ||vc->vc_num == sel_cons)

/*
 * Console (vt and kd) routines, as defined by USL SVR4 manual, and by
 * experimentation and study of X386 SYSV handling.
 *
 * SYSV vt's are /dev/vtX, which X >= 0, and /dev/console is a separate ttyp.
 * Linux used to followed a different method but now that we support multihead
 * VT 0 as the foreground console no longer makes sense. Now Linux follows
 * SYSV where, /dev/tty is equal to /dev/console, and the vc start at
 * /dev/ttyX, X >= 0. So we will always treat our set of vt as numbered ttys
 * 0..MAX_NR_CONSOLES-1. Using using /dev/tty as a target is legal, since an
 * implicit aliasing to the current console is done by the main ioctl code.
 */

struct vt_struct *vt_cons = NULL;

/* Keyboard type: Default is KB_101, but can be set by machine
 * specific code.
 */
unsigned char keyboard_type = KB_101;

#if !defined(__alpha__) && !defined(__ia64__) && !defined(__mips__) && !defined(__arm__) && !defined(__sh__)
asmlinkage long sys_ioperm(unsigned long from, unsigned long num, int on);
#endif

/*
 * these are the valid i/o ports we're allowed to change. they map all the
 * video ports
 */
#define GPFIRST 0x3b4
#define GPLAST 0x3df
#define GPNUM (GPLAST - GPFIRST + 1)

/*
 * Generates sound of some frequency for some number of clock ticks
 *
 * If freq is 0, will turn off sound, else will turn it on for that time.
 * If msec is 0, will return immediately, else will sleep for msec time, then
 * turn sound off.
 *
 * We also return immediately, which is what was implied within the X
 * comments - KDMKTONE doesn't put the process to sleep.
 */

#if defined(__i386__) || defined(__alpha__) || defined(__powerpc__) \
    || (defined(__mips__) && !defined(CONFIG_SGI_IP22)) \
    || (defined(__arm__) && defined(CONFIG_HOST_FOOTBRIDGE))

static spinlock_t beep_lock = SPIN_LOCK_UNLOCKED;

static void
kd_nosound(unsigned long ignored)
{
	/* disable counter 2 */
	outb(inb_p(0x61)&0xFC, 0x61);
	return;
}

void
_kd_mksound(unsigned int hz, unsigned int ticks)
{
	static struct timer_list sound_timer = { function: kd_nosound };
	unsigned int count = 0;
	unsigned long flags;

	if (hz > 20 && hz < 32767)
		count = 1193180 / hz;

	spin_lock_irqsave(&beep_lock, flags);	
	del_timer(&sound_timer);
	if (count) {
		/* enable counter 2 */
		outb_p(inb_p(0x61)|3, 0x61);
		/* set command for counter 2, 2 byte write */
		outb_p(0xB6, 0x43);
		/* select desired HZ */
		outb_p(count & 0xff, 0x42);
		outb((count >> 8) & 0xff, 0x42);

		if (ticks) {
			sound_timer.expires = jiffies+ticks;
			add_timer(&sound_timer);
		}
	} else
		kd_nosound(0);
	spin_unlock_irqrestore(&beep_lock, flags);
	return;
}

#else

void
_kd_mksound(unsigned int hz, unsigned int ticks)
{
}

#endif

void (*kd_mksound)(unsigned int hz, unsigned int ticks) = _kd_mksound;

/*
 * Sometimes we want to wait until a particular VT has been activated. We
 * do it in a very simple manner. Everybody waits on a single queue and
 * get woken up at once. Those that are satisfied go on with their business,
 * while those not ready go back to sleep. Seems overkill to add a wait
 * to each vt just for this - usually this does nothing!
 */
static DECLARE_WAIT_QUEUE_HEAD(vt_activate_queue);

/*
 * Sleeps until a vt is activated, or the task is interrupted. Returns
 * 0 if activation, -EINTR if interrupted.
 */
int vt_waitactive(struct vc_data *vc)
{
	int retval;
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&vt_activate_queue, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		retval = 0;
		if (vc->vc_num == vc->display_fg->fg_console->vc_num)
			break;
		retval = -EINTR;
		if (signal_pending(current))
			break;
		schedule();
	}
	remove_wait_queue(&vt_activate_queue, &wait);
	current->state = TASK_RUNNING;
	return retval;
}

#define i (tmp.kb_index)
#define s (tmp.kb_table)
#define v (tmp.kb_value)
static inline int
do_kdsk_ioctl(int cmd, struct kbentry *user_kbe, int perm, struct kbd_struct *kbd)
{
	struct kbentry tmp;
	ushort *key_map, val, ov;

	if (copy_from_user(&tmp, user_kbe, sizeof(struct kbentry)))
		return -EFAULT;
	if (i >= NR_KEYS || s >= MAX_NR_KEYMAPS)
		return -EINVAL;	

	switch (cmd) {
	case KDGKBENT:
		key_map = key_maps[s];
		if (key_map) 
		    val = U(key_map[i]);
		else
		    val = (i ? K_HOLE : K_NOSUCHMAP);
		return put_user(val, &user_kbe->kb_value);
	case KDSKBENT:
		if (!perm)
			return -EPERM;
		if (!i && v == K_NOSUCHMAP) {
			/* disallocate map */
			key_map = key_maps[s];
			if (s && key_map) {
			    key_maps[s] = 0;
			    if (key_map[0] == U(K_ALLOCATED)) {
					kfree(key_map);
					keymap_count--;
			    }
			}
			break;
		}

		if ((KTYP(v) >= NR_TYPES || KVAL(v) > max_vals[KTYP(v)]) &&
			(v & 0xf000))
                	return -EINVAL;

		/* ++Geert: non-PC keyboards may generate keycode zero */
#if !defined(__mc68000__) && !defined(__powerpc__)
		/* assignment to entry 0 only tests validity of args */
		if (!i)
			break;
#endif

		if (!(key_map = key_maps[s])) {
			int j;

			if (keymap_count >= MAX_NR_OF_USER_KEYMAPS &&
			    !capable(CAP_SYS_RESOURCE))
				return -EPERM;

			key_map = (ushort *) kmalloc(sizeof(plain_map),
						     GFP_KERNEL);
			if (!key_map)
				return -ENOMEM;
			key_maps[s] = key_map;
			key_map[0] = U(K_ALLOCATED);
			for (j = 1; j < NR_KEYS; j++)
				key_map[j] = U(K_HOLE);
			keymap_count++;
		}
		ov = U(key_map[i]);
		if (v == ov)
			break;	/* nothing to do */
		/*
		 * Attention Key.
		 */
		if (((ov == K_SAK) || (v == K_SAK)) && !capable(CAP_SYS_ADMIN))
			return -EPERM;
		key_map[i] = U(v);
		if (!s && (KTYP(ov) == KT_SHIFT || KTYP(v) == KT_SHIFT))
			compute_shiftstate();
		break;
	}
	return 0;
}
#undef i
#undef s
#undef v

static inline int 
do_kbkeycode_ioctl(int cmd, struct kbkeycode *user_kbkc, int perm)
{
	struct kbkeycode tmp;
	int kc = 0;

	if (copy_from_user(&tmp, user_kbkc, sizeof(struct kbkeycode)))
		return -EFAULT;
	switch (cmd) {
	case KDGETKEYCODE:
		kc = getkeycode(tmp.scancode);
		if (kc >= 0)
			kc = put_user(kc, &user_kbkc->keycode);
		break;
	case KDSETKEYCODE:
		if (!perm)
			return -EPERM;
		kc = setkeycode(tmp.scancode, tmp.keycode);
		break;
	}
	return kc;
}

static inline int
do_kdgkb_ioctl(int cmd, struct kbsentry *user_kdgkb, int perm)
{
	struct kbsentry tmp;
	char *p;
	u_char *q;
	int sz;
	int delta;
	char *first_free, *fj, *fnw;
	int i, j, k;

	/* we mostly copy too much here (512bytes), but who cares ;) */
	if (copy_from_user(&tmp, user_kdgkb, sizeof(struct kbsentry)))
		return -EFAULT;
	tmp.kb_string[sizeof(tmp.kb_string)-1] = '\0';
	if (tmp.kb_func >= MAX_NR_FUNC)
		return -EINVAL;
	i = tmp.kb_func;

	switch (cmd) {
	case KDGKBSENT:
		sz = sizeof(tmp.kb_string) - 1; /* sz should have been
						  a struct member */
		q = user_kdgkb->kb_string;
		p = func_table[i];
		if(p)
			for ( ; *p && sz; p++, sz--)
				put_user(*p, q++);
		put_user('\0', q);
		return ((p && *p) ? -EOVERFLOW : 0);
	case KDSKBSENT:
		if (!perm)
			return -EPERM;

		q = func_table[i];
		first_free = funcbufptr + (funcbufsize - funcbufleft);
		for (j = i+1; j < MAX_NR_FUNC && !func_table[j]; j++) 
			;
		if (j < MAX_NR_FUNC)
			fj = func_table[j];
		else
			fj = first_free;

		delta = (q ? -strlen(q) : 1) + strlen(tmp.kb_string);
		if (delta <= funcbufleft) { 	/* it fits in current buf */
		    if (j < MAX_NR_FUNC) {
			memmove(fj + delta, fj, first_free - fj);
			for (k = j; k < MAX_NR_FUNC; k++)
			    if (func_table[k])
				func_table[k] += delta;
		    }
		    if (!q)
		      func_table[i] = fj;
		    funcbufleft -= delta;
		} else {			/* allocate a larger buffer */
		    sz = 256;
		    while (sz < funcbufsize - funcbufleft + delta)
		      sz <<= 1;
		    fnw = (char *) kmalloc(sz, GFP_KERNEL);
		    if(!fnw)
		      return -ENOMEM;

		    if (!q)
		      func_table[i] = fj;
		    if (fj > funcbufptr)
			memmove(fnw, funcbufptr, fj - funcbufptr);
		    for (k = 0; k < j; k++)
		      if (func_table[k])
			func_table[k] = fnw + (func_table[k] - funcbufptr);

		    if (first_free > fj) {
			memmove(fnw + (fj - funcbufptr) + delta, fj, first_free - fj);
			for (k = j; k < MAX_NR_FUNC; k++)
			  if (func_table[k])
			    func_table[k] = fnw + (func_table[k] - funcbufptr) + delta;
		    }
		    if (funcbufptr != func_buf)
		      kfree(funcbufptr);
		    funcbufptr = fnw;
		    funcbufleft = funcbufleft - delta + sz - funcbufsize;
		    funcbufsize = sz;
		}
		strcpy(func_table[i], tmp.kb_string);
		break;
	}
	return 0;
}

/*
 *  Font switching
 *
 *  Currently we only support fonts up to 32 pixels wide, at a maximum height
 *  of 32 pixels. Userspace fontdata is stored with 32 bytes (shorts/ints,
 *  depending on width) reserved for each character which is kinda wasty, but
 *  this is done in order to maintain compatibility with the EGA/VGA fonts. It
 *  is upto the actual low-level console-driver convert data into its favorite
 *  format (maybe we should add a `fontoffset' field to the `display'
 *  structure so we wont have to convert the fontdata all the time.
 *  /Jes
 */                     
#define max_font_size 65536

int con_font_op(struct vc_data *vc, struct console_font_op *op)
{
	int err = -EINVAL, size = max_font_size;
	struct console_font_op old_op;
	u8 *temp = NULL;

	if (vc->vc_mode != KD_TEXT)
		return -EINVAL;

	switch (op->op) {
	case KD_FONT_OP_SET:
		/* We need data from userland */
		if (!op->data || !op->charcount)
			return -EINVAL;
		/* Need to guess font height [compat]. In the old days you
		   could pass in console_font_op w/o a heigth and figure
		   out the size. We recommend you pass in console_font_op
		   with the height set.*/
		if (!op->height) {      
		  	int h, i;
                       	u8 *charmap = op->data, tmp;              
                       	/* If from KDFONTOP ioctl, don't allow things 
		   	   which can be done in userland, so that we can
                           get rid of this soon. Ha Ha. Not */
                       	if (!(op->flags & KD_FONT_FLAG_OLD))
                               	return -EINVAL;
                       	for (h = 32; h > 0; h--)
                               	for (i = 0; i < op->charcount; i++) {
                                       	if (get_user(tmp, &charmap[32*i+h-1]))
                                               	return -EFAULT;
                                       	if (tmp)
                                               	goto nonzero;
                               	}
			return -EINVAL;
                nonzero:
                       	op->height = h;
                }
                if (op->width > 32 || op->height > 32)
                	return -EINVAL;        
                size = (op->width+7)/8 * 32 * op->charcount;
                if (size > max_font_size)
                       	return -ENOSPC;
		/* Okay the font passed in doesn't do anything nasty. 
		   Now we grab the font data from userland */
		temp = kmalloc(size, GFP_KERNEL);
		if (!temp)
			return -ENOMEM;
		if (copy_from_user(temp, op->data, size)) {
			kfree(temp);
			return -EFAULT;
		}
		op->data = temp;
		spin_lock_irq(&vc->display_fg->vt_lock);
		err = vc->display_fg->vt_sw->con_font_op(vc, op);
		spin_unlock_irq(&vc->display_fg->vt_lock);
		if (!err)
			vc->vc_font = *op;
		break;
	case KD_FONT_OP_GET:
		/* Save passed in console_font_op */
		memcpy(&old_op, op, sizeof(old_op));
		/* allocate space for font data */
		if (op->data) {
			temp = kmalloc(size, GFP_KERNEL);
			if (!temp)
				return -ENOMEM;
			op->data = temp;
		}
		/* Get font data for present console */	
		err = vc->display_fg->vt_sw->con_font_op(vc, op);
		
 		/* point userland font data space back to passed in font_op */	
		op->data = old_op.data;
		if (!err) {
			/* This data is from the current VC, not userland 
			   we figure out how much memory the VC is using */
			size = (op->width+7)/8 * 32 * op->charcount;
		
			/* Next we see if userland passed in space and 
			   see if userland requested more chars than
			   what the VC has */	
               		if (op->data && op->charcount > old_op.charcount) {
				kfree(temp);
                       		return -ENOSPC;
			}
               		if (!(op->flags & KD_FONT_FLAG_OLD)) {
                       		if (op->height > old_op.height ||
			    		op->width > old_op.width) { 
					kfree(temp);
					return -ENOSPC;
				}
               		} else if ((old_op.height && op->height > old_op.height)
				  || op->height > 32) {
					kfree(temp);
                              		return -ENOSPC;                
			}	    
                }
                if (!err && op->data && copy_to_user(op->data, temp, size)) {
			kfree(temp);
                        return -EFAULT;
		}		
		break;
	case KD_FONT_OP_COPY:
		vc->vc_font = vc->display_fg->fg_console->vc_font;
		break;
	case KD_FONT_OP_SET_DEFAULT:
		vc->vc_font = vc->display_fg->default_mode->vc_font;	
		err = vc->display_fg->vt_sw->con_font_op(vc, op);
		break;
	default:
		return -EINVAL;
	}
	return err;
}

static inline int 
do_fontx_ioctl(struct vc_data *vc, int cmd, struct consolefontdesc *user_cfd, 
	       int perm)
{
	struct consolefontdesc cfdarg;
	struct console_font_op op;
	int i;

	if (copy_from_user(&cfdarg, user_cfd, sizeof(struct consolefontdesc))) 
		return -EFAULT;
 	
	switch (cmd) {
	case PIO_FONTX:
		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET;
		op.flags = KD_FONT_FLAG_OLD;
		op.width = 8;
		op.height = cfdarg.charheight;
		op.charcount = cfdarg.charcount;
		op.data = cfdarg.chardata;
		return con_font_op(vc, &op);
	case GIO_FONTX: {
		op.op = KD_FONT_OP_GET;
		op.flags = KD_FONT_FLAG_OLD;
		op.width = 8;
		op.height = cfdarg.charheight;
		op.charcount = cfdarg.charcount;
		op.data = cfdarg.chardata;
		i = con_font_op(vc, &op);
		if (i)
			return i;
		cfdarg.charheight = op.height;
		cfdarg.charcount = op.charcount;
		if (copy_to_user(user_cfd, &cfdarg, sizeof(struct consolefontdesc)))
			return -EFAULT;
		return 0;
		}
	}
	return -EINVAL;
}

static inline int 
do_unimap_ioctl(struct vc_data *vc,int cmd, struct unimapdesc *user_ud,int perm)
{
	struct unimapdesc tmp;
	int i = 0; 

	if (copy_from_user(&tmp, user_ud, sizeof tmp))
		return -EFAULT;
	if (tmp.entries) {
		i = verify_area(VERIFY_WRITE, tmp.entries, 
						tmp.entry_ct*sizeof(struct unipair));
		if (i) return i;
	}
	switch (cmd) {
	case PIO_UNIMAP:
		if (!perm)
			return -EPERM;
		return con_set_unimap(vc->display_fg->fg_console, tmp.entry_ct,
				      tmp.entries);
	case GIO_UNIMAP:
		return con_get_unimap(vc->display_fg->fg_console, tmp.entry_ct,
				      &(user_ud->entry_ct), tmp.entries);
	}
	return 0;
}

/*
 * Load palette into the DAC registers. arg points to a colour
 * map, 3 bytes per colour, 16 colours, range from 0 to 255.
 */
int con_set_cmap(struct vc_data *vc, unsigned char *arg)
{
	struct vt_struct *vt = vc->display_fg;
	int red[16], green[16], blue[16];
        int i, j, k;

        for (i = 0; i < 16; i++) {
                get_user(red[i], arg++);
                get_user(green[i], arg++);
                get_user(blue[i], arg++);
        }
        for (i = 0; i < MAX_NR_USER_CONSOLES; i++) {
		struct vc_data *tmp = vt->vc_cons[i];
                if (tmp) {
                	for (j = k = 0; j < 16; j++) {
                                tmp->vc_palette[k++] = red[j];
                                tmp->vc_palette[k++] = green[j];
                                tmp->vc_palette[k++] = blue[j];
                        }
                } 
        }
	set_palette(vc->display_fg->fg_console);
        return 0;
}

int con_get_cmap(struct vc_data *vc, unsigned char *arg)
{
        int i;

        for (i = 0; i < 16; i++) {
                put_user(vc->vc_palette[i], arg++);
                put_user(vc->vc_palette[i], arg++);
                put_user(vc->vc_palette[i], arg++);
        }
        return 0;
}
              
void do_blank_screen(struct vc_data *vc)
{
        if (vc->display_fg->vt_blanked)
                return;

        /* entering graphics mode? */
        hide_cursor(vc);
        vc->display_fg->vt_sw->con_blank(vc, -1);
        vc->display_fg->vt_blanked = 1;
        set_origin(vc);
        return;
}                               

/*
 * We handle the console-specific ioctl's here.  We allow the
 * capability to modify any console, not just the fg_console. 
 */
int vt_ioctl(struct tty_struct *tty, struct file * file,
	     unsigned int cmd, unsigned long arg)
{
	struct vc_data *vc = (struct vc_data *)tty->driver_data;
	struct kbd_struct * kbd;
	unsigned char ucval;
	int i, perm;

	if (!vc) 	/* impossible? */
		return -ENOIOCTLCMD;

	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or super-user.
	 */
	perm = 0;
	if (current->tty == tty || suser())
		perm = 1;
 
	kbd = &vc->kbd_table;
	switch (cmd) {
	case KIOCSOUND:
		if (!perm)
			return -EPERM;
		if (arg)
			arg = 1193180 / arg;
		kd_mksound(arg, 0);
		return 0;

	case KDMKTONE:
		if (!perm)
			return -EPERM;
	{
		unsigned int ticks, count;
		
		/*
		 * Generate the tone for the appropriate number of ticks.
		 * If the time is zero, turn off sound ourselves.
		 */
		ticks = HZ * ((arg >> 16) & 0xffff) / 1000;
		count = ticks ? (arg & 0xffff) : 0;
		if (count)
			count = 1193180 / count;
		kd_mksound(count, ticks);
		return 0;
	}

	case KDGKBTYPE:
		/*
		 * this is naive.
		 */
		ucval = keyboard_type;
		goto setchar;

#if !defined(__alpha__) && !defined(__ia64__) && !defined(__mips__) && !defined(__arm__) && !defined(__sh__)
		/*
		 * These cannot be implemented on any machine that implements
		 * ioperm() in user level (such as Alpha PCs).
		 */
	case KDADDIO:
	case KDDELIO:
		/*
		 * KDADDIO and KDDELIO may be able to add ports beyond what
		 * we reject here, but to be safe...
		 */
		if (arg < GPFIRST || arg > GPLAST)
			return -EINVAL;
		return sys_ioperm(arg, 1, (cmd == KDADDIO)) ? -ENXIO : 0;

	case KDENABIO:
	case KDDISABIO:
		return sys_ioperm(GPFIRST, GPNUM,
				  (cmd == KDENABIO)) ? -ENXIO : 0;
#endif

	case KDSETMODE:
		/*
		 * currently, setting the mode from KD_TEXT to KD_GRAPHICS
		 * doesn't do a whole lot. i'm not sure if it should do any
		 * restoration of modes or what...
		 */
		if (!perm)
			return -EPERM;
		switch (arg) {
		case KD_GRAPHICS:
			break;
		case KD_TEXT0:
		case KD_TEXT1:
			arg = KD_TEXT;
		case KD_TEXT:
			break;
		default:
			return -EINVAL;
		}
		if (vc->vc_mode == (unsigned char) arg)
			return 0;
		vc->vc_mode = (unsigned char) arg;
		if (vc->vc_num != vc->display_fg->fg_console->vc_num)
			return 0;
		/*
		 * explicitly blank/unblank the screen if switching modes
		 */
		if (arg == KD_TEXT) {
			unblank_screen(vc->display_fg);
		} else
			do_blank_screen(vc);
		return 0;

	case KDGETMODE:
		ucval = vc->vc_mode;
		goto setint;

	case KDMAPDISP:
	case KDUNMAPDISP:
		/*
		 * these work like a combination of mmap and KDENABIO.
		 * this could be easily finished.
		 */
		return -EINVAL;

	case KDSKBMODE:
		if (!perm)
			return -EPERM;
		switch(arg) {
		  case K_RAW:
			kbd->kbdmode = VC_RAW;
			break;
		  case K_MEDIUMRAW:
			kbd->kbdmode = VC_MEDIUMRAW;
			break;
		  case K_XLATE:
			kbd->kbdmode = VC_XLATE;
			compute_shiftstate();
			break;
		  case K_UNICODE:
			kbd->kbdmode = VC_UNICODE;
			compute_shiftstate();
			break;
		  default:
			return -EINVAL;
		}
		if (tty->ldisc.flush_buffer)
			tty->ldisc.flush_buffer(tty);
		return 0;

	case KDGKBMODE:
		ucval = ((kbd->kbdmode == VC_RAW) ? K_RAW :
				 (kbd->kbdmode == VC_MEDIUMRAW) ? K_MEDIUMRAW :
				 (kbd->kbdmode == VC_UNICODE) ? K_UNICODE :
				 K_XLATE);
		goto setint;

	/* this could be folded into KDSKBMODE, but for compatibility
	   reasons it is not so easy to fold KDGKBMETA into KDGKBMODE */
	case KDSKBMETA:
		switch(arg) {
		  case K_METABIT:
			clr_vc_kbd_mode(kbd, VC_META);
			break;
		  case K_ESCPREFIX:
			set_vc_kbd_mode(kbd, VC_META);
			break;
		  default:
			return -EINVAL;
		}
		return 0;

	case KDGKBMETA:
		ucval = (vc_kbd_mode(kbd, VC_META) ? K_ESCPREFIX : K_METABIT);
	setint:
		return put_user(ucval, (int *)arg); 

	case KDGETKEYCODE:
	case KDSETKEYCODE:
		if(!capable(CAP_SYS_ADMIN))
			perm=0;
		return do_kbkeycode_ioctl(cmd, (struct kbkeycode *)arg, perm);

	case KDGKBENT:
	case KDSKBENT:
		return do_kdsk_ioctl(cmd, (struct kbentry *)arg, perm, kbd);

	case KDGKBSENT:
	case KDSKBSENT:
		return do_kdgkb_ioctl(cmd, (struct kbsentry *)arg, perm);

	case KDGKBDIACR:
	{
		struct kbdiacrs *a = (struct kbdiacrs *)arg;

		if (put_user(accent_table_size, &a->kb_cnt))
			return -EFAULT;
		if (copy_to_user(a->kbdiacr, accent_table, accent_table_size*sizeof(struct kbdiacr)))
			return -EFAULT;
		return 0;
	}

	case KDSKBDIACR:
	{
		struct kbdiacrs *a = (struct kbdiacrs *)arg;
		unsigned int ct;

		if (!perm)
			return -EPERM;
		if (get_user(ct,&a->kb_cnt))
			return -EFAULT;
		if (ct >= MAX_DIACR)
			return -EINVAL;
		accent_table_size = ct;
		if (copy_from_user(accent_table, a->kbdiacr, ct*sizeof(struct kbdiacr)))
			return -EFAULT;
		return 0;
	}

	/* the ioctls below read/set the flags usually shown in the leds */
	/* don't use them - they will go away without warning */
	case KDGKBLED:
		ucval = kbd->ledflagstate | (kbd->default_ledflagstate << 4);
		goto setchar;

	case KDSKBLED:
		if (!perm)
			return -EPERM;
		if (arg & ~0x77)
			return -EINVAL;
		kbd->ledflagstate = (arg & 7);
		kbd->default_ledflagstate = ((arg >> 4) & 7);
		set_leds();
		return 0;

	/* the ioctls below only set the lights, not the functions */
	/* for those, see KDGKBLED and KDSKBLED above */
	case KDGETLED:
		ucval = getledstate();
	setchar:
		return put_user(ucval, (char*)arg);

	case KDSETLED:
		if (!perm)
		  return -EPERM;
		setledstate(kbd, arg);
		return 0;

	/*
	 * A process can indicate its willingness to accept signals
	 * generated by pressing an appropriate key combination.
	 * Thus, one can have a daemon that e.g. spawns a new console
	 * upon a keypress and then changes to it.
	 * Probably init should be changed to do this (and have a
	 * field ks (`keyboard signal') in inittab describing the
	 * desired action), so that the number of background daemons
	 * does not increase.
	 */
	case KDSIGACCEPT:
	{
		extern int spawnpid, spawnsig;
		if (!perm || !capable(CAP_KILL))
		  return -EPERM;
		if (arg < 1 || arg > _NSIG || arg == SIGKILL)
		  return -EINVAL;
		spawnpid = current->pid;
		spawnsig = arg;
		return 0;
	}

	case VT_SETMODE:
	{
		struct vt_mode tmp;

		if (!perm)
			return -EPERM;
		if (copy_from_user(&tmp, (void*)arg, sizeof(struct vt_mode)))
			return -EFAULT;
		if (tmp.mode != VT_AUTO && tmp.mode != VT_PROCESS)
			return -EINVAL;
		vc->vt_mode = tmp;
		/* the frsig is ignored, so we set it to 0 */
		vc->vt_mode.frsig = 0;
		vc->vt_pid = current->pid;
		/* no switch is required -- saw@shade.msu.ru */
		vc->vt_newvt = -1; 
		return 0;
	}

	case VT_GETMODE:
		return copy_to_user((void*)arg, &(vc->vt_mode), sizeof(struct vt_mode)) ? -EFAULT : 0;

	/*
         * Returns global vt state. Note that /dev/tty is always open, since
         * it's an alias for the current VC, and people can't use it here.
         * We cannot return state for more than 16 VCs, since v_state is short.
	 */
	case VT_GETSTATE:
	{
		struct vt_stat *vtstat = (struct vt_stat *)arg;
		unsigned short state = 0, mask;
		struct vc_data *tmp;

		i = verify_area(VERIFY_WRITE,(void *)vtstat, sizeof(struct vt_stat));
		if (i)
			return i;
		put_user(vc->display_fg->fg_console->vc_num, &vtstat->v_active);
		for (i = 0,mask = 0; i < MAX_NR_USER_CONSOLES; ++i,mask <<= 1) {
			tmp = find_vc(i + vc->display_fg->first_vc);
			if (tmp && VT_IS_IN_USE(tmp))
				state |= mask;
		}
		return put_user(state, &vtstat->v_state);
	}

	/*
	 * Returns the first available (non-opened) console.
	 */
	case VT_OPENQRY:
	{
		int j = vc->display_fg->first_vc;	
		struct vc_data *tmp;

		for (i = 0; i < MAX_NR_USER_CONSOLES; ++i,j++) {
			tmp = find_vc(j);	
			if (!tmp)
				break;
		} 
		ucval = i < MAX_NR_USER_CONSOLES ? (j) : -1;
		goto setint;		 
	}

	/*
	 * ioctl(fd, VT_ACTIVATE, num) will cause us to switch to vt # num,
	 * with num >= 0. Switches to the foreground console, are not allowed,
         * nor is switching to another physical VT just to preserve sanity.
         * The first VC of a VC pool is always allocated.
	 */
	case VT_ACTIVATE:
	{
		struct vc_data *tmp;		

		if (!perm)
			return -EPERM;
		if (arg > MAX_NR_CONSOLES) 
			return -ENXIO;

		i = vc_allocate(arg);
		if (i)
			return i;
		tmp = find_vc(arg);
		if (tmp->display_fg != vc->display_fg)
			return -ENXIO;
		set_console(tmp);
		return 0;
	}
	/*
	 * wait until the specified VT has been activated
	 */
	case VT_WAITACTIVE:
	{
		struct vc_data *tmp = find_vc(arg);	

		if (!perm)
			return -EPERM;
		if (arg > MAX_NR_CONSOLES || !tmp)
			return -ENXIO;
		
		return vt_waitactive(tmp);
	}
	/*
	 * If a vt is under process control, the kernel will not switch to it
	 * immediately, but postpone the operation until the process calls this
	 * ioctl, allowing the switch to complete.
	 *
	 * According to the X sources this is the behavior:
	 *	0:	pending switch-from not OK
	 *	1:	pending switch-from OK
	 *	2:	completed switch-to OK
	 */
	case VT_RELDISP:
		if (!perm)
			return -EPERM;
		if (vc->vt_mode.mode != VT_PROCESS)
			return -EINVAL;

		/*
		 * Switching-from response
		 */
		if (vc->vt_newvt >= 0) {
			if (arg == vc->display_fg->fg_console->vc_num)
				/*
				 * Switch disallowed, so forget we were trying
				 * to do it.
				 */
				vc->vt_newvt = -1;

			else {
				/*
				 * The current vt has been released, so
				 * complete the switch.
				 */
				struct vc_data *tmp;

				i = vc_allocate(vc->vt_newvt);
				if (i) {
					vc->vt_newvt = -1;
					return i;
				}
				tmp = find_vc(vc->vt_newvt);
				if (!tmp)
                                	return -ENXIO;

				/*
				 * When we actually do the console switch,
				 * make sure we are atomic with respect to
				 * other console switches..
				 */
				spin_lock_irq(&vc->display_fg->vt_lock);
				complete_change_console(tmp, vc->display_fg->fg_console);
				spin_unlock_irq(&vc->display_fg->vc_lock);
			}
		} else {
			/*
		 	 * Switched-to response. If it's just an ACK, ignore it
		 	 */
			if (arg != VT_ACKACQ)
				return -EINVAL;
		}
		return 0;


	 /*
	  * Disallocate memory associated to VCs (but leave all VTs)
	  */
	 case VT_DISALLOCATE:
	 {	
		struct vt_struct *vt = vc->display_fg;
		struct vc_data *tmp;	

		if (arg > MAX_NR_CONSOLES)
			return -ENXIO;
		if (arg == vt->fg_console->vc_num) {
		    	/* disallocate all unused consoles for a VT, 
		       	   but leave the foreground VC */
		    	for (i=0; i < MAX_NR_USER_CONSOLES; i++) {
		      		tmp = find_vc(i + vt->first_vc);
		      		if (tmp && (vt->fg_console->vc_num != tmp->vc_num) && !VT_BUSY(tmp)) 
					vc_disallocate(tmp->vc_num);
		    	}	
		} else {
		    /* disallocate a single console, if possible */
		    tmp = find_vc(arg);
		    if (!tmp || VT_BUSY(tmp))
		      return -EBUSY;
		    vc_disallocate(arg);
		}
		return 0;
	}
	case VT_RESIZE:
	{
		struct vt_sizes *vtsizes = (struct vt_sizes *) arg;
		ushort ll,cc;
		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)vtsizes, sizeof(struct vt_sizes));
		if (i)
			return i;
		get_user(ll, &vtsizes->v_rows);
		get_user(cc, &vtsizes->v_cols);

		for (i = 0; i < MAX_NR_USER_CONSOLES; i++) {
			struct vc_data *tmp = vc->display_fg->vc_cons[i];
			vc_resize(tmp, ll, cc);
		}
		return 0;
	}

	case VT_RESIZEX:
	{
		struct vt_consize *vtconsize = (struct vt_consize *) arg;
		ushort ll,cc,vlin,clin,vcol,ccol;
		if (!perm)
			return -EPERM;
		i = verify_area(VERIFY_READ, (void *)vtconsize, sizeof(struct vt_consize));
		if (i)
			return i;
		get_user(ll, &vtconsize->v_rows);
		get_user(cc, &vtconsize->v_cols);
		get_user(vlin, &vtconsize->v_vlin);
		get_user(clin, &vtconsize->v_clin);
		get_user(vcol, &vtconsize->v_vcol);
		get_user(ccol, &vtconsize->v_ccol);
		vlin = vlin ? vlin : vc->vc_scan_lines;
		if (clin) {
		    if (ll) {
			if ( ll != vlin/clin )
			  	return -EINVAL; /* Parameters don't add up */
		    } else
		      	ll = vlin/clin;
		}
		if (vcol && ccol) {
		    if (cc) {
			if ( cc != vcol/ccol )
			  return -EINVAL;
		    } else 
		        cc = vcol/ccol;
		}

		if ( clin > 32 )
		  return -EINVAL;
		    
		if ( vlin )
		  vc->vc_scan_lines = vlin;
		if ( clin )
		  vc->vc_font.height = clin;
	
		for (i = 0; i < MAX_NR_USER_CONSOLES; i++) {
                        struct vc_data *tmp = vc->display_fg->vc_cons[i];
                        vc_resize(tmp, ll, cc);
                }
		return 0;
  	}

	case PIO_FONT: {
		struct console_font_op op;
		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET;
		/* Compatibility */
		op.flags = KD_FONT_FLAG_OLD | KD_FONT_FLAG_DONT_RECALC;
		op.width = 8;
		op.height = 0;
		op.charcount = 256;
		op.data = (char *) arg;
		return con_font_op(vc, &op);
	}

	case GIO_FONT: {
		struct console_font_op op;
		op.op = KD_FONT_OP_GET;
		op.flags = KD_FONT_FLAG_OLD;
		op.width = 8;
		op.height = 32;
		op.charcount = 256;
		op.data = (char *) arg;
		return con_font_op(vc, &op);
	}

	case PIO_CMAP:
                if (!perm)
			return -EPERM;
                return con_set_cmap(vc, (char *)arg);

	case GIO_CMAP:
                return con_get_cmap(vc, (char *)arg);

	case PIO_FONTX:
	case GIO_FONTX:
		return do_fontx_ioctl(vc, cmd, (struct consolefontdesc *)arg, 
				      perm);

	case PIO_FONTRESET:
	{
		struct console_font_op op;

		if (!perm)
			return -EPERM;
		op.op = KD_FONT_OP_SET_DEFAULT;
		op.data = NULL;
		i = con_font_op(vc, &op);
		if (i) return i;
		con_set_default_unimap(vc);
		return 0;
	}

	case KDFONTOP: {
		struct console_font_op op;
	
		if (copy_from_user(&op, (void *) arg, sizeof(op)))
			return -EFAULT;
		if (!perm)
			return -EPERM;
		i = con_font_op(vc, &op);
		if (i) return i;
		if (copy_to_user((void *) arg, &op, sizeof(op)))
			return -EFAULT;
		return 0;
	}

	case PIO_SCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans_old(vc, (unsigned char *)arg);

	case GIO_SCRNMAP:
		return con_get_trans_old(vc, (unsigned char *)arg);

	case PIO_UNISCRNMAP:
		if (!perm)
			return -EPERM;
		return con_set_trans_new(vc, (unsigned short *)arg);

	case GIO_UNISCRNMAP:
		return con_get_trans_new(vc, (unsigned short *)arg);

	case PIO_UNIMAPCLR:
	      { struct unimapinit ui;
		if (!perm)
			return -EPERM;
		i = copy_from_user(&ui, (void *)arg, sizeof(struct unimapinit));
		if (i) return -EFAULT;
		con_clear_unimap(vc->display_fg->fg_console, &ui);
		return 0;
	      }

	case PIO_UNIMAP:
	case GIO_UNIMAP:
		return do_unimap_ioctl(vc, cmd, (struct unimapdesc *)arg, perm);

	case VT_LOCKSWITCH:
		if (!suser())
		   return -EPERM;
		vc->display_fg->vt_dont_switch = 1;
		return 0;
	case VT_UNLOCKSWITCH:
		if (!suser())
		   return -EPERM;
		vc->display_fg->vt_dont_switch = 0;
		return 0;
#ifdef CONFIG_FB_COMPAT_XPMAC
	case VC_GETMODE:
		{
			struct vc_mode mode;

			i = verify_area(VERIFY_WRITE, (void *) arg,
					sizeof(struct vc_mode));
			if (i == 0)
				i = console_getmode(&mode);
			if (i)
				return i;
			if (copy_to_user((void *) arg, &mode, sizeof(mode)))
				return -EFAULT;
			return 0;
		}
	case VC_SETMODE:
	case VC_INQMODE:
		{
			struct vc_mode mode;

			if (!perm)
				return -EPERM;
			i = verify_area(VERIFY_READ, (void *) arg,
					sizeof(struct vc_mode));
			if (i)
				return i;
			if (copy_from_user(&mode, (void *) arg, sizeof(mode)))
				return -EFAULT;
			return console_setmode(&mode, cmd == VC_SETMODE);
		}
	case VC_SETCMAP:
		{
			unsigned char cmap[3][256], *p;
			int n_entries, cmap_size, i, j;

			if (!perm)
				return -EPERM;
			if (arg == (unsigned long) VC_POWERMODE_INQUIRY
			    || arg <= VESA_POWERDOWN) {
				/* compatibility hack: VC_POWERMODE
				   was changed from 0x766a to 0x766c */
				return console_powermode((int) arg);
			}
			i = verify_area(VERIFY_READ, (void *) arg,
					sizeof(int));
			if (i)
				return i;
			if (get_user(cmap_size, (int *) arg))
				return -EFAULT;
			if (cmap_size % 3)
				return -EINVAL;
			n_entries = cmap_size / 3;
			if ((unsigned) n_entries > 256)
				return -EINVAL;
			p = (unsigned char *) (arg + sizeof(int));
			for (j = 0; j < n_entries; ++j)
				for (i = 0; i < 3; ++i)
					if (get_user(cmap[i][j], p++))
						return -EFAULT;
			return console_setcmap(n_entries, cmap[0],
					       cmap[1], cmap[2]);
		}
	case VC_GETCMAP:
		/* not implemented yet */
		return -ENOIOCTLCMD;
	case VC_POWERMODE:
		if (!perm)
			return -EPERM;
		return console_powermode((int) arg);
#endif /* CONFIG_FB_COMPAT_XPMAC */
	default:
		return -ENOIOCTLCMD;
	}
}

void reset_vc(struct vc_data *vc)
{
	vc->vc_mode = KD_TEXT;
	vc->kbd_table.kbdmode = VC_XLATE;
	vc->vt_mode.mode = VT_AUTO;
	vc->vt_mode.waitv = 0;
	vc->vt_mode.relsig = 0;
	vc->vt_mode.acqsig = 0;
	vc->vt_mode.frsig = 0;
	vc->vt_pid = -1;
	vc->vt_newvt = -1;
	reset_palette(vc);
}

inline void switch_screen(struct vc_data *new_vc, struct vc_data *old_vc)
{
	struct vt_struct *vt = new_vc->display_fg;

        if (!new_vc) {
                /* strange ... */
                printk("redraw_screen: tty %d not allocated ??\n", new_vc->vc_num);
                return;
        }

        hide_cursor(old_vc);
        if (old_vc->vc_num != new_vc->vc_num) {
		vt->fg_console = new_vc;
                set_origin(old_vc);               
		
		set_origin(new_vc);	

/*	
		if (new_vc->vc_font.height != old_vc->vc_font.height ||
		    new_vc->vc_font.width != old_vc->vc_font.width || 
		    new_vc->vc_font.charcount != old_vc->vc_font.charcount ||
		    !strcmp(&new_vc->vc_font.data, &old_vc->vc_font.data)) {
			vt->vt_sw->con_font_op(new_vc, &new_vc->vc_font);
		}	
*/	
		set_palette(new_vc);
                if (new_vc->vc_mode != KD_GRAPHICS) { 
                        /* Update the screen contents */
                        do_update_region(new_vc, new_vc->vc_origin, 
					 new_vc->vc_screensize);
		}
        }                               
        set_cursor(new_vc);
        set_leds();
        compute_shiftstate();
}           
  
/*
 * Performs the front-end of a vt switch
 */
void change_console(struct vc_data *new_vc, struct vc_data *old_vc)
{
	/*
	 * If this vt is in process mode, then we need to handshake with
	 * that process before switching. Essentially, we store where that
	 * vt wants to switch to and wait for it to tell us when it's done
	 * (via VT_RELDISP ioctl).
	 *
	 * We also check to see if the controlling process still exists.
	 * If it doesn't, we reset this vt to auto mode and continue.
	 * This is a cheap way to track process control. The worst thing
	 * that can happen is: we send a signal to a process, it dies, and
	 * the switch gets "lost" waiting for a response; hopefully, the
	 * user will try again, we'll detect the process is gone (unless
	 * the user waits just the right amount of time :-) and revert the
	 * vt to auto control.
	 */
	if (old_vc->vt_mode.mode == VT_PROCESS) {
		/*
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_proc(old_vc->vt_pid, old_vc->vt_mode.relsig,1) == 0) { 
			/*
			 * It worked. Mark the vt to switch to and
			 * return. The process needs to send us a
			 * VT_RELDISP ioctl to complete the switch.
			 */
			old_vc->vt_newvt = new_vc->vc_num;
			return;
		}

		/*
		 * The controlling process has died, so we revert back to
		 * normal operation. In this case, we'll also change back
		 * to KD_TEXT mode. I'm not sure if this is strictly correct
		 * but it saves the agony when the X server dies and the screen
		 * remains blanked due to KD_GRAPHICS! It would be nice to do
		 * this outside of VT_PROCESS but there is no single process
		 * to account for and tracking tty count may be undesirable.
		 */
		reset_vc(old_vc);

		/*
		 * Fall through to normal (VT_AUTO) handling of the switch...
		 */
	}

	/*
	 * Ignore all switches in KD_GRAPHICS+VT_AUTO mode
	 */
	if (old_vc->vc_mode == KD_GRAPHICS)
		return;

	complete_change_console(new_vc, old_vc);
}

/*
 * Performs the back end of a vt switch
 */
void complete_change_console(struct vc_data *new_vc, struct vc_data *old_vc)
{
	unsigned char old_vc_mode;

	old_vc->display_fg->last_console = old_vc;

	/*
	 * If we're switching, we could be going from KD_GRAPHICS to
	 * KD_TEXT mode or vice versa, which means we need to blank or
	 * unblank the screen later.
	 */
	old_vc_mode = old_vc->vc_mode;
	switch_screen(new_vc, old_vc);

	/*
	 * If this new console is under process control, send it a signal
	 * telling it that it has acquired. Also check if it has died and
	 * clean up (similar to logic employed in change_console())
	 */
	if (new_vc->vt_mode.mode == VT_PROCESS) {
		/*
		 * Send the signal as privileged - kill_proc() will
		 * tell us if the process has gone or something else
		 * is awry
		 */
		if (kill_proc(new_vc->vt_pid,new_vc->vt_mode.acqsig, 1) != 0) { 
		/*
		 * The controlling process has died, so we revert back to
		 * normal operation. In this case, we'll also change back
		 * to KD_TEXT mode. I'm not sure if this is strictly correct
		 * but it saves the agony when the X server dies and the screen
		 * remains blanked due to KD_GRAPHICS! It would be nice to do
		 * this outside of VT_PROCESS but there is no single process
		 * to account for and tracking tty count may be undesirable.
		 */
		        reset_vc(new_vc);
		}
	}

	/*
	 * We do this here because the controlling process above may have
	 * gone, and so there is now a new vc_mode
         */	
	if (old_vc_mode != new_vc->vc_mode) {
		if (new_vc->vc_mode == KD_TEXT) {
			unblank_screen(new_vc->display_fg);
		} else
			do_blank_screen(new_vc);
	}

	/*
	 * Wake anyone waiting for their VT to activate
	 */
	wake_up(&vt_activate_queue);
	return;
}
