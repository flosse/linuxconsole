/*
 * $Id$
 *
 *  Copyright (c) 2000 Vojtech Pavlik
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

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("USB/RS232 I-Force joysticks and wheels driver");

#define USB_VENDOR_ID_LOGITECH		0x046d
#define USB_DEVICE_ID_LOGITECH_WMFORCE	0xc281

#define IFORCE_MAX_LENGTH               16
 
#if defined(CONFIG_INPUT_IFORCE_232) || defined(CONFIG_INPUT_IFORCE_232_MODULE)
#define IFORCE_232
#endif
#if defined(CONFIG_INPUT_IFORCE_USB) || defined(CONFIG_INPUT_IFORCE_USB_MODULE)
#define IFORCE_USB
#endif

struct iforce {
        signed char data[IFORCE_MAX_LENGTH];
        struct usb_device *usbdev;
        struct serio *serio;    /* FF: needed by iforce_input_event */
        struct input_dev dev;
        struct urb irq;
        int open;
        int idx, pkt, len, id;
        unsigned char csum;
};

static struct {
        __s32 x;
        __s32 y;
} iforce_hat_to_axis[16] = {{ 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static char *iforce_name = "I-Force joystick/wheel";

/* FF: various macros */
/* Get hi and low bytes of a 16-bits int */
#define HI(a)	(((a) & 0xff00)>>8)
#define LOW(a)	((a) & 0x00ff)

/* Encode a time value */
#define TIME_SCALE(a)	((a)==0xffff?0xffff:(((a)*0x03e8)/0x0100))
 
/* Macros computing the damned mysterious magic values of effect ids */
#define MOD_ID_PERIOD(a)	((a) << 4)
#define MOD_ID_SHAPE(a)		(((a) << 4) | 0x0c)
#define MOD_ID_MAGNITUDE(a)	((a) << 4)
#define MOD_ID_POSITION_X(a)	(((a) << 4) | 0x0a)
#define MOD_ID_POSITION_Y(a)	(((a) << 4) | 0x0c)

/*
 * Send a packet of bytes to the device
 */
static void send_serio(struct serio* pserio, unsigned char* data)
{
	unsigned char i;
	unsigned char cs = 0;
	unsigned char len = data[2] +3;
 
	printk(KERN_DEBUG "ff msg:");
	for (i=0; i<len; ++i) {
		serio_write(pserio, data[i]);
		cs = cs^data[i];
		printk("%02x ", data[i]);
	}
	serio_write(pserio, cs);
	printk("%02x\n", cs);
} 

/*
 * Start or stop playing an effect
 */
int iforce_input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct serio* serio = ((struct iforce*)(dev->private))->serio;

	if (code & FF_PLAY) {
		unsigned char data[6] = {0x2b, 0x41, 0x03, 0x00, 0x00, 0x00};
		int effect_id = code & FF_CTRL_MASK;

	        printk(KERN_DEBUG "iforce ff: play effect %d\n", effect_id);
	        data[3] = (unsigned char)effect_id;
	        data[4] = (value == 1)?0x01:0x41;
	        data[5] = (unsigned char)value;
 
	        send_serio(serio, data);

		return 0; 
	}
	else if (code & FF_STOP) {
		unsigned char data[6] = {0x2b, 0x41, 0x03, 0x00, 0x00, 0x00};
		int effect_id = code & FF_CTRL_MASK;
 
		printk(KERN_DEBUG "iforce ff: stop effect %d\n", effect_id); 
		data[3] = (unsigned char)effect_id;
 
		send_serio(serio, data);

		return 0; 
	}
	return -1;
}

/*
 * Initialise the device
 */
void iforce_ff_init(struct iforce* iforce)
{
	unsigned char data[] = {
		0x2b, 0xff, 0x01, 0x4f, 0x9a, 0x2b, 0xff, 0x01,
		0x56, 0x83, 0x2b, 0xff, 0x01, 0x4e, 0x9b, 0x2b,
		0xff, 0x01, 0x42, 0x97, 0x2b, 0xff, 0x01, 0x4d,
		0x98, 0x2b, 0xff, 0x01, 0x50, 0x85, 0x2b, 0x40,
		0x03, 0x06, 0xf4, 0x01, 0x9b, 0x2b, 0x43, 0x01,
		0x80, 0xe9, 0x2b, 0x42, 0x01, 0x04, 0x6c, 0x2b,
		0x42, 0x01, 0x05, 0x6d, 0x2b, 0x40, 0x02, 0x03,
		0x29, 0x43, 0x2b, 0x40, 0x02, 0x04, 0x01, 0x6c,
		0x2b, 0x42, 0x01, 0x05, 0x6d, 0x2b, 0x40, 0x02,
		0x03, 0x29, 0x43, 0x2b, 0x40, 0x02, 0x04, 0x01,
		0x6c, 0x2b, 0x43, 0x01, 0x80, 0xe9, 0x2b, 0x42,
		0x01, 0x05, 0x6d, 0x2b, 0x40, 0x02, 0x04, 0x00,
		0x6d, 0x2b, 0x42, 0x01, 0x05, 0x6d, 0x2b, 0x40,
		0x02, 0x04, 0x00, 0x6d, 0x2b, 0x43, 0x01, 0x80,
		0xe9, 0x2b, 0x43, 0x01, 0x80, 0xe9, 0x2b, 0x40,
		0x02, 0x04, 0x00, 0x6d
	};

	int i;
	struct serio* serio = iforce->serio;
 
        printk(KERN_INFO "iforce ff: init\n");
        for (i=0; i<124; ++i) {
                serio->write(serio, data[i]);
        }                                    
}

/*
 * Deinitialise the device
 */
void iforce_ff_close(struct iforce* iforce)
{
	/* TODO: iforce_ff_close */
}

/*
 * Upload the component of an effect dealing with the period, phase and magnitude
 */
static void make_period_modifier(struct iforce* iforce,
	__u16 id, __s16 magnitude, __s16 offset, __u16 period, __u16 phase)
{
	unsigned char data[10] = {0x2b, 0x04, 0x07,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct serio* serio = iforce->serio;
 
	period = TIME_SCALE(period);
 
	data[3] = (unsigned char)LOW(MOD_ID_PERIOD(id));
	data[4] = (unsigned char)HI(MOD_ID_PERIOD(id));
 
	data[5] = (unsigned char)(magnitude >> 8);
	data[6] = (unsigned char)(offset >> 8);
	data[7] = (unsigned char)(phase >> 8);
 
	data[8] = (unsigned char)LOW(period);
	data[9] = (unsigned char)HI(period);
 
	send_serio(serio, data); 
}

/*
 * Uploads the part of an effect setting the shape of the force
 */
static void make_shape_modifier(struct iforce* iforce, __u16 id,
        __u16 attack_duration, __s16 initial_level,
        __u16 fade_duration, __s16 final_level)
{
	unsigned char data[11] = {0x2b, 0x02, 0x08,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct serio* serio = iforce->serio;
 
	attack_duration = TIME_SCALE(attack_duration);
	fade_duration = TIME_SCALE(fade_duration);
 
	data[3] = (unsigned char)LOW(MOD_ID_SHAPE(id));
	data[4] = (unsigned char)HI(MOD_ID_SHAPE(id));
 
	data[5] = (unsigned char)LOW(attack_duration);
	data[6] = (unsigned char)HI(attack_duration);
	data[7] = (unsigned char)(initial_level >> 8);
 
	data[8] = (unsigned char)LOW(fade_duration);
	data[9] = (unsigned char)HI(fade_duration);
	data[10] = (unsigned char)(final_level >> 8);
 
	send_serio(serio, data);
}

static void make_core(struct iforce* iforce, __u16 id, __u16 mod_id1, __u16 mod_id2,
	__u8 effect_type, __u16 duration, __u16 delay, __u16 button,
	__u16 interval, __u16 direction)
{
	unsigned char data[17] = {0x2b, 0x01, 0x0e,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	struct serio* serio = (struct serio*)iforce->serio;
	int isWheel = iforce->dev.ffbit[0] == BIT(FF_X);
 
	duration = TIME_SCALE(duration);
	delay = TIME_SCALE(delay);
	interval = TIME_SCALE(interval);
 
	data[3] = (unsigned char)id;
	data[4] = effect_type;
	data[5] = (unsigned char)((isWheel? 0x40: 0x20) |
				 ((button+1) & 0x0f));
	if (button == FF_BUTTON_NONE) data[5] &= 0xf0;
 
	data[6] = (unsigned char)LOW(duration);
	data[7] = (unsigned char)HI(duration);
 
	data[8] = (unsigned char)(isWheel?
				  0x5a: (direction >> 8));
 
	data[9] = (unsigned char)LOW(interval);
	data[10] = (unsigned char)HI(interval);
 
	data[11] = (unsigned char)LOW(mod_id1);
	data[12] = (unsigned char)HI(mod_id1);
	data[13] = (unsigned char)LOW(mod_id2);
	data[14] = (unsigned char)HI(mod_id2);
 
	data[15] = (unsigned char)LOW(delay);
	data[16] = (unsigned char)HI(delay);

	send_serio(serio, data);                                               
}

/*
 * Upload a periodic effect to the device
 */
static void iforce_upload_periodic(struct iforce* iforce, struct ff_effect* effect)
{
	__u8 wave_code;
 
	printk(KERN_DEBUG "iforce ff: make periodic effect \n"); 
	make_period_modifier(iforce, effect->id,
		effect->u.periodic.magnitude, effect->u.periodic.offset,
		effect->u.periodic.period, effect->u.periodic.phase);
 
        make_shape_modifier(iforce, effect->id,
                effect->u.periodic.shape.attack_length,
		effect->u.periodic.shape.attack_level,
                effect->u.periodic.shape.fade_length,
		effect->u.periodic.shape.fade_level);
 
	switch (effect->u.periodic.waveform) {
	case FF_SQUARE:		wave_code = 0x20; break;
	case FF_TRIANGLE:	wave_code = 0x21; break;
	case FF_SINE:		wave_code = 0x22; break;
	case FF_SAW_UP:		wave_code = 0x23; break;
	case FF_SAW_DOWN:	wave_code = 0x24; break;
	}
 
	make_core(iforce, effect->id,
                MOD_ID_PERIOD(effect->id),
                MOD_ID_SHAPE(effect->id),
                wave_code,
                effect->u.periodic.replay.length,
		effect->u.periodic.replay.delay,
		effect->u.periodic.trigger.button,
		effect->u.periodic.trigger.interval,
		effect->u.periodic.direction);
 
}

/*
 * Function called when an ioctl is performed on the event dev entry.
 * It uploads an effect to the device
 */
void iforce_upload_effect(struct input_dev *dev, struct ff_effect *effect)
{
	struct iforce* iforce = (struct iforce*)(dev->private);

	printk(KERN_DEBUG "iforce ff: upload effect\n");
	switch (effect->type) {
	case FF_PERIODIC:
		iforce_upload_periodic(iforce, effect);
		break;

	case FF_CONSTANT:
/*		iforce_upload_constant(serio, effect);*/
		break;

	case FF_SPRING:
	case FF_FRICTION:
/*		iforce_upload_position(serio, effect);*/
		break;
	};
}

static void iforce_process_packet(struct input_dev *dev, unsigned char id, int idx, unsigned char *data)
{
	switch (id) {

		case 1:	/* joystick position data */
		case 3: /* wheel position data */

			if (id == 1) {
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

		case 2: /* force feedback effect status */
			break;
	}
}

#ifdef IFORCE_USB

static int iforce_open(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	if (dev->idbus == BUS_USB && !iforce->open++) {
		iforce->irq.dev = iforce->usbdev;
		if (usb_submit_urb(&iforce->irq))
			return -EIO;
	}

	return 0;
}

static void iforce_close(struct input_dev *dev)
{
	struct iforce *iforce = dev->private;

	if (dev->idbus == BUS_USB && !--iforce->open)
		usb_unlink_urb(&iforce->irq);
}

#endif

static void iforce_input_setup(struct iforce *iforce)
{
	int i;

	iforce->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	iforce->dev.keybit[LONG(BTN_JOYSTICK)] |= BIT(BTN_TRIGGER) | BIT(BTN_TOP) | BIT(BTN_THUMB) | BIT(BTN_TOP2) |
					BIT(BTN_BASE) | BIT(BTN_BASE2) | BIT(BTN_BASE3) | BIT(BTN_BASE4) | BIT(BTN_BASE5);
	iforce->dev.keybit[LONG(BTN_GAMEPAD)] |= BIT(BTN_A) | BIT(BTN_B) | BIT(BTN_C);
	iforce->dev.absbit[0] = BIT(ABS_X) | BIT(ABS_Y) | BIT(ABS_THROTTLE) | BIT(ABS_HAT0X) | BIT(ABS_HAT0Y)
				| BIT(ABS_WHEEL) | BIT(ABS_GAS) | BIT(ABS_BRAKE);

	/*
	 * FF: set ffbit here
	 */
	/* TODO: auto detect, or use option */
	iforce->dev.ffbit[0] = BIT(FF_X) | BIT(FF_Y);

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

	iforce->dev.private = iforce;

#ifdef IFORCE_USB
	iforce->dev.open = iforce_open;
	iforce->dev.close = iforce_close;
#endif

#ifdef IFORCE_232
	iforce->dev.event = iforce_input_event;
#endif

	input_register_device(&iforce->dev);
}

#ifdef IFORCE_USB

static void iforce_usb_irq(struct urb *urb)
{
	struct iforce *iforce = urb->context;
	if (urb->status) return;
	iforce_process_packet(&iforce->dev, iforce->data[0], 8, iforce->data + 1);
}

static void *iforce_usb_probe(struct usb_device *dev, unsigned int ifnum,
			      const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *endpoint;
	struct iforce *iforce;

	endpoint = dev->config[0].interface[ifnum].altsetting[0].endpoint + 0;

	if (!(iforce = kmalloc(sizeof(struct iforce), GFP_KERNEL))) return NULL;
	memset(iforce, 0, sizeof(struct iforce));

	iforce->dev.name = iforce_name;
	iforce->dev.idbus = BUS_USB;
	iforce->dev.idvendor = dev->descriptor.idVendor;
	iforce->dev.idproduct = dev->descriptor.idProduct;
	iforce->dev.idversion = dev->descriptor.bcdDevice;

	FILL_INT_URB(&iforce->irq, dev, usb_rcvintpipe(dev, endpoint->bEndpointAddress),
			iforce->data, 8, iforce_usb_irq, iforce, endpoint->bInterval);

	iforce_input_setup(iforce);

	printk(KERN_INFO "input%d: %s on usb%d:%d.%d\n",
		 iforce->dev.number, iforce_name, dev->bus->busnum, dev->devnum, ifnum);

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
		if (data > 3) {
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
		iforce_process_packet(&iforce->dev, iforce->id, iforce->idx, iforce->data);
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

	iforce->dev.name = iforce_name;
	iforce->dev.idbus = BUS_RS232;
	iforce->dev.idvendor = SERIO_IFORCE;
	iforce->dev.idproduct = 0x0001;
	iforce->dev.idversion = 0x0100;

	/* FF: function to be called when a effect is to be uploaded */
	iforce->dev.upload_effect = iforce_upload_effect;

	/* FF: iforce_input_event() needs a reference to serio */
	iforce->serio = serio;

	serio->private = iforce;

	if (serio_open(serio, dev)) {
		kfree(iforce);
		return;
	}

	iforce_input_setup(iforce);

	/*
	 * FF: init codes here
	 */
	iforce_ff_init(iforce);

	printk(KERN_INFO "input%d: %s on serio%d\n",
		 iforce->dev.number, iforce_name, serio->number);
}

static void iforce_serio_disconnect(struct serio *serio)
{
	struct iforce* iforce = serio->private;

	/*
	 * FF: deinit codes here
	 */
	iforce_ff_close(iforce);

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
