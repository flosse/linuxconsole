/*
 * $Id$
 *
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 *
 *  Based on the work of:
 *  	David Thompson
 *  	Joseph Krahn
 *
 *  Sponsored by SuSE
 */

/*
 * SpaceTec SpaceBall 2003/3003/4000 FLX driver for Linux
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
 *  Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/serio.h>

/*
 * Constants.
 */

#define SPACEBALL_MAX_LENGTH	128
static int spaceball_axes[] = { ABS_X, ABS_Z, ABS_Y, ABS_RX, ABS_RZ, ABS_RY };
static int spaceball_buttons[] = { BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8, BTN_9,
					BTN_A, BTN_B, BTN_C, BTN_MODE };
static char *spaceball_name = "SpaceTec SpaceBall 2003/3003/4000 FLX"; 

/*
 * Per-Ball data.
 */

struct spaceball {
	struct input_dev dev;
	struct serio *serio;
	int idx;
	int escape;
	unsigned char data[SPACEBALL_MAX_LENGTH];
};

/*
 * spaceball_process_packet() decodes packets the driver receives from the
 * SpaceBall.
 */

static void spaceball_process_packet(struct spaceball* spaceball)
{
	struct input_dev *dev = &spaceball->dev;
	unsigned char *data = spaceball->data;
	int i;

	if (spaceball->idx < 2) return;

	printk("%c %d\n", spaceball->data[0], spaceball->idx);

	switch (spaceball->data[0]) {

		case 'D':					/* Ball data */
			if (spaceball->idx != 15) return;
			for (i = 0; i < 6; i++)
				input_report_abs(dev, spaceball_axes[i], 
					(__s16)((data[2 * i + 3] << 8) | data[2 * i + 2]));
			break;

		case 'K':					/* Button data */
			if (spaceball->idx != 3) return;
			input_report_key(dev, BTN_1, (data[2] & 0x01) || (data[2] & 0x20));
			input_report_key(dev, BTN_2, data[2] & 0x02);
			input_report_key(dev, BTN_3, data[2] & 0x04);
			input_report_key(dev, BTN_4, data[2] & 0x08);
			input_report_key(dev, BTN_5, data[1] & 0x01);
			input_report_key(dev, BTN_6, data[1] & 0x02);
			input_report_key(dev, BTN_7, data[1] & 0x04);
			input_report_key(dev, BTN_8, data[1] & 0x10);
			break;

		case '.':					/* Advanced button data */
			if (spaceball->idx != 3) return;
			input_report_key(dev, BTN_1, data[2] & 0x01);
			input_report_key(dev, BTN_2, data[2] & 0x02);
			input_report_key(dev, BTN_3, data[2] & 0x04);
			input_report_key(dev, BTN_4, data[2] & 0x08);
			input_report_key(dev, BTN_5, data[2] & 0x10);
			input_report_key(dev, BTN_6, data[2] & 0x20);
			input_report_key(dev, BTN_7, data[2] & 0x80);
			input_report_key(dev, BTN_8, data[1] & 0x01);
			input_report_key(dev, BTN_9, data[1] & 0x02);
			input_report_key(dev, BTN_A, data[1] & 0x04);
			input_report_key(dev, BTN_B, data[1] & 0x08);
			input_report_key(dev, BTN_C, data[1] & 0x10);
			input_report_key(dev, BTN_MODE, data[1] & 0x20);
			break;

		case 'E':					/* Device error */
			spaceball->data[spaceball->idx - 1] = 0;
			printk(KERN_ERR "spaceball: Device error. [%s]\n", spaceball->data + 1);
			break;

		case '?':					/* Bad command packet */
			spaceball->data[spaceball->idx - 1] = 0;
			printk(KERN_ERR "spaceball: Bad command. [%s]\n", spaceball->data + 1);
			break;
	}
}

/*
 * Spaceball 4000 FLX packets all start with a one letter packet-type decriptor,
 * and end in 0x0d. It uses '^' as an escape for CR, XOFF and XON characters which
 * can occur in the axis values.
 */

static void spaceball_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct spaceball *spaceball = serio->private;

	switch (data) {
		case 0xd:
			spaceball_process_packet(spaceball);
			spaceball->idx = 0;
			spaceball->escape = 0;
			return;
		case '^':
			if (!spaceball->escape) {
				spaceball->escape = 1;
				return;
			}
			spaceball->escape = 0;
		case 'M':
		case 'Q':
		case 'S':
			if (spaceball->escape) {
				spaceball->escape = 0;
				data &= 0x1f;
			}
		default:
			if (spaceball->escape) {
				spaceball->escape = 0;
				printk(KERN_WARNING "spaceball.c: Unknown escaped character: %#x (%c)\n", data, data);
			}
			if (spaceball->idx < SPACEBALL_MAX_LENGTH)
				spaceball->data[spaceball->idx++] = data;
			return;
	}
}

/*
 * spaceball_disconnect() is the opposite of spaceball_connect()
 */

static void spaceball_disconnect(struct serio *serio)
{
	struct spaceball* spaceball = serio->private;
	input_unregister_device(&spaceball->dev);
	serio_close(serio);
	kfree(spaceball);
}

/*
 * spaceball_connect() is the routine that is called when someone adds a
 * new serio device. It looks for the Magellan, and if found, registers
 * it as an input device.
 */

static void spaceball_connect(struct serio *serio, struct serio_dev *dev)
{
	struct spaceball *spaceball;
	int i, t;

	if (serio->type != (SERIO_RS232 | SERIO_SPACEBALL))
		return;

	if (!(spaceball = kmalloc(sizeof(struct spaceball), GFP_KERNEL)))
		return;
	memset(spaceball, 0, sizeof(struct spaceball));

	spaceball->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);	
	spaceball->dev.keybit[LONG(BTN_0)] = BIT(BTN_0) | BIT(BTN_1);

	for (i = 0; i < 13; i++) {
		printk(KERN_DEBUG "%d\n", spaceball_buttons[i]);
		set_bit(spaceball_buttons[i], spaceball->dev.keybit);
	}

	for (i = 0; i < 6; i++) {
		t = spaceball_axes[i];
		set_bit(t, spaceball->dev.absbit);
		spaceball->dev.absmin[t] = i < 3 ? -8000 : -1600;
		spaceball->dev.absmax[t] = i < 3 ?  8000 :  1600;
		spaceball->dev.absflat[t] = i < 3 ? 40 : 8;
		spaceball->dev.absfuzz[t] = i < 3 ? 8 : 2;
	}

	spaceball->serio = serio;
	spaceball->dev.private = spaceball;

	spaceball->dev.name = spaceball_name;
	spaceball->dev.idbus = BUS_RS232;
	spaceball->dev.idvendor = SERIO_SPACEBALL;
	spaceball->dev.idproduct = 0x0001;
	spaceball->dev.idversion = 0x0100;
	
	serio->private = spaceball;

	if (serio_open(serio, dev)) {
		kfree(spaceball);
		return;
	}

	input_register_device(&spaceball->dev);

	printk(KERN_INFO "input%d: %s on serio%d\n",
		spaceball->dev.number, spaceball_name, serio->number);
}

/*
 * The serio device structure.
 */

static struct serio_dev spaceball_dev = {
	interrupt:	spaceball_interrupt,
	connect:	spaceball_connect,
	disconnect:	spaceball_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

int __init spaceball_init(void)
{
	serio_register_device(&spaceball_dev);
	return 0;
}

void __exit spaceball_exit(void)
{
	serio_unregister_device(&spaceball_dev);
}

module_init(spaceball_init);
module_exit(spaceball_exit);
