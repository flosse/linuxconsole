/*
 * $Id$
 *
 *  Force feedback support for hid devices.
 *  Not all hid devices use the same protocol. For example, some use PID,
 *  other use their own proprietary procotol.
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

#undef DEBUG

#include <linux/usb.h>

#include "hid.h"

/* Drivers' initializing functions */
static int hid_lgff_init(struct hid_device* hid);

struct hid_ff_initializer {
	__u16 idVendor;
	__u16 idProduct;
	int (*init)(struct hid_device*);
};

static struct hid_ff_initializer inits[] = {
#ifdef CONFIG_LOGITECH_RUMBLE
	{0x46d, 0xc211, hid_lgff_init},
#endif
	{0, 0, NULL} /* Terminating entry */
};

static struct hid_ff_initializer *hid_get_ff_init(__u16 idVendor, __u16 idProduct)
{
	struct hid_ff_initializer *init;
	for (init = inits;
	     init->idVendor && 
		     !(init->idVendor == idVendor && init->idProduct == idProduct);
	     init++);

	return init->idVendor? init : NULL;
}

int hid_init_ff(struct hid_device* hid)
{
	struct hid_ff_initializer *init;

	init = hid_get_ff_init(hid->dev->descriptor.idVendor, hid->dev->descriptor.idProduct);

	return init? init->init(hid) : -ENOSYS;
}



/* Implements the protocol used by the Logitech WingMan Cordless rumble pad */

#ifdef CONFIG_LOGITECH_RUMBLE

#define LGFF_BUFFER_SIZE 8

struct hid_ff_logitech {
	struct urb* urbffout;                                           /* Output URB used to send ff commands */
	struct usb_ctrlrequest ffcr;                                    /* ff commands are sent using control URBs */
	char ffoutbuf[LGFF_BUFFER_SIZE];
	signed char rumble_left;                                        /* Magnitude of left motor */
	signed char rumble_right;                                       /* Magnitude of right motor */
	int rumble_play;                                                /* Enable rumbling */
};

static void hid_lgff_ctrl_out(struct urb *urb);
static int hid_lgff_upload_effect(struct input_dev* input, struct ff_effect* effect);
static void hid_lgff_exit(struct hid_device* hid);
static int hid_lgff_event(struct input_dev* input, unsigned int type, unsigned int code, int value);
static void hid_lgff_make_rumble(struct hid_device* hid);

static int hid_lgff_init(struct hid_device* hid)
{
	struct hid_ff_logitech *private;

	/* Private data */
	private = hid->ff_private = kmalloc(sizeof(struct hid_ff_logitech), GFP_KERNEL);
	if (!hid->ff_private) return -1;

	/* Event and exit callbacks */
	hid->exit_ff = hid_lgff_exit;
	hid->ff_event = hid_lgff_event;

	/* USB init */
	if (!(private->urbffout = usb_alloc_urb(0, GFP_KERNEL))) {
		kfree(hid->ff_private);
		return -1;
	}

	FILL_CONTROL_URB(private->urbffout, hid->dev, 0, (void*) &private->ffcr, private->ffoutbuf, 8, hid_lgff_ctrl_out, hid);
	dbg("Created ff output control urb");

	/* Input init */
	hid->input.upload_effect = hid_lgff_upload_effect;
	set_bit(FF_RUMBLE, hid->input.ffbit);
	set_bit(EV_FF, hid->input.evbit);
	hid->input.ff_effects_max = 1;

	return 0;
}

static void hid_lgff_exit(struct hid_device* hid)
{
	struct hid_ff_logitech *lgff = hid->ff_private;

	if (lgff->urbffout) {
		usb_unlink_urb(lgff->urbffout);
		usb_free_urb(lgff->urbffout);
	}
}

static int hid_lgff_event(struct input_dev* input, unsigned int type, unsigned int code, int value)
{
	struct hid_device *hid = input->private;
	struct hid_ff_logitech *lgff = hid->ff_private;

	if (type == EV_FF) {
		int old = lgff->rumble_play;
		lgff->rumble_play = (value!=0);
		if (old != lgff->rumble_play) hid_lgff_make_rumble(hid);

		return 0;
	}
	else return -EINVAL;
}

static void hid_lgff_make_rumble(struct hid_device* hid)
{
	struct hid_ff_logitech *lgff = hid->ff_private;
	char packet[] = {0x03, 0x42, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00};
	int err;
	
	dbg("in hid_make_rumble");
	memcpy(lgff->ffoutbuf, packet, 8);
	if (lgff->rumble_play) {
		lgff->ffoutbuf[3] = lgff->rumble_left;
		lgff->ffoutbuf[4] = lgff->rumble_right;
	} else {
		lgff->ffoutbuf[3] = 0;
		lgff->ffoutbuf[4] = 0;
	}

	lgff->urbffout->pipe = usb_sndctrlpipe(hid->dev, 0);
	lgff->ffcr.bRequestType = USB_TYPE_CLASS | USB_DIR_OUT | USB_RECIP_INTERFACE;
	lgff->urbffout->transfer_buffer_length = lgff->ffcr.wLength = 8;
	lgff->ffcr.bRequest = 9;
	lgff->ffcr.wValue = 0x0203;
	lgff->ffcr.wIndex = 0;
	
	lgff->urbffout->dev = hid->dev;
	
	if ((err=usb_submit_urb(lgff->urbffout, GFP_ATOMIC)))
		warn("usb_submit_urb returned %d", err);
	dbg("rumble urb submited");
}

static void hid_lgff_ctrl_out(struct urb *urb)
{
	struct hid_device *hid = urb->context;

	if (urb->status)
		warn("hid_irq_ffout status %d received", urb->status);
}

static int hid_lgff_upload_effect(struct input_dev* input, struct ff_effect* effect)
{
	struct hid_device* hid = input->private;
	struct hid_ff_logitech *lgff = hid->ff_private;

	dbg("ioctl rumble");

	if (!test_bit(effect->type, input->ffbit)) return -EINVAL;

	switch (effect->type) {
	case FF_RUMBLE:
		lgff->rumble_left = 0x80;
		lgff->rumble_right = 0x00;
		
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

#endif /* CONFIG_LOGITECH_RUMBLE */
