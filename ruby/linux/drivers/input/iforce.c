/*
 * $Id$
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2001 Johann Deneux <deneux@ifrance.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 *
 *  Sponsored by SuSE
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/usb.h>
#include <linux/serio.h>
#include <linux/config.h>

/* FF: This module provides arbitrary resource management routines.
 * I use it to manage the device's memory.
 * Despite the name of this module, I am *not* going to access the ioports.
 */
#include <linux/ioport.h>


MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>, Johann Deneux <deneux@ifrance.com>");
MODULE_DESCRIPTION("USB/RS232 I-Force joysticks and wheels driver");

#define USB_VENDOR_ID_LOGITECH		0x046d
#define USB_DEVICE_ID_LOGITECH_WMFORCE	0xc281
#define USB_DEVICE_ID_LOGITECH_FORMULA	0xc291

#define IFORCE_MAX_LENGTH               16
 
#if defined(CONFIG_INPUT_IFORCE_232) || defined(CONFIG_INPUT_IFORCE_232_MODULE)
#define IFORCE_232
#endif
#if defined(CONFIG_INPUT_IFORCE_USB) || defined(CONFIG_INPUT_IFORCE_USB_MODULE)
#define IFORCE_USB
#endif

#define FF_EFFECTS_MAX	32

/* Each force feedback effect is made of one core effect, which can be
 * associated to at most to effect modifiers
 */
#define FF_MOD1_IS_USED		0
#define FF_MOD2_IS_USED		1
#define FF_CORE_IS_USED		2
#define FF_CORE_IS_PLAYED	3
#define FF_MODCORE_MAX		3

struct iforce_core_effect {
	/* Information about where modifiers are stored in the device's memory */
	struct resource mod1_chunk;
	struct resource mod2_chunk;
	unsigned long flags[NBITS(FF_MODCORE_MAX)];
};

#define FF_CMD_EFFECT		0x010e
#define FF_CMD_SHAPE		0x0208
#define FF_CMD_MAGNITUDE	0x0303
#define FF_CMD_PERIOD		0x0407
#define FF_CMD_INTERACT		0x050a

#define FF_CMD_INIT_0_A		0x4002
#define FF_CMD_INIT_0_B		0x4003
#define FF_CMD_PLAY		0x4103
#define FF_CMD_INIT_2		0x4201
#define FF_CMD_INIT_3		0x4301

#define FF_CMD_INIT_F		0xff01

/* For iforce->init_done: Tells what parts of the init process are completed */
#define FF_INIT_RAMSIZE		0x01
#define FF_INIT_N_EFFECTS	0x02
#define FF_INIT_DEV_TYPE	0x04
#define FF_INIT_VID		0x08
#define FF_INIT_PID		0x10
#define FF_INIT_ALL_MASK	0x1f	

struct iforce {
	struct input_dev dev;		/* Input device interface */
        int open;
	int type;

        signed char data[IFORCE_MAX_LENGTH];

#ifdef IFORCE_232
        struct serio *serio;		/* RS232 transfer */
        int idx, pkt, len, id;
        unsigned char csum;
#endif
#ifdef IFORCE_USB
        struct usb_device *usbdev;	/* USB transfer */
        struct urb irq, out;
#endif
					/* Force Feedback */
	wait_queue_head_t wait;
	unsigned long init_done;
	struct resource device_memory;  
	int n_effects_max;
	struct iforce_core_effect core_effects[FF_EFFECTS_MAX];
};

