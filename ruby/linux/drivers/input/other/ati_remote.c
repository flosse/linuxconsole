/*
 *
 *  Copyright (c) 2002 Vladimir Dergachev
 *
 *  USB ATI Remote support
 *
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
 * e-mail - mail your message to volodya@mindspring.com
 *
 * This driver was derived from usbati_remote and usbkbd drivers by Vojtech Pavlik
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb.h>

/*
 * Version Information
 */
#define DRIVER_VERSION "v0.1"
#define DRIVER_AUTHOR "Vladimir Dergachev <volodya@minspring.com>"
#define DRIVER_DESC "USB ATI Remote driver"

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL");

/* Get hi and low bytes of a 16-bits int */
#define HI(a)	((unsigned char)((a) >> 8))
#define LO(a)	((unsigned char)((a) & 0xff))

struct ati_remote {
	unsigned char data[8];
	char name[128];
	unsigned char old[2];
	unsigned long old_jiffies;
	struct usb_device *usbdev;
	struct input_dev dev;
	struct urb irq, out;
	wait_queue_head_t wait;
	devrequest dr;
	int open;
};

static char init1[]={
	0x01, 0x00, 0x20, 0x14 };
static char init2[]={
	0x01, 0x00, 0x20, 0x14, 0x20, 0x20, 0x20 };

#define KIND_END	0
#define KIND_LITERAL	1
#define KIND_FILTERED	2
#define KIND_LU		3
#define KIND_RU		4
#define KIND_LD		5
#define KIND_RD		6
#define KIND_ACCEL	7

