/*
 * $$
 *
 * Force feedback support for hid-compliant for some of the devices from
 * Logitech, namely:
 * - WingMan Cordless RumblePad
 *
 *  Copyright (c) 2002 Johann Deneux
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
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <deneux@ifrance.com>
 */

#include <linux/input.h>
#include <linux/sched.h>

#define DEBUG
#include <linux/usb.h>

#include <linux/circ_buf.h>

#include "hid.h"

#define RUN_AT(t) (jiffies + (t))

/* Transmition state */
#define XMIT_RUNNING 0

/* Effect status */
#define EFFECT_STARTED 0     /* Effect is going to play after some time
				(ff_replay.delay) */
#define EFFECT_PLAYING 1     /* Effect is being played */
#define EFFECT_USED    2

/* Check that the current process can access an effect */
#define CHECK_OWNERSHIP(effect) (current->pid == 0 \
        || effect.owner == current->pid)

/* **************************************************************************/
/* Implements the protocol used by the Logitech WingMan Cordless RumblePad */
/* **************************************************************************/

#define LGFF_CHECK_OWNERSHIP(i, l) \
        (i>=0 && i<LGFF_EFFECTS \
        && test_bit(EFFECT_USED, l->effects[i].flags) \
        && CHECK_OWNERSHIP(l->effects[i]))

#define LGFF_BUFFER_SIZE 64
#define LGFF_EFFECTS 8

struct lgff_magnitudes {
	unsigned char left;
	unsigned char right;
};

struct lgff_effect {
	int id;
	struct hid_ff_logitech* lgff;

	pid_t owner;
	unsigned char left;        /* Magnitude of vibration for left motor */
	unsigned char right;       /* Magnitude of vibration for right motor */
	struct ff_replay replay;
	unsigned int count;        /* Number of times to play */
	struct timer_list timer;
	unsigned long flags[1];
};

struct hid_ff_logitech {
	struct hid_device* hid;

	struct hid_report report;
	__s32 value[8];

	struct lgff_effect effects[LGFF_EFFECTS];
	spinlock_t lock;             /* device-level lock. Having locks on
					a per-effect basis could be nice, but
					isn't really necessary */
};

static void hid_lgff_exit(struct hid_device* hid);
static int hid_lgff_event(struct hid_device *hid, struct input_dev *input,
			  unsigned int type, unsigned int code, int value);
static void hid_lgff_make_rumble(struct hid_device* hid);

static int hid_lgff_flush(struct input_dev *input, struct file *file);
static int hid_lgff_upload_effect(struct input_dev *input,
				  struct ff_effect *effect);
static int hid_lgff_erase(struct input_dev *input, int id);
static void hid_lgff_ctrl_playback(struct hid_device* hid, struct lgff_effect*,
				   int play);
static void hid_lgff_timer(unsigned long timer_data);


int hid_lgff_init(struct hid_device* hid)
{
	struct hid_ff_logitech *private;
	struct hid_report* report;
	struct hid_field* field;
	int i;

	/* Find the report to use */
	if (list_empty(&hid->report_enum[HID_OUTPUT_REPORT].report_list)) {
		err("No output report found");
		return -1;
	}
	/* Check the report looks ok */
	report = (struct hid_report*)hid->report_enum[HID_OUTPUT_REPORT].report_list.next;
	if (!report) {
		err("NULL output report");
		return -1;
	}
	field = report->field[0];
	if (!field) {
		err("NULL field");
		return -1;
	}
	if (!field->value) {
		err("No space allocated for values");
		return -1;
	}

	private = kmalloc(sizeof(struct hid_ff_logitech), GFP_KERNEL);
	if (!private) return -1;
	memset(private, 0, sizeof(struct hid_ff_logitech));
	hid->ff_private = private;

	private->report = *(struct hid_report*)(hid->report_enum[HID_OUTPUT_REPORT].report_list.next);
	private->hid = hid;

	spin_lock_init(&private->lock);

	for (i=0; i<LGFF_EFFECTS; ++i) {
		struct lgff_effect* effect = &private->effects[i];
		struct timer_list* timer = &effect->timer;

		init_timer(timer);
		effect->id = i;
		effect->lgff = private;
		timer->data = (unsigned long)effect;
		timer->function = hid_lgff_timer;
	}

	/* Event and exit callbacks */
	hid->ff_exit = hid_lgff_exit;
	hid->ff_event = hid_lgff_event;

	/* Input init */
	hid->input.upload_effect = hid_lgff_upload_effect;
	hid->input.flush = hid_lgff_flush;
	set_bit(FF_RUMBLE, hid->input.ffbit);
	set_bit(EV_FF, hid->input.evbit);
	hid->input.ff_effects_max = LGFF_EFFECTS;

	printk(KERN_INFO "Force feedback for Logitech rumble devices by Johann Deneux <deneux@ifrance.com>\n");

	return 0;
}