static struct {
        __s32 x;
        __s32 y;
} iforce_hat_to_axis[16] = {{ 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static struct ff_init_data {
	u16 cmd;
	u8 data[4];
} ff_init_data[] =  {

	{ FF_CMD_INIT_F,   { 0x4F } },
	{ FF_CMD_INIT_F,   { 0x56 } },
	{ FF_CMD_INIT_F,   { 0x4E } },
	{ FF_CMD_INIT_F,   { 0x42 } },
	{ FF_CMD_INIT_F,   { 0x4D } },
	{ FF_CMD_INIT_F,   { 0x50 } },

	{ FF_CMD_INIT_0_B, { 0x06, 0xF4, 0x01 } },
	{ FF_CMD_INIT_3,   { 0x80 } },
	{ FF_CMD_INIT_2,   { 0x04 } },
	{ FF_CMD_INIT_2,   { 0x04 } },
	{ FF_CMD_INIT_2,   { 0x05 } },
	{ FF_CMD_INIT_0_A, { 0x04, 0x00 } },
	{ FF_CMD_INIT_2,   { 0x05 } },
	{ FF_CMD_INIT_0_A, { 0x04, 0x00 } },
	{ FF_CMD_INIT_3,   { 0x80 } },
	{ FF_CMD_INIT_2,   { 0x05 } },
	{ FF_CMD_INIT_0_A, { 0x04, 0x00 } },
	{ FF_CMD_INIT_2,   { 0x01 } },
	{ FF_CMD_INIT_2,   { 0x00 } },

	{ FF_CMD_INIT_F,   { 0x42 } },
	{ FF_CMD_INIT_F,   { 0x43 } },
	{ FF_CMD_INIT_F,   { 0x45 } },
	{ FF_CMD_INIT_F,   { 0x4d } },
	{ FF_CMD_INIT_F,   { 0x4e } },
	{ FF_CMD_INIT_F,   { 0x4f } },
	{ FF_CMD_INIT_F,   { 0x50 } },
	{ FF_CMD_INIT_F,   { 0x56 } },

	{ FF_CMD_INIT_0_B, { 0x06, 0xF4, 0x01 } },
	{ FF_CMD_INIT_3,   { 0x80 } },
	{ FF_CMD_INIT_2,   { 0x04 } },
	{ FF_CMD_INIT_2,   { 0x04 } },
	{ FF_CMD_INIT_2,   { 0x05 } },
	{ FF_CMD_INIT_2,   { 0x04 } },
	{ FF_CMD_INIT_0_A, { 0x04, 0x00 } },
	{ FF_CMD_INIT_2,   { 0x05 } },
	{ FF_CMD_INIT_0_A, { 0x04, 0x00 } },
	{ FF_CMD_INIT_3,   { 0x80 } },
	{ 0, }
 };

static char *iforce_name_joystick = "I-Force joystick";
static char *iforce_name_wheel = "I-Force wheel";

/* Get hi and low bytes of a 16-bits int */
#define HI(a)	((unsigned char)((a) >> 8))
#define LO(a)	((unsigned char)((a) & 0xff))

/* Encode a time value */
#define TIME_SCALE(a)	((a) == 0xffff ? 0xffff : (a) * 1000 / 256)
 
static void dump_packet(char *msg, u16 cmd, unsigned char *data)
{
	int i;

	printk(KERN_DEBUG "iforce.c: %s ( cmd = %04x, data = ", msg, cmd);
	for (i = 0; i < LO(cmd); i++)
		printk("%02x ", data[i]);
	printk(")\n");
}

/*
 * Send a packet of bytes to the device
 */
static void send_packet(struct iforce *iforce, u16 cmd, unsigned char* data)
{
	switch (iforce->dev.idbus) {

#ifdef IFORCE_232
		case BUS_RS232: {

			int i;
			unsigned char csum = 0x2b ^ HI(cmd) ^ LO(cmd);
		 
			serio_write(iforce->serio, 0x2b);
			serio_write(iforce->serio, HI(cmd));
			serio_write(iforce->serio, LO(cmd));

			for (i = 0; i < LO(cmd); i++) {
				serio_write(iforce->serio, data[i]);
				csum = csum ^ data[i];
			}

			serio_write(iforce->serio, csum);

			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(HZ/100); /* 10 ms */

			return;
		}
#endif
#ifdef IFORCE_USB
		case BUS_USB: {

			DECLARE_WAITQUEUE(wait, current);
			int status, timeout = HZ; /* 1 second */

			memcpy(iforce->out.transfer_buffer + 1, data, LO(cmd));
			((char*)iforce->out.transfer_buffer)[0] = HI(cmd);
			iforce->out.transfer_buffer_length = LO(cmd) + 2;
			iforce->out.dev = iforce->usbdev;

			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&iforce->wait, &wait);

			if ((status = usb_submit_urb(&iforce->out))) {
				set_current_state(TASK_RUNNING);
				remove_wait_queue(&iforce->wait, &wait);
				printk(KERN_WARNING "iforce.c: Failed to submit output urb. (%d)\n", status);
				return;
			}

			while (timeout && iforce->out.status == -EINPROGRESS)
				timeout = schedule_timeout(timeout);

			set_current_state(TASK_RUNNING);
			remove_wait_queue(&iforce->wait, &wait);

			if (!timeout) {
				printk(KERN_WARNING "iforce.c: Output urb: timeout\n");
				usb_unlink_urb(&iforce->out);
			}

			return;
		}
#endif
	}
} 

/*
 * Start or stop playing an effect
 */

static int iforce_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct iforce* iforce = (struct iforce*)(dev->private);
	unsigned char data[3];

	if (type != EV_FF)
		return -1;

	printk(KERN_DEBUG "iforce.c: input_event(type = %d, code = %d, value = %d)\n", type, code, value);

        data[0] = LO(code);
        data[1] = (value > 0) ? ((value > 1) ? 0x41 : 0x01) : 0;
        data[2] = LO(value);
 
        send_packet(iforce, FF_CMD_PLAY, data);

	return 0;
}