static struct {
	short kind;
	unsigned char data1, data2;
	int type; 
	unsigned int code;
	int value;
	} ati_remote_translation_table[]={
	{KIND_LITERAL, 0x3d, 0x78, EV_KEY, BTN_LEFT, 1},	/* left ati_remote button */
	{KIND_LITERAL, 0x3e, 0x79, EV_KEY, BTN_LEFT, 0},
	{KIND_LITERAL, 0x41, 0x7c, EV_KEY, BTN_RIGHT, 1},	/* right ati_remote button */
	{KIND_LITERAL, 0x42, 0x7d, EV_KEY, BTN_RIGHT, 0},
		/* ati_remote */
	{KIND_ACCEL, 0x35, 0x70, EV_REL, REL_X, -1},   /* left */
	{KIND_ACCEL, 0x36, 0x71, EV_REL, REL_X, 1},   /* right */
	{KIND_ACCEL, 0x37, 0x72, EV_REL, REL_Y, -1},   /* up */
	{KIND_ACCEL, 0x38, 0x73, EV_REL, REL_Y, 1},   /* down */

	{KIND_LU, 0x39, 0x74, EV_REL, 0, 0},   /* left up */
	{KIND_RU, 0x3a, 0x75, EV_REL, 0, 0},   /* right up */
	{KIND_LD, 0x3c, 0x77, EV_REL, 0, 0},   /* left down */
	{KIND_RD, 0x3b, 0x76, EV_REL, 0, 0},   /* right down */

		/* keyboard.. */
	{KIND_FILTERED, 0xe2, 0x1d, EV_KEY, KEY_LEFT, 1},   /* key left */
	{KIND_FILTERED, 0xe4, 0x1f, EV_KEY, KEY_RIGHT, 1},   /* key right */
	{KIND_FILTERED, 0xe7, 0x22, EV_KEY, KEY_DOWN, 1},   /* key down */
	{KIND_FILTERED, 0xdf, 0x1a, EV_KEY, KEY_UP, 1},   /* key left */

	{KIND_FILTERED, 0xe3, 0x1e, EV_KEY, KEY_ENTER, 1},   /* key enter */

	{KIND_FILTERED, 0xd2, 0x0d, EV_KEY, KEY_1, 1},   
	{KIND_FILTERED, 0xd3, 0x0e, EV_KEY, KEY_2, 1},   
	{KIND_FILTERED, 0xd4, 0x0f, EV_KEY, KEY_3, 1},  
	{KIND_FILTERED, 0xd5, 0x10, EV_KEY, KEY_4, 1},  
	{KIND_FILTERED, 0xd6, 0x11, EV_KEY, KEY_5, 1},  
	{KIND_FILTERED, 0xd7, 0x12, EV_KEY, KEY_6, 1},   
	{KIND_FILTERED, 0xd8, 0x13, EV_KEY, KEY_7, 1},   
	{KIND_FILTERED, 0xd9, 0x14, EV_KEY, KEY_8, 1},   
	{KIND_FILTERED, 0xda, 0x15, EV_KEY, KEY_9, 1},   
	{KIND_FILTERED, 0xdc, 0x17, EV_KEY, KEY_0, 1},   

	{KIND_FILTERED, 0xdd, 0x18, EV_KEY, KEY_KPENTER, 1},   /* key "checkbox" */

	{KIND_FILTERED, 0xc5, 0x00, EV_KEY, KEY_A, 1},   
	{KIND_FILTERED, 0xc6, 0x01, EV_KEY, KEY_B, 1},   
	{KIND_FILTERED, 0xde, 0x19, EV_KEY, KEY_C, 1},   
	{KIND_FILTERED, 0xe0, 0x1b, EV_KEY, KEY_D, 1},   
	{KIND_FILTERED, 0xe6, 0x21, EV_KEY, KEY_E, 1},   
	{KIND_FILTERED, 0xe8, 0x23, EV_KEY, KEY_F, 1},   

	{KIND_FILTERED, 0xdb, 0x16, EV_KEY, KEY_MENU, 1},   /* key menu */
	{KIND_FILTERED, 0xc7, 0x02, EV_KEY, KEY_POWER, 1},   /* key power */
	{KIND_FILTERED, 0xc8, 0x03, EV_KEY, KEY_PROG1, 1},   /* key TV */
	{KIND_FILTERED, 0xc9, 0x04, EV_KEY, KEY_PROG2, 1},   /* key DVD */
	{KIND_FILTERED, 0xca, 0x05, EV_KEY, KEY_WWW, 1},   /* key Web */
	{KIND_FILTERED, 0xcb, 0x06, EV_KEY, KEY_BOOKMARKS, 1},   /* key "open book" */
	{KIND_FILTERED, 0xcc, 0x07, EV_KEY, KEY_EDIT, 1},   /* key "hand" */
	{KIND_FILTERED, 0xe1, 0x1c, EV_KEY, KEY_COFFEE, 1},   /* key "timer" */
	
	{KIND_FILTERED, 0xce, 0x09, EV_KEY, KEY_VOLUMEDOWN, 1},  
	{KIND_FILTERED, 0xcd, 0x08, EV_KEY, KEY_VOLUMEUP, 1},   
	{KIND_FILTERED, 0xcf, 0x0a, EV_KEY, KEY_MUTE, 1},   
	{KIND_FILTERED, 0xd1, 0x0c, EV_KEY, KEY_PAGEDOWN, 1},    /* prev channel*/
	{KIND_FILTERED, 0xd0, 0x0b, EV_KEY, KEY_PAGEUP, 1},   /* next channel */
	
	{KIND_FILTERED, 0xec, 0x27, EV_KEY, KEY_RECORD, 1},   
	{KIND_FILTERED, 0xea, 0x25, EV_KEY, KEY_PLAYCD, 1},   /* ke pay */
	{KIND_FILTERED, 0xe9, 0x24, EV_KEY, KEY_REWIND, 1},   
	{KIND_FILTERED, 0xeb, 0x26, EV_KEY, KEY_FORWARD, 1},   
	{KIND_FILTERED, 0xed, 0x28, EV_KEY, KEY_STOP, 1},   
	{KIND_FILTERED, 0xee, 0x29, EV_KEY, KEY_PAUSE, 1},   

	{KIND_FILTERED, 0xe5, 0x20, EV_KEY, KEY_FRONT, 1},   /* maximize */
	
	{KIND_END, 0x00, 0x00, EV_MAX+1, 0, 0} /* END */
	};

/*
 * Send a packet of bytes to the device
 */
static void send_packet(struct ati_remote *ati_remote, u16 cmd, unsigned char* data)
{
	DECLARE_WAITQUEUE(wait, current);
	int timeout = HZ; /* 1 second */

	memcpy(ati_remote->out.transfer_buffer + 1, data, LO(cmd));
	((char*)ati_remote->out.transfer_buffer)[0] = HI(cmd);
	ati_remote->out.transfer_buffer_length = LO(cmd) + 1;
	ati_remote->out.dev = ati_remote->usbdev;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&ati_remote->wait, &wait);

	if (usb_submit_urb(&ati_remote->out)) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&ati_remote->wait, &wait);
		return;
		}

	while (timeout && ati_remote->out.status == -EINPROGRESS)
		timeout = schedule_timeout(timeout);

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&ati_remote->wait, &wait);

	if (!timeout)
		usb_unlink_urb(&ati_remote->out);
}