static void hid_lgff_exit(struct hid_device* hid)
{
	struct hid_ff_logitech *lgff = hid->ff_private;

	/* At this point, all effects were erased by hid_lgff_flush.
	   No need to do anything */
}

static int hid_lgff_event(struct hid_device *hid, struct input_dev* input,
			  unsigned int type, unsigned int code, int value)
{
	struct hid_ff_logitech *lgff = hid->ff_private;
	struct lgff_effect *effect = lgff->effects + code;
	unsigned long flags;

	if (type != EV_FF)                     return -EINVAL;
       	if (!LGFF_CHECK_OWNERSHIP(code, lgff)) return -EACCES;
	if (value < 0)                         return -EINVAL;

	spin_lock_irqsave(&lgff->lock, flags);
	
	if (value > 0) {
		if (test_bit(EFFECT_STARTED, effect->flags)) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -EBUSY;
		}
		if (test_bit(EFFECT_PLAYING, effect->flags)) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -EBUSY;
		}

		effect->count = value;

		if (effect->replay.delay) {
			set_bit(EFFECT_STARTED, effect->flags);
			effect->timer.expires = RUN_AT(effect->replay.delay * HZ / 1000);
		} else {
			hid_lgff_ctrl_playback(hid, effect, value);
			effect->timer.expires = RUN_AT(effect->replay.length * HZ / 1000);
		}

		add_timer(&effect->timer);
	}
	else { /* value == 0 */
		if (test_and_clear_bit(EFFECT_STARTED, effect->flags)) {
			del_timer(&effect->timer);

		} else if (test_and_clear_bit(EFFECT_PLAYING, effect->flags)) {
			del_timer(&effect->timer);
			hid_lgff_ctrl_playback(hid, effect, value);
		}

		if (test_bit(EFFECT_PLAYING, effect->flags))
			warn("Effect %d still playing", code);
	}

	spin_unlock_irqrestore(&lgff->lock, flags);

	return 0;

}

/* Erase all effects this process owns */
static int hid_lgff_flush(struct input_dev *dev, struct file *file)
{
	struct hid_device *hid = dev->private;
	struct hid_ff_logitech *lgff = hid->ff_private;
	int i;

	for (i=0; i<dev->ff_effects_max; ++i) {

		/*NOTE: no need to lock here. The only times EFFECT_USED is
		  modified is when effects are uploaded or when an effect is
		  erased. But a process cannot close its dev/input/eventX fd
		  and perform ioctls on the same fd all at the same time */
		if ( current->pid == lgff->effects[i].owner
		     && test_bit(EFFECT_USED, lgff->effects[i].flags)) {
			
			if (hid_lgff_erase(dev, i))
				warn("erase effect %d failed", i);
		}

	}

	return 0;
}

static int hid_lgff_erase(struct input_dev *dev, int id)
{
	struct hid_device *hid = dev->private;
	struct hid_ff_logitech *lgff = hid->ff_private;
	unsigned long flags;

	if (!LGFF_CHECK_OWNERSHIP(id, lgff)) return -EACCES;

	spin_lock_irqsave(&lgff->lock, flags);
	hid_lgff_ctrl_playback(hid, lgff->effects + id, 0);
	lgff->effects[id].flags[0] = 0;
	spin_unlock_irqrestore(&lgff->lock, flags);

	return 0;
}

static int hid_lgff_upload_effect(struct input_dev* input,
				  struct ff_effect* effect)
{
	struct hid_device *hid = input->private;
	struct hid_ff_logitech *lgff = hid->ff_private;
	struct lgff_effect new;
	int id;
	unsigned long flags;
	
	dbg("ioctl rumble");

	if (!test_bit(effect->type, input->ffbit)) return -EINVAL;

	if (effect->type != FF_RUMBLE) return -EINVAL;

	spin_lock_irqsave(&lgff->lock, flags);

	if (effect->id == -1) {
		int i;

		for (i=0; i<LGFF_EFFECTS && test_bit(EFFECT_USED, lgff->effects[i].flags); ++i);
		if (i >= LGFF_EFFECTS) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -ENOSPC;
		}