/*
 * Set the magnitude of a constant force effect
 * Return error code
 *
 * Note: caller must ensure exclusive access to device
 */

static int make_magnitude_modifier(struct iforce* iforce,
	struct resource* mod_chunk, __s16 level)
{
        unsigned char data[3];
 
	if (allocate_resource(&(iforce->device_memory), mod_chunk, 2,
		iforce->device_memory.start, iforce->device_memory.end, 2L,
		NULL, NULL)) {
		return -ENOMEM;
	}

        data[0] = LO(mod_chunk->start);
        data[1] = HI(mod_chunk->start);
        data[2] = HI(level);
 
        send_packet(iforce, FF_CMD_MAGNITUDE, data);

	return 0;
}

/*
 * Upload the component of an effect dealing with the period, phase and magnitude
 */

static int make_period_modifier(struct iforce* iforce, struct resource* mod_chunk,
	__s16 magnitude, __s16 offset, u16 period, u16 phase)
{
	unsigned char data[7];
 
	period = TIME_SCALE(period);
 
	if (allocate_resource(&(iforce->device_memory), mod_chunk, 0x0c,
		iforce->device_memory.start, iforce->device_memory.end, 2L,
		NULL, NULL)) {
		return -ENOMEM;
	}

	data[0] = LO(mod_chunk->start);
	data[1] = HI(mod_chunk->start);
 
	data[2] = HI(magnitude);
	data[3] = HI(offset);
	data[4] = HI(phase);
 
	data[5] = LO(period);
	data[6] = HI(period);
 
	send_packet(iforce, FF_CMD_PERIOD, data); 

	return 0;
}

/*
 * Uploads the part of an effect setting the shape of the force
 */

static int make_shape_modifier(struct iforce* iforce, struct resource* mod_chunk,
        u16 attack_duration, __s16 initial_level,
        u16 fade_duration, __s16 final_level)
{
	unsigned char data[8];
 
	attack_duration = TIME_SCALE(attack_duration);
	fade_duration = TIME_SCALE(fade_duration);
 
	if (allocate_resource(&(iforce->device_memory), mod_chunk, 0x0e,
		iforce->device_memory.start, iforce->device_memory.end, 2L,
		NULL, NULL)) {
		return -ENOMEM;
	}

	data[0] = LO(mod_chunk->start);
	data[1] = HI(mod_chunk->start);
 
	data[2] = LO(attack_duration);
	data[3] = HI(attack_duration);
	data[4] = HI(initial_level);
 
	data[5] = LO(fade_duration);
	data[6] = HI(fade_duration);
	data[7] = HI(final_level);
 
	send_packet(iforce, FF_CMD_SHAPE, data);

	return 0;
}

/*
 * Component of spring, friction, inertia... effects
 */

static int make_interactive_modifier(struct iforce* iforce, 
	struct resource* mod_chunk,
	__s16 rsat, __s16 lsat, __s16 rk, __s16 lk, u16 db, __s16 center)
{
        unsigned char data[10];

	if (allocate_resource(&(iforce->device_memory), mod_chunk, 8,
		iforce->device_memory.start, iforce->device_memory.end, 2L,
		NULL, NULL)) {
		return -ENOMEM;
	}

        data[0] = LO(mod_chunk->start);
        data[1] = HI(mod_chunk->start);

        data[2] = HI(rk);
        data[3] = HI(lk);

        data[4] = LO(center);
	data[5] = HI(center);

        data[6] = LO(db);
        data[7] = HI(db);

        data[8] = HI(rsat);
        data[9] = HI(lsat);

        send_packet(iforce, FF_CMD_INTERACT, data);

	return 0;
}

/*
 * Send the part common to all effects to the device
 */