static void ati_remote_irq(struct urb *urb)
{
	struct ati_remote *ati_remote = urb->context;
	unsigned char *data = ati_remote->data;
	struct input_dev *dev = &ati_remote->dev;
	int i;
	int accel;

	if (urb->status) return;
	
	if(urb->actual_length==4){
		if((data[0]!=0x14)||(data[3]!=0xf0))
			printk("** weird key=%02x%02x%02x%02x\n", data[0], data[1], data[2], data[3]);
		} else
	if(urb->actual_length==1){
		if((data[0]!=(unsigned char)0xff)&&(data[0]!=0x00))
			printk("** weird byte=0x%02x\n", data[0]);
		} else {
		printk("length=%d  %02x %02x %02x %02x %02x %02x %02x %02x\n",
			urb->actual_length, data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
		}

	accel=1;
	if((ati_remote->old[0]==data[1])&&(ati_remote->old[1]==data[2])){
		if(ati_remote->old_jiffies+4*HZ<jiffies)accel=8;
			else
		if(ati_remote->old_jiffies+3*HZ<jiffies)accel=6;
			else
		if(ati_remote->old_jiffies+2*HZ<jiffies)accel=4;
			else
		if(ati_remote->old_jiffies+HZ<jiffies)accel=3;
			else
		if(ati_remote->old_jiffies+(HZ>>1)<jiffies)accel=2;
		}
	if((urb->actual_length==4) && (data[0]==0x14) && (data[3]==0xf0)){
		for(i=0;ati_remote_translation_table[i].kind!=KIND_END;i++){
			if((ati_remote_translation_table[i].data1==data[1]) && (ati_remote_translation_table[i].data2==data[2])){
				switch(ati_remote_translation_table[i].kind){
					case KIND_LITERAL:
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							ati_remote_translation_table[i].value);
						break;
					case KIND_ACCEL:
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							ati_remote_translation_table[i].value*accel);
						break;
					case KIND_LU:
						input_report_rel(dev, REL_X, -accel);
						input_report_rel(dev, REL_Y, -accel);
						break;
					case KIND_RU:
						input_report_rel(dev, REL_X, accel);
						input_report_rel(dev, REL_Y, -accel);
						break;
					case KIND_LD:
						input_report_rel(dev, REL_X, -accel);
						input_report_rel(dev, REL_Y, accel);
						break;
					case KIND_RD:
						input_report_rel(dev, REL_X, accel);
						input_report_rel(dev, REL_Y, accel);
						break;
					case KIND_FILTERED:
						if((ati_remote->old[0]==data[1])&&(ati_remote->old[1]==data[2])&&((ati_remote->old_jiffies+(HZ>>2))>jiffies)){
							return;
							}
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							1);
						input_event(dev, ati_remote_translation_table[i].type,
							ati_remote_translation_table[i].code,
							0);
						break;
					default:
						printk("kind=%d\n", ati_remote_translation_table[i].kind);
					}
				break;
				}
			}
		if(ati_remote_translation_table[i].kind==KIND_END){
			printk("** unknown key=%02x%02x\n", data[1], data[2]);
			}
		if((ati_remote->old[0]!=data[1])||(ati_remote->old[1]!=data[2]))
			ati_remote->old_jiffies=jiffies;
		ati_remote->old[0]=data[1];
		ati_remote->old[1]=data[2];
		}
}

static int ati_remote_open(struct input_dev *dev)
{
	struct ati_remote *ati_remote = dev->private;

	printk("ati_remote_open %d\n", ati_remote->open);

	if (ati_remote->open++)
		return 0;

	ati_remote->irq.dev = ati_remote->usbdev;
	if (usb_submit_urb(&ati_remote->irq))
		return -EIO;

	printk("done: ati_remote_open now open=%d\n", ati_remote->open);
	return 0;
}

static void ati_remote_close(struct input_dev *dev)
{
	struct ati_remote *ati_remote = dev->private;

	if (!--ati_remote->open)
		usb_unlink_urb(&ati_remote->irq);
}

static void ati_remote_usb_out(struct urb *urb)
{
	struct ati_remote *ati_remote = urb->context;
	if (urb->status) return;
	if (waitqueue_active(&ati_remote->wait))
		wake_up(&ati_remote->wait);
}

