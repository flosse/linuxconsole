/*
 *  usbusbkbd.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  USB HIDBP Keyboard support
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
#include <linux/malloc.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/usb.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

static unsigned char usbkbd_keycode[256] = {
	  0,  0,  0,  0, 30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
	 50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44,  2,  3,
	  4,  5,  6,  7,  8,  9, 10, 11, 28,  1, 14, 15, 57, 12, 13, 26,
	 27, 43, 84, 39, 40, 41, 51, 52, 53, 58, 59, 60, 61, 62, 63, 64,
	 65, 66, 67, 68, 87, 88, 99, 70,119,110,102,104,111,107,109,106,
	105,108,103, 69, 98, 55, 74, 78, 96, 79, 80, 81, 75, 76, 77, 71,
	 72, 73, 82, 83, 86,127,116,117, 85, 89, 90, 91, 92, 93, 94, 95,
	120,121,122,123,134,138,130,132,128,129,131,137,133,135,136,113,
	115,114,  0,  0,  0,  0,  0,124,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
	 29, 42, 56,125, 97, 54,100,126,164,166,165,163,161,115,114,113,
	150,158,159,128,136,177,178,176,142,152,173,140
};

struct usbkbd {
	struct input_dev dev;
	unsigned char new[8];
	unsigned char old[8];
	struct urb irq, led;
	devrequest dr;
	unsigned char leds;
	int open;
};

static void usbkbd_irq(struct urb *urb)
{
	struct usbkbd *usbkbd = urb->context;
	int i;

	if (urb->status) return;

	for (i = 0; i < 8; i++)
		input_report_key(&usbkbd->dev, usbkbd_keycode[i + 224], (usbkbd->new[0] >> i) & 1);

	for (i = 2; i < 8; i++) {

		if (usbkbd->old[i] > 3 && memscan(usbkbd->new + 2, usbkbd->old[i], 6) == usbkbd->new + 8) {
			if (usbkbd_keycode[usbkbd->old[i]])
				input_report_key(&usbkbd->dev, usbkbd_keycode[usbkbd->old[i]], 0);
			else
				info("Unknown key (scancode %#x) released.", usbkbd->old[i]);
		}

		if (usbkbd->new[i] > 3 && memscan(usbkbd->old + 2, usbkbd->new[i], 6) == usbkbd->old + 8) {
			if (usbkbd_keycode[usbkbd->new[i]])
				input_report_key(&usbkbd->dev, usbkbd_keycode[usbkbd->new[i]], 1);
			else
				info("Unknown key (scancode %#x) pressed.", usbkbd->new[i]);
		}
	}

	memcpy(usbkbd->old, usbkbd->new, 8);
}

int usbkbd_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct usbkbd *usbkbd = dev->private;

	if (type != EV_LED) return -1;

	if (usbkbd->led.status == -EINPROGRESS) {
		warn("had to kill led urb");
		usb_unlink_urb(&usbkbd->led);
	}

	usbkbd->leds = (!!test_bit(LED_KANA,    dev->led) << 3) | (!!test_bit(LED_COMPOSE, dev->led) << 3) |
		       (!!test_bit(LED_SCROLLL, dev->led) << 2) | (!!test_bit(LED_CAPSL,   dev->led) << 1) |
		       (!!test_bit(LED_NUML,    dev->led));


	if (usb_submit_urb(&usbkbd->led)) {
		err("usb_submit_urb(leds) failed");
		return -1;
	}

	return 0;
}

static void usbkbd_led(struct urb *urb)
{
	if (urb->status)
		warn("led urb status %d received", urb->status);
}

static int usbkbd_open(struct input_dev *dev)
{
	struct usbkbd *usbkbd = dev->private;

	if (usbkbd->open++)
		return 0;

	 if (usb_submit_urb(&usbkbd->irq)) {
		kfree(usbkbd);
		return -EIO;
	}
}

static void usbkbd_close(struct input_dev *dev)
{
	struct usbkbd *usbkbd = dev->private;

	if (!--usbkbd->open)
		usb_unlink_urb(&usbkbd->irq);
}

static void *usbkbd_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint;
	struct usbkbd *usbkbd;
	int i;

	if (dev->descriptor.bNumConfigurations != 1) return NULL;
	interface = dev->config[0].interface[ifnum].altsetting + 0;

	if (interface->bInterfaceClass != 3) return NULL;
	if (interface->bInterfaceSubClass != 1) return NULL;
	if (interface->bInterfaceProtocol != 1) return NULL;
	if (interface->bNumEndpoints != 1) return NULL;

	endpoint = interface->endpoint + 0;
	if (!(endpoint->bEndpointAddress & 0x80)) return NULL;
	if ((endpoint->bmAttributes & 3) != 3) return NULL;

	usb_set_protocol(dev, interface->bInterfaceNumber, 0);
	usb_set_idle(dev, interface->bInterfaceNumber, 0, 0);

	if (!(usbkbd = kmalloc(sizeof(struct usbkbd), GFP_KERNEL))) return NULL;
	memset(usbkbd, 0, sizeof(struct usbkbd));

	usbkbd->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_LED) | BIT(EV_REP);
	usbkbd->dev.ledbit[0] = BIT(LED_NUML) | BIT(LED_CAPSL) | BIT(LED_SCROLLL) | BIT(LED_COMPOSE) | BIT(LED_KANA);

	for (i = 0; i < 255; i++)
		set_bit(usbkbd_keycode[i], usbkbd->dev.keybit);
	clear_bit(0, usbkbd->dev.keybit);
	
	usbkbd->dev.private = usbkbd;
	usbkbd->dev.event = usbkbd_event;

	{
		int pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
		int maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));

		FILL_INT_URB(&usbkbd->irq, dev, pipe, usbkbd->new, maxp > 8 ? 8 : maxp,
			usbkbd_irq, usbkbd, endpoint->bInterval);
	}

	usbkbd->dr.requesttype = USB_TYPE_CLASS | USB_RECIP_INTERFACE;
	usbkbd->dr.request = USB_REQ_SET_REPORT;
	usbkbd->dr.value = 0x200;
	usbkbd->dr.index = interface->bInterfaceNumber;
	usbkbd->dr.length = 1;

	FILL_CONTROL_URB(&usbkbd->led, dev, usb_sndctrlpipe(dev, 0),
		(void*) &usbkbd->dr, &usbkbd->leds, 1, usbkbd_led, usbkbd);
			

	input_register_device(&usbkbd->dev);

	printk(KERN_INFO "input%d: USB HIDBP keyboard\n", usbkbd->dev.number);


	return usbkbd;
}

static void usbkbd_disconnect(struct usb_device *dev, void *ptr)
{
	struct usbkbd *usbkbd = ptr;
	usb_unlink_urb(&usbkbd->irq);
	input_unregister_device(&usbkbd->dev);
	kfree(usbkbd);
}

static struct usb_driver usbkbd_driver = {
	name:		"keyboard",
	probe:		usbkbd_probe,
	disconnect:	usbkbd_disconnect
};

static int __init usbkbd_init(void)
{
	usb_register(&usbkbd_driver);
	return 0;
}

static void __exit usbkbd_exit(void)
{
	usb_deregister(&usbkbd_driver);
}

module_init(usbkbd_init);
module_exit(usbkbd_exit);