static int make_core(struct iforce* iforce, u16 id, u16 mod_id1, u16 mod_id2,
	u8 effect_type, u8 axes, u16 duration, u16 delay, u16 button,
	u16 interval, u16 direction)
{
	unsigned char data[14];
 
	duration = TIME_SCALE(duration);
	delay    = TIME_SCALE(delay);
	interval = TIME_SCALE(interval);
 
	data[0]  = LO(id);
	data[1]  = effect_type;
	data[2]  = LO((axes)
		 | ((button == FF_BUTTON_NONE ? 0 : (button + 1)) & 0x0f));

	data[3]  = LO(duration);
	data[4]  = HI(duration);
 
	data[5]  = HI(direction);
 
	data[6]  = LO(interval);
	data[7]  = HI(interval);
 
	data[8]  = LO(mod_id1);
	data[9]  = HI(mod_id1);
	data[10] = LO(mod_id2);
	data[11] = HI(mod_id2);
 
	data[12] = LO(delay);
	data[13] = HI(delay);

	send_packet(iforce, FF_CMD_EFFECT, data);                                               

	return 0;
}

/*
 * Upload a periodic effect to the device
 */

static int iforce_upload_periodic(struct iforce* iforce, struct ff_effect* effect)
{
	u8 wave_code;
	int core_id = effect->id;
	struct iforce_core_effect* core_effect = iforce->core_effects + core_id;
	struct resource* mod1_chunk = &(iforce->core_effects[core_id].mod1_chunk);
	struct resource* mod2_chunk = &(iforce->core_effects[core_id].mod2_chunk);
	int err = 0;
	
	err = make_period_modifier(iforce, mod1_chunk,
		effect->u.periodic.magnitude, effect->u.periodic.offset,
		effect->u.periodic.period, effect->u.periodic.phase);
	if (err) return err;
	set_bit(FF_MOD1_IS_USED, core_effect->flags);
 
        err = make_shape_modifier(iforce, mod2_chunk,
                effect->u.periodic.shape.attack_length,
		effect->u.periodic.shape.attack_level,
                effect->u.periodic.shape.fade_length,
		effect->u.periodic.shape.fade_level);
	if (err) return err;
	set_bit(FF_MOD2_IS_USED, core_effect->flags);
 
	switch (effect->u.periodic.waveform) {
		case FF_SQUARE:		wave_code = 0x20; break;
		case FF_TRIANGLE:	wave_code = 0x21; break;
		case FF_SINE:		wave_code = 0x22; break;
		case FF_SAW_UP:		wave_code = 0x23; break;
		case FF_SAW_DOWN:	wave_code = 0x24; break;
		default: 		wave_code = 0x20; break;
	}
 
	err = make_core(iforce, effect->id,
                mod1_chunk->start,
                mod2_chunk->start,
                wave_code,
		0x20,
                effect->replay.length,
		effect->replay.delay,
		effect->trigger.button,
		effect->trigger.interval,
		effect->u.periodic.direction);
 
	return err;
}

/*
 * Upload a constant force effect
 */
static int iforce_upload_constant(struct iforce* iforce, struct ff_effect* effect)
{
	int core_id = effect->id;
	struct iforce_core_effect* core_effect = iforce->core_effects + core_id;
	struct resource* mod1_chunk = &(iforce->core_effects[core_id].mod1_chunk);
	struct resource* mod2_chunk = &(iforce->core_effects[core_id].mod2_chunk);
	int err = 0;

	printk(KERN_DEBUG "iforce.c: make constant effect\n");
 
	err = make_magnitude_modifier(iforce, mod1_chunk, effect->u.constant.level);
	if (err) return err;
	set_bit(FF_MOD1_IS_USED, core_effect->flags);
 
	err = make_shape_modifier(iforce, mod2_chunk,
		effect->u.constant.shape.attack_length,
		effect->u.constant.shape.attack_level,
		effect->u.constant.shape.fade_length,
		effect->u.constant.shape.fade_level);
	if (err) return err;
	set_bit(FF_MOD2_IS_USED, core_effect->flags);
 
	err = make_core(iforce, effect->id,
		mod1_chunk->start,
		mod2_chunk->start,
		0x00,
		0x20,
		effect->replay.length,
		effect->replay.delay,
		effect->trigger.button,
		effect->trigger.interval,
		effect->u.constant.direction);
 
	return err;                                                               
}

/*
 * Upload an interactive effect. Those are for example friction, inertia, springs...
 */