static void *ati_remote_probe(struct usb_device *dev, unsigned int ifnum,
			     const struct usb_device_id *id)
{
	struct usb_interface *iface;
	struct usb_interface_descriptor *interface;
	struct usb_endpoint_descriptor *endpoint, *epout;
	struct ati_remote *ati_remote;
	int pipe, maxp;
	char *buf;
	int i;


	iface = &dev->actconfig->interface[ifnum];
	interface = &iface->altsetting[iface->act_altsetting];

	if (interface->bNumEndpoints != 2) return NULL;

	/* use the first endpoint only for now */
	endpoint = interface->endpoint + 0;
	if (!(endpoint->bEndpointAddress & 0x80)) return NULL;
	if ((endpoint->bmAttributes & 3) != 3) return NULL;
	epout = interface->endpoint + 1;

	pipe = usb_rcvintpipe(dev, endpoint->bEndpointAddress);
	maxp = usb_maxpacket(dev, pipe, usb_pipeout(pipe));
	printk("maxp=%d endpoint=0x%02x\n", maxp, endpoint->bEndpointAddress);

	usb_set_idle(dev, interface->bInterfaceNumber, 0, 0);

	if (!(ati_remote = kmalloc(sizeof(struct ati_remote)+32, GFP_KERNEL))) return NULL;
	memset(ati_remote, 0, sizeof(struct ati_remote)+32);

	ati_remote->usbdev = dev;

	for(i=0;ati_remote_translation_table[i].kind!=KIND_END;i++)
		if(ati_remote_translation_table[i].type==EV_KEY)
			set_bit(ati_remote_translation_table[i].code, ati_remote->dev.keybit);
	clear_bit(BTN_LEFT, ati_remote->dev.keybit);
	clear_bit(BTN_RIGHT, ati_remote->dev.keybit);

	ati_remote->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	ati_remote->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT) | BIT(BTN_MIDDLE);
	ati_remote->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);
	ati_remote->dev.keybit[LONG(BTN_MOUSE)] |= BIT(BTN_SIDE) | BIT(BTN_EXTRA);
	ati_remote->dev.relbit[0] |= BIT(REL_WHEEL);


	ati_remote->dev.private = ati_remote;
	ati_remote->dev.open = ati_remote_open;
	ati_remote->dev.close = ati_remote_close;

	ati_remote->dev.name = ati_remote->name;
	ati_remote->dev.idbus = BUS_USB;
	ati_remote->dev.idvendor = dev->descriptor.idVendor;
	ati_remote->dev.idproduct = dev->descriptor.idProduct;
	ati_remote->dev.idversion = dev->descriptor.bcdDevice;
	init_waitqueue_head(&ati_remote->wait);

	ati_remote->dr.requesttype = USB_TYPE_VENDOR | USB_DIR_IN | USB_RECIP_INTERFACE;
	ati_remote->dr.index = 0;
	ati_remote->dr.length = 16;
	
	ati_remote->old[0]=0;
	ati_remote->old[1]=0;
	ati_remote->old_jiffies=jiffies;

	if (!(buf = kmalloc(63, GFP_KERNEL))) {
		kfree(ati_remote);
		return NULL;
	}

	if (dev->descriptor.iManufacturer &&
		usb_string(dev, dev->descriptor.iManufacturer, buf, 63) > 0)
			strcat(ati_remote->name, buf);
	if (dev->descriptor.iProduct &&
		usb_string(dev, dev->descriptor.iProduct, buf, 63) > 0)
			sprintf(ati_remote->name, "%s %s", ati_remote->name, buf);

	if (!strlen(ati_remote->name))
		sprintf(ati_remote->name, "USB HIDBP Mouse %04x:%04x",
			ati_remote->dev.idvendor, ati_remote->dev.idproduct);

	kfree(buf);

	FILL_INT_URB(&ati_remote->irq, dev, pipe, ati_remote->data, maxp > 8 ? 8 : maxp,
		ati_remote_irq, ati_remote, endpoint->bInterval);

	FILL_INT_URB(&ati_remote->out, dev, usb_sndintpipe(dev, epout->bEndpointAddress),
			ati_remote + 1, 32, ati_remote_usb_out, ati_remote, epout->bInterval);

	input_register_device(&ati_remote->dev);

	printk(KERN_INFO "input%d: %s on usb%d:%d.%d\n",
		 ati_remote->dev.number, ati_remote->name, dev->bus->busnum, dev->devnum, ifnum);

	send_packet(ati_remote, 0x8004, init1);
	send_packet(ati_remote, 0x8007, init2);
	usb_unlink_urb(&(ati_remote->out));
	return ati_remote;
}

static void ati_remote_disconnect(struct usb_device *dev, void *ptr)
{
	struct ati_remote *ati_remote = ptr;
	usb_unlink_urb(&ati_remote->irq);
	input_unregister_device(&ati_remote->dev);
	kfree(ati_remote);
}

static struct usb_device_id ati_remote_id_table [] = {
	{ USB_DEVICE(0x0bc7, 0x004) },
    { }						/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, ati_remote_id_table);

static struct usb_driver ati_remote_driver = {
	name:		"ati_remote",
	probe:		ati_remote_probe,
	disconnect:	ati_remote_disconnect,
	id_table:	ati_remote_id_table,
};

static int __init ati_remote_init(void)
{
	usb_register(&ati_remote_driver);
	info(DRIVER_VERSION ":" DRIVER_DESC);
	return 0;
}

static void __exit ati_remote_exit(void)
{
	usb_deregister(&ati_remote_driver);
}

module_init(ati_remote_init);
module_exit(ati_remote_exit);
