/*
 *  sermouse.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Serial mouse driver
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
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/config.h>
#include <linux/serio.h>
#include <linux/init.h>

static char *sermouse_protocols[] = { "None", "Mouse Systems", "Sun", "Microsoft", "Logitech M+",
						"Microsoft MZ", "Logitech MZ+", "Logitech MZ++"};

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");

struct sermouse {
	struct input_dev dev;
	struct serio *serio;
	char buf[8];
	unsigned char count;
	unsigned char type;
	unsigned long last;
};

/*
 * sermouse_process_msc() analyzes the incoming MSC/Sun bytestream and
 * generates events from it as soon as possible.
 */

static void sermouse_process_msc(struct sermouse *sermouse, char data)
{
	struct input_dev *dev = &sermouse->dev;

	switch (sermouse->count) {

		case 0:
			if ((data & 0xf8) != 0x80) return;
			input_report_key(dev, BTN_LEFT,   !(data & 4)); 
			input_report_key(dev, BTN_RIGHT,  !(data & 1));
			input_report_key(dev, BTN_MIDDLE, !(data & 2));
			break;

		case 1: 
		case 3: 
			sermouse->buf[0] = data;
			break;

		case 2: 
		case 4:
			input_report_rel(dev, REL_X, sermouse->buf[0]);
			input_report_rel(dev, REL_Y,            -data);
			break;

	}

	if (++sermouse->count == (5 - ((sermouse->type == SERIO_SUN) << 1)))
		sermouse->count = 0;
}

/*
 * sermouse_process_ms() anlyzes the incoming MS(Z/+/++) bytestream and
 * generates events.
 */

static void sermouse_process_ms(struct sermouse *sermouse, char data)
{
	struct input_dev *dev = &sermouse->dev;
	char *buf = sermouse->buf;

	if (data & 0x40) sermouse->count = 0;

	switch (sermouse->count) {

		case 0:

			input_report_key(dev, BTN_LEFT,   (data >> 5) & 1);
			input_report_key(dev, BTN_RIGHT,  (data >> 4) & 1);

			buf[1] = data;
			break;

		case 1:
			buf[2] = data;
			break;

		case 2:

			/* Guessing the state of the middle button on 3-button MS-protocol mice. This is ugly. */
			if ((sermouse->type == SERIO_MS) && !data && !buf[2] && !((buf[0] & 0xf0) ^ buf[1]))
				input_report_key(dev, BTN_MIDDLE, !test_bit(BTN_MIDDLE, dev->key));

			buf[0] = buf[1];

			input_report_rel(dev, REL_X, (char) (((buf[1] << 6) & 0xc0) | (buf[2] & 0x3f)));
			input_report_rel(dev, REL_Y, (char) (((buf[1] << 4) & 0xc0) | (data   & 0x3f)));

			break;

		case 3:

			switch (sermouse->type) {
			
				case SERIO_MS:
					 sermouse->type = SERIO_MP;

				case SERIO_MP:
					if ((data >> 2) & 3) break;	/* M++ Wireless Extension packet. */
					input_report_key(dev, BTN_MIDDLE, (data >> 5) & 1);
					input_report_key(dev, BTN_SIDE,   (data >> 4) & 1);
					break;

				case SERIO_MZP:
				case SERIO_MZPP:
					input_report_key(dev, BTN_SIDE,   (data >> 5) & 1);

				case SERIO_MZ:
					input_report_key(dev, BTN_MIDDLE, (data >> 4) & 1);
					input_report_rel(dev, REL_WHEEL, (data & 7) - (data & 8));
					break;
			}
					
			break;

		case 4:
		case 6:	/* MZ++ packet type. We can get these bytes for M++ too but we ignore them later. */
			buf[1] = (data >> 2) & 0x0f;
			break;

		case 5:
		case 7: /* Ignore anything besides MZ++ */
			if (sermouse->type != SERIO_MZPP) break;

			switch (buf[1]) {

				case 1: /* Extra mouse info */

					input_report_key(dev, BTN_SIDE, (data >> 4) & 1);
					input_report_key(dev, BTN_EXTRA, (data >> 5) & 1);
					input_report_rel(dev, data & 0x80 ? REL_HWHEEL : REL_WHEEL, (data & 7) - (data & 8));

					break;

				default: /* We don't decode anything else yet. */

					printk(KERN_WARNING
						"sermouse.c: Received MZ++ packet %x, don't know how to handle.\n", buf[1]);
					break;
			}

			break;
	}

	sermouse->count++;
}

/*
 * sermouse_interrupt() handles incoming characters, either gathering them into
 * packets or passing them to the command routine as command output.
 */

static void sermouse_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct sermouse *sermouse = serio->private;

	if (jiffies - sermouse->last > 2) sermouse->count = 0;
	sermouse->last = jiffies;

	if (sermouse->type > SERIO_SUN)
		sermouse_process_ms(sermouse, data);
	else
		sermouse_process_msc(sermouse, data);
}

/*
 * sermouse_disconnect() cleans up after we don't want talk
 * to the mouse anymore.
 */

static void sermouse_disconnect(struct serio *serio)
{
	struct sermouse *sermouse = serio->private;
	input_unregister_device(&sermouse->dev);
	serio_close(serio);
	kfree(sermouse);
}

/*
 * sermouse_connect() is a callback form the serio module when
 * an unhandled serio port is found.
 */

static void sermouse_connect(struct serio *serio, struct serio_dev *dev)
{
	struct sermouse *sermouse;
	unsigned char c;
	
	if ((serio->type & SERIO_TYPE) != SERIO_RS232)
		return;

	if (!(serio->type & SERIO_PROTO) || ((serio->type & SERIO_PROTO) > SERIO_MZPP))
		return;

	if (!(sermouse = kmalloc(sizeof(struct sermouse), GFP_KERNEL)))
		return;

	memset(sermouse, 0, sizeof(struct sermouse));

	sermouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	sermouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_RIGHT);
	sermouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);

	sermouse->serio = serio;
	sermouse->dev.private = sermouse;

	serio->private = sermouse;

	sermouse->type = serio->type & 0xff;
	c = (serio->type >> 16) & 0xff;

	if (c & 0x01) set_bit(BTN_MIDDLE, &sermouse->dev.keybit);
	if (c & 0x02) set_bit(BTN_SIDE, &sermouse->dev.keybit);
	if (c & 0x04) set_bit(BTN_EXTRA, &sermouse->dev.keybit);
	if (c & 0x10) set_bit(REL_WHEEL, &sermouse->dev.relbit);
	if (c & 0x20) set_bit(REL_HWHEEL, &sermouse->dev.relbit);

	if (serio_open(serio, dev)) {
		kfree(sermouse);
		return;
	}

	input_register_device(&sermouse->dev);
	
	printk(KERN_INFO "input%d: %s mouse on serio%d\n", sermouse->dev.number,
		sermouse_protocols[sermouse->type], serio->number);
}

static struct serio_dev sermouse_dev = {
	interrupt:	sermouse_interrupt,
	connect:	sermouse_connect,
	disconnect:	sermouse_disconnect
};

int __init sermouse_init(void)
{
	serio_register_device(&sermouse_dev);
	return 0;
}

void __exit sermouse_exit(void)
{
	serio_unregister_device(&sermouse_dev);
}

module_init(sermouse_init);
module_exit(sermouse_exit);