static int iforce_upload_interactive(struct iforce* iforce, struct ff_effect* effect)
{
	int core_id = effect->id;
	struct iforce_core_effect* core_effect = iforce->core_effects + core_id;
	struct resource* mod_chunk = &(core_effect->mod1_chunk);
	u8 type, axes;
	u16 mod1, mod2, direction;
	int err = 0;

	printk(KERN_DEBUG "iforce.c: make interactive effect\n");

	switch (effect->type) {
		case FF_SPRING:      type = 0x40; break;
		case FF_FRICTION:    type = 0x41; break;
		default: return -1;
	}

	err = make_interactive_modifier(iforce, mod_chunk,
		effect->u.interactive.right_saturation,
		effect->u.interactive.left_saturation,
		effect->u.interactive.right_coeff,
		effect->u.interactive.left_coeff,
		effect->u.interactive.deadband,
		effect->u.interactive.center);
	if (err) return err;
	set_bit(FF_MOD1_IS_USED, core_effect->flags);

	/* Only X axis */
	if (effect->u.interactive.axis == BIT(FF_X)) {
		mod1 = mod_chunk->start;
		mod2 = 0xffff;
		direction = 0x5a00;
		axes = 0x40;
	}
	/* Only Y axis */
	else if (effect->u.interactive.axis == BIT(FF_Y)) {
		mod1 = 0xffff;
		mod2 = mod_chunk->start;
		direction = 0xb400;
		axes = 0x80;
	} 
	/* Only one axis, choose orientation */
	else if (effect->u.interactive.axis == 0) {
		mod1 = mod_chunk->start;
		mod2 = 0xffff;
		direction = effect->u.interactive.direction;
		axes = 0x20;
	}
	/* Both X and Y axes */
	else if ( effect->u.interactive.axis == (BIT(FF_X)|BIT(FF_Y)) ) {
		/* TODO: same setting for both axes is not mandatory */
		mod1 = mod_chunk->start;
		mod2 = mod_chunk->start;
		direction = 0x6000;
		axes = 0xc0;
	}
	/* Error */
	else {
		return -1;
	}

	err = make_core(iforce, effect->id, 
		mod1, mod2,
		type, axes,
		effect->replay.length, effect->replay.delay,
		effect->trigger.button, effect->trigger.interval,
		direction);

	return err;
}

/*
 * Function called when an ioctl is performed on the event dev entry.
 * It uploads an effect to the device
 */
static int iforce_upload_effect(struct input_dev *dev, struct ff_effect *effect)
{
	struct iforce* iforce = (struct iforce*)(dev->private);
	int err = 0;

	printk(KERN_DEBUG "iforce.c: upload effect\n");

	/* 
	 * Get a free id
	 */
	{
		int id;
		for (id=0; id < FF_EFFECTS_MAX; ++id) {
			if (!test_bit(FF_CORE_IS_USED, iforce->core_effects[id].flags)) break;
		}
		if ( id == FF_EFFECTS_MAX || id >= iforce->n_effects_max ) {
			return -ENOMEM;
		}
		effect->id = id;
		set_bit(FF_CORE_IS_USED, iforce->core_effects[id].flags);
	}
	
	switch (effect->type) {
	case FF_PERIODIC:
		err = iforce_upload_periodic(iforce, effect);
		break;

	case FF_CONSTANT:
		err = iforce_upload_constant(iforce, effect);
		break;

	case FF_SPRING:
	case FF_FRICTION:
		err = iforce_upload_interactive(iforce, effect);
		break;
	};

	return err;
}

static int iforce_erase_effect(struct input_dev *dev, int effect_id)
{
	struct iforce* iforce = (struct iforce*)(dev->private);
	int err = 0;
	struct iforce_core_effect* core_effect;

	printk(KERN_DEBUG "iforce.c: erase effect %d\n", effect_id);

	if (effect_id < 0 || effect_id >= FF_EFFECTS_MAX) {
		return -EINVAL;
	}

	core_effect = iforce->core_effects + effect_id;

	if (test_bit(FF_MOD1_IS_USED, core_effect->flags)) {
		err = release_resource(&(iforce->core_effects[effect_id].mod1_chunk));
	}
	if (!err && test_bit(FF_MOD2_IS_USED, core_effect->flags)) {
		err = release_resource(&(iforce->core_effects[effect_id].mod2_chunk));
	}
	/*TODO: remember to change that if more FF_MOD* bits are added */
	core_effect->flags[0] = 0;

	return err;
}

static void iforce_wake(struct iforce *iforce)
{
	if (iforce->init_done == FF_INIT_ALL_MASK)
		if (waitqueue_active(&iforce->wait))
			wake_up(&iforce->wait);
}