		effect->id = i;
		lgff->effects[i].owner = current->pid;
		lgff->effects[i].flags[0] = 0;
		set_bit(EFFECT_USED, lgff->effects[i].flags);
	}
	else if (!LGFF_CHECK_OWNERSHIP(effect->id, lgff)) {
		spin_unlock_irqrestore(&lgff->lock, flags);
		return -EACCES;
	}

	id = effect->id;
	new = lgff->effects[id];

	new.right = effect->u.rumble.strong_magnitude >> 9;
	new.left = effect->u.rumble.weak_magnitude >> 9;
	new.replay = effect->replay;

	/* If we updated an effect that was being played, we need to remake
	   the rumble effect */
	if (test_bit(EFFECT_STARTED, lgff->effects[id].flags)
	    || test_bit(EFFECT_STARTED, lgff->effects[id].flags)) {

		/* Changing replay parameters is not allowed (for the time
		   being) */
		if (new.replay.delay != lgff->effects[id].replay.delay
		    || new.replay.length != lgff->effects[id].replay.length) {
			spin_unlock_irqrestore(&lgff->lock, flags);
			return -ENOSYS;
		}

		lgff->effects[id] = new;
		hid_lgff_make_rumble(hid);

	} else {
		lgff->effects[id] = new;
	}

	spin_unlock_irqrestore(&lgff->lock, flags);
	return 0;
}

static void hid_lgff_make_rumble(struct hid_device* hid)
{
	struct hid_ff_logitech *lgff = hid->ff_private;
	int left = 0, right = 0;
	int i;
	unsigned long flags;

	for (i=0; i<LGFF_EFFECTS; ++i) {
		if (test_bit(EFFECT_USED, lgff->effects[i].flags)
		    && test_bit(EFFECT_PLAYING, lgff->effects[i].flags)) {
			left += lgff->effects[i].left;
			right += lgff->effects[i].right;
		}
	}

	lgff->report.field[0]->value[0] = 0x03;
	lgff->report.field[0]->value[1] = 0x42;
	lgff->report.field[0]->value[3] = left;
	lgff->report.field[0]->value[4] = right;
	hid_submit_report(hid, &lgff->report, USB_DIR_OUT);
}

/* Lock must be held by caller */
static void hid_lgff_ctrl_playback(struct hid_device *hid,
				   struct lgff_effect *effect, int play)
{
	if (play) {
		set_bit(EFFECT_PLAYING, effect->flags);
		hid_lgff_make_rumble(hid);

	} else {
		clear_bit(EFFECT_PLAYING, effect->flags);
		hid_lgff_make_rumble(hid);
	}
}

static void hid_lgff_timer(unsigned long timer_data)
{
	struct lgff_effect *effect = (struct lgff_effect*) timer_data;
	struct hid_ff_logitech* lgff = effect->lgff;
	int id = effect->id;

	unsigned long flags;

	dbg("in hid_lgff_timer");

	if (id < 0 || id >= LGFF_EFFECTS) {
		warn("Bad effect id %d", id);
		return;
	}

	effect = lgff->effects + id;

	spin_lock_irqsave(&lgff->lock, flags);

	if (!test_bit(EFFECT_USED, effect->flags)) {
		warn("Unused effect id %d", id);

	} else if (test_bit(EFFECT_STARTED, effect->flags)) {
		clear_bit(EFFECT_STARTED, effect->flags);
		set_bit(EFFECT_PLAYING, effect->flags);
		hid_lgff_ctrl_playback(lgff->hid, effect, 1);
		if (effect->replay.length) {
			effect->timer.expires = RUN_AT(effect->replay.length * HZ / 1000);
			add_timer(&effect->timer);
		}
		/* else { play for ever } */

		dbg("Effect %d starts playing", id);
	} else if (test_bit(EFFECT_PLAYING, effect->flags)) {
		clear_bit(EFFECT_PLAYING, effect->flags);
		hid_lgff_ctrl_playback(lgff->hid, effect, 0);
		if (--effect->count > 0) {
			/*TODO: check that replay.delay is non-null */
			set_bit(EFFECT_STARTED, effect->flags);
			effect->timer.expires = RUN_AT(effect->replay.delay * HZ / 1000);
			add_timer(&effect->timer);
			dbg("Effect %d restarted", id);
		} else {
			dbg("Effect %d stopped", id);
		}
	} else {
		warn("Effect %d is not started nor playing", id);
	}

	spin_unlock_irqrestore(&lgff->lock, flags);
}