static void iforce_process_packet(struct iforce *iforce, u16 cmd, unsigned char *data)
{
	struct input_dev *dev = &iforce->dev;

	switch (HI(cmd)) {

		case 0x01:	/* joystick position data */
		case 0x03:	/* wheel position data */

			if (~iforce->init_done & FF_INIT_DEV_TYPE) {
				iforce->type = HI(cmd);
				iforce->init_done |= FF_INIT_DEV_TYPE;
				iforce_wake(iforce);
			}

			if (HI(cmd) == 1) {
				input_report_abs(dev, ABS_X, (__s16) (((__s16)data[1] << 8) | data[0]));
				input_report_abs(dev, ABS_Y, (__s16) (((__s16)data[3] << 8) | data[2]));
				input_report_abs(dev, ABS_THROTTLE, 255 - data[4]);
			} else {
				input_report_abs(dev, ABS_WHEEL, (__s16) (((__s16)data[1] << 8) | data[0]));
				input_report_abs(dev, ABS_GAS,   255 - data[2]);
				input_report_abs(dev, ABS_BRAKE, 255 - data[3]);
			}

			input_report_abs(dev, ABS_HAT0X, iforce_hat_to_axis[data[6] >> 4].x);
			input_report_abs(dev, ABS_HAT0Y, iforce_hat_to_axis[data[6] >> 4].y);

			input_report_key(dev, BTN_TRIGGER, data[5] & 0x01);
			input_report_key(dev, BTN_TOP,     data[5] & 0x02);
			input_report_key(dev, BTN_THUMB,   data[5] & 0x04);
			input_report_key(dev, BTN_TOP2,    data[5] & 0x08);
			input_report_key(dev, BTN_BASE,    data[5] & 0x10);
			input_report_key(dev, BTN_BASE2,   data[5] & 0x20);
			input_report_key(dev, BTN_BASE3,   data[5] & 0x40);
			input_report_key(dev, BTN_BASE4,   data[5] & 0x80);
			input_report_key(dev, BTN_BASE5,   data[6] & 0x01);
			input_report_key(dev, BTN_A,       data[6] & 0x02);
			input_report_key(dev, BTN_B,       data[6] & 0x04);
			input_report_key(dev, BTN_C,       data[6] & 0x08);
			break;


		case 0x02:	/* status report */

			input_report_key(dev, BTN_DEAD, data[0] & 0x02);
			break;
			
		case 0xff:	/* autodetect report */


			switch (data[0]) {

				case 0x42: 	/* Effect memory size */

					if (~iforce->init_done & FF_INIT_RAMSIZE) {
						iforce->device_memory.end = (data[2] << 8) | data[1];
						iforce->init_done |= FF_INIT_RAMSIZE;
						iforce_wake(iforce);
					}
					break;

				case 0x4d:	/* Vendor ID */

					if (~iforce->init_done & FF_INIT_VID) {
						iforce->dev.idvendor = (data[2] << 8) | data[1];
						iforce->init_done |= FF_INIT_VID;
						iforce_wake(iforce);
					}
					break;

				case 0x4e:	/* Number of simultaneous effects */

					if (~iforce->init_done & FF_INIT_N_EFFECTS) {
						iforce->n_effects_max = data[1];
						iforce->init_done |= FF_INIT_N_EFFECTS;
						iforce_wake(iforce);
					}
					break;

				case 0x50:	/* Product ID */

					if (~iforce->init_done & FF_INIT_PID) {
						iforce->dev.idproduct = (data[2] << 8) | data[1];
						iforce->init_done |= FF_INIT_PID;
						iforce_wake(iforce);
					}
					break;

				case 0x43: /* No data? */
				case 0x45: /* Device revision? */
				case 0x4f: /* No data? */
				case 0x56: /* I-Force protocol revision? */

				default:
					dump_packet("rcff", cmd, data);
			}
			break;

		default:
			dump_packet("recv", cmd, data);
	}
}


static int iforce_open(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	switch (dev->idbus) {
#ifdef IFORCE_USB
		case BUS_USB:
			if (iforce->open++)
				break;
			iforce->irq.dev = iforce->usbdev;
			if (usb_submit_urb(&iforce->irq))
					return -EIO;
			break;
#endif
	}
	return 0;
}

static void iforce_close(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	switch (dev->idbus) {
#ifdef IFORCE_USB
		case BUS_USB:
			if (!--iforce->open)
				usb_unlink_urb(&iforce->irq);
			break;
#endif
	}
}

static void iforce_init_device(struct iforce *iforce)
{
	DECLARE_WAITQUEUE(wait, current);
	int i, timeout = HZ; /* 1 second */

	iforce->dev.private = iforce;
	iforce->dev.open = iforce_open;
	iforce->dev.close = iforce_close;
	iforce->dev.event = iforce_input_event;
	iforce->dev.upload_effect = iforce_upload_effect;
	iforce->dev.erase_effect = iforce_erase_effect;

	iforce->device_memory.name = "I-Force device effect memory";
	iforce->device_memory.start = 0;
	iforce->device_memory.end = 200;
	iforce->device_memory.flags = IORESOURCE_MEM;
	iforce->device_memory.parent = NULL;
	iforce->device_memory.child = NULL;
	iforce->device_memory.sibling = NULL;

	iforce->n_effects_max = 10;
	iforce->type = 1;

	init_waitqueue_head(&iforce->wait);
	iforce_open(&iforce->dev);

	for (i = 0; ff_init_data[i].cmd; i++)
		send_packet(iforce, ff_init_data[i].cmd, ff_init_data[i].data);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&iforce->wait, &wait);

	while (timeout && (iforce->init_done != FF_INIT_ALL_MASK))
		timeout = schedule_timeout(timeout);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&iforce->wait, &wait);
	iforce_close(&iforce->dev);

	if (!timeout)
		printk(KERN_WARNING "iforce.c: Init timeout %#lx. Detection may not be accurate.\n", iforce->init_done);

	iforce->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS) | BIT(EV_FF);
	iforce->dev.keybit[LONG(BTN_JOYSTICK)] |= BIT(BTN_TRIGGER) | BIT(BTN_TOP) | BIT(BTN_THUMB) | BIT(BTN_TOP2) |
					BIT(BTN_BASE) | BIT(BTN_BASE2) | BIT(BTN_BASE3) | BIT(BTN_BASE4) | BIT(BTN_BASE5);
	iforce->dev.keybit[LONG(BTN_GAMEPAD)] |= BIT(BTN_A) | BIT(BTN_B) | BIT(BTN_C);

	switch (iforce->type) {

		case 0x01: /* Joystick */
			iforce->dev.absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_THROTTLE) | BIT(ABS_HAT0X) | BIT(ABS_HAT0Y);
			iforce->dev.ffbit[0] = BIT(FF_X) | BIT(FF_Y);
			iforce->dev.name = iforce_name_joystick;
			break;

		case 0x03: /* Wheel */
			iforce->dev.absbit[0] = BIT(ABS_WHEEL) | BIT(ABS_GAS) | BIT(ABS_BRAKE) | BIT(ABS_HAT0X) | BIT(ABS_HAT0Y);
			iforce->dev.ffbit[0] = BIT(FF_X);
			iforce->dev.name = iforce_name_wheel;
			break;
	}

	for (i = ABS_X; i <= ABS_Y; i++) {
		iforce->dev.absmax[i] =  1920;
		iforce->dev.absmin[i] = -1920;
		iforce->dev.absflat[i] = 128;
		iforce->dev.absfuzz[i] = 16;
	}

	for (i = ABS_THROTTLE; i <= ABS_RUDDER; i++) {
		iforce->dev.absmax[i] = 255;
		iforce->dev.absmin[i] = 0;
	}

	for (i = ABS_HAT0X; i <= ABS_HAT0Y; i++) {
		iforce->dev.absmax[i] =  1;
		iforce->dev.absmin[i] = -1;
	}

	input_register_device(&iforce->dev);
}

#ifdef IFORCE_USB

static void iforce_usb_irq(struct urb *urb)
{
	struct iforce *iforce = urb->context;
	if (urb->status) return;
	iforce_process_packet(iforce,
		(iforce->data[0] << 8) | (urb->actual_length - 1), iforce->data + 1);
}

static void iforce_usb_out(struct urb *urb)
{
	 struct iforce *iforce = urb->context;

	if (urb->status)
		printk(KERN_WARNING "iforce.c: nonzero output urb status %d\n", urb->status);

	if (waitqueue_active(&iforce->wait))
		wake_up(&iforce->wait);
}

static void *iforce_usb_probe(struct usb_device *dev, unsigned int ifnum,
			      const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *epirq, *epout;
	struct iforce *iforce;

	epirq = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;
	epout = dev->config[0].interface[ifnum].altsetting[0].endpoint + 1;

	if (!(iforce = kmalloc(sizeof(struct iforce) + 32, GFP_KERNEL))) return NULL;
	memset(iforce, 0, sizeof(struct iforce));

	iforce->dev.idbus = BUS_USB;
	iforce->dev.idvendor = dev->descriptor.idVendor;
	iforce->dev.idproduct = dev->descriptor.idProduct;
	iforce->dev.idversion = dev->descriptor.bcdDevice;

	iforce->usbdev = dev;

	FILL_INT_URB(&iforce->irq, dev, usb_rcvintpipe(dev, epirq->bEndpointAddress),
			iforce->data, 16, iforce_usb_irq, iforce, epirq->bInterval);

	FILL_BULK_URB(&iforce->out, dev, usb_sndbulkpipe(dev, epout->bEndpointAddress),
                        iforce + 1, 32, iforce_usb_out, iforce);

	iforce_init_device(iforce);

	printk(KERN_INFO "input%d: %s [%04x:%04x, %d effects, %ld bytes memory] on usb%d:%d.%d\n",
		 iforce->dev.number, iforce->dev.name, iforce->dev.idvendor, iforce->dev.idproduct,
		 iforce->n_effects_max, iforce->device_memory.end, dev->bus->busnum, dev->devnum, ifnum);

	return iforce;
}

static void iforce_usb_disconnect(struct usb_device *dev, void *ptr)
{
	struct iforce *iforce = ptr;
	usb_unlink_urb(&iforce->irq);
	input_unregister_device(&iforce->dev);
	kfree(iforce);
}

static struct usb_device_id iforce_usb_ids [] = {
	{ USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_WMFORCE) },
	{ USB_DEVICE(USB_VENDOR_ID_LOGITECH, USB_DEVICE_ID_LOGITECH_FORMULA) },
	{ }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, iforce_usb_ids);

static struct usb_driver iforce_usb_driver = {
	name:		"iforce",
	probe:		iforce_usb_probe,
	disconnect:	iforce_usb_disconnect,
	id_table:	iforce_usb_ids,
};

#endif

#ifdef IFORCE_232

static void iforce_serio_irq(struct serio *serio, unsigned char data, unsigned int flags)
{
        struct iforce* iforce = serio->private;

	if (!iforce->pkt) {
		if (data != 0x2b) {
			return;
		}
		iforce->pkt = 1;
		return;
	}

        if (!iforce->id) {
		if (data > 3 && data != 0xff) {
			iforce->pkt = 0;
			return;
		}
                iforce->id = data;
		return;
        }

	if (!iforce->len) {
		if (data > IFORCE_MAX_LENGTH) {
			iforce->pkt = 0;
			iforce->id = 0;
			return;
		}
		iforce->len = data;
		return;
	}

        if (iforce->idx < iforce->len) {
                iforce->csum += iforce->data[iforce->idx++] = data;
		return;
	}

        if (iforce->idx == iforce->len) {
		iforce_process_packet(iforce, (iforce->id << 8) | iforce->idx, iforce->data);
		iforce->pkt = 0;
		iforce->id  = 0;
                iforce->len = 0;
                iforce->idx = 0;
		iforce->csum = 0;
        }
}

static void iforce_serio_connect(struct serio *serio, struct serio_dev *dev)
{
	struct iforce *iforce;
	if (serio->type != (SERIO_RS232 | SERIO_IFORCE))
		return;

	if (!(iforce = kmalloc(sizeof(struct iforce), GFP_KERNEL))) return;
	memset(iforce, 0, sizeof(struct iforce));

	iforce->dev.idbus = BUS_RS232;
	iforce->dev.idvendor = SERIO_IFORCE;
	iforce->dev.idproduct = 0x0001;
	iforce->dev.idversion = 0x0100;

	iforce->serio = serio;
	serio->private = iforce;

	if (serio_open(serio, dev)) {
		kfree(iforce);
		return;
	}

	iforce_init_device(iforce);

	printk(KERN_INFO "input%d: %s [%04x:%04x, %d effects, %ld bytes memory] on serio%d\n",
		iforce->dev.number, iforce->dev.name, iforce->dev.idvendor, iforce->dev.idproduct,
		iforce->n_effects_max, iforce->device_memory.end, serio->number);
}

static void iforce_serio_disconnect(struct serio *serio)
{
	struct iforce* iforce = serio->private;

	input_unregister_device(&iforce->dev);
	serio_close(serio);
	kfree(iforce);
}

static struct serio_dev iforce_serio_dev = {
	interrupt:	iforce_serio_irq,
	connect:	iforce_serio_connect,
	disconnect:	iforce_serio_disconnect,
};

#endif

static int __init iforce_init(void)
{
#ifdef IFORCE_USB
	usb_register(&iforce_usb_driver);
#endif
#ifdef IFORCE_232
	serio_register_device(&iforce_serio_dev);
#endif
	return 0;
}

static void __exit iforce_exit(void)
{
#ifdef IFORCE_USB
	usb_deregister(&iforce_usb_driver);
#endif
#ifdef IFORCE_232
	serio_unregister_device(&iforce_serio_dev);
#endif
}

module_init(iforce_init);
module_exit(iforce_exit);
