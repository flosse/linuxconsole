/*
 *  magellan.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 */

/*
 * This is a module for the Linux input driver, supporting
 * the Magellan and Space Mouse 6dof controllers.
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
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/input.h>
#include <linux/serio.h>

/*
 * Definitions & global arrays.
 */

#define	MAGELLAN_MAX_LENGTH	32

static int magellan_buttons[] = { BTN_0, BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8};
static int magellan_axes[] = { ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ};

/*
 * Per-Magellan data.
 */


struct magellan {
	struct input_dev dev;
	struct serio *serio;
	int idx;
	unsigned char data[MAGELLAN_MAX_LENGTH];
};

/*
 * magellan_crunch_nibbles() verifies that the bytes sent from the Magellan
 * have correct upper nibbles for the lower ones, if not, the packet will
 * be thrown away. It also strips these upper halves to simplify further
 * processing.
 */

static int magellan_crunch_nibbles(unsigned char *data, int count)
{
	static unsigned char nibbles[16] = "0AB3D56GH9:K<MN?";

	do {
		if (data[count] == nibbles[data[count] & 0xf])
			data[count] = data[count] & 0xf;
		else
			return -1;
	} while (--count);

	return 0;
}

/*
 * magellan_process_packet() decodes packets the driver receives from the
 * Magellan. It updates the data accordingly, and sets an ACK flag
 * to the type of last packet received, if received OK.
 */

static void magellan_process_packet(struct magellan* magellan)
{
	struct input_dev *dev = &magellan->dev;
	unsigned char *data = magellan->data;
	int i, t;

	if (!magellan->idx) return;

	switch (magellan->data[0]) {

		case 'd':				/* Axis data */
			if (magellan->idx != 25) return;
			if (magellan_crunch_nibbles(data, 24)) return;
			for (i = 0; i < 6; i++)
				input_report_abs(dev, magellan_axes[i],
					(data[(i << 2) + 1] << 12 | data[(i << 2) + 2] << 8 |
					 data[(i << 2) + 3] <<  4 | data[(i << 2) + 4]) - 32768);
			break;

		case 'k':				/* Button data */
			if (magellan->idx != 4) return;
			if (magellan_crunch_nibbles(data, 3)) return;
			t = (data[1] << 1) | (data[2] << 5) | data[3];
			for (i = 0; i < 9; i++) input_report_key(dev, magellan_buttons[i], (t >> i) & 1);
			break;
	}
}

/*
 * magellan_interrupt() is called by the low level driver when characters
 * are ready for us. We then buffer them for further processing, or call the
 * packet processing routine.
 */

static void magellan_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct magellan* magellan = serio->private;

	if (data == '\r') {
		magellan_process_packet(magellan);
		magellan->idx = 0;
	} else {
		if (magellan->idx < MAGELLAN_MAX_LENGTH)
			magellan->data[magellan->idx++] = data;
	} 
}

/*
 * magellan_disconnect() is the opposite of magellan_connect()
 */

static void magellan_disconnect(struct serio *serio)
{
	struct magellan* magellan = serio->private;
	input_unregister_device(&magellan->dev);
	serio_close(serio);
	kfree(magellan);
}

/*
 * magellan_connect() is the routine that is called when someone adds a
 * new serio device. It looks for the Magellan, and if found, registers
 * it as an input device.
 */

static void magellan_connect(struct serio *serio, struct serio_dev *dev)
{
	struct magellan *magellan;
	int i;

	if (serio->type != (SERIO_RS232 | SERIO_MAGELLAN))
		return;

	if (!(magellan = kmalloc(sizeof(struct magellan), GFP_KERNEL)))
		return;

	memset(magellan, 0, sizeof(struct magellan));

	magellan->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);	
	for (i = 0; i < 9; i++) set_bit(magellan_buttons[i], &magellan->dev.keybit);
	for (i = 0; i < 6; i++) set_bit(magellan_axes[i], &magellan->dev.absbit);

	magellan->serio = serio;
	magellan->dev.private = magellan;
	
	serio->private = magellan;

	if (serio_open(serio, dev)) {
		kfree(magellan);
		return;
	}

	input_register_device(&magellan->dev);

	printk(KERN_INFO "input%d: Magellan on serio%d\n", magellan->dev.number, serio->number);
}

/*
 * The serio device structure.
 */

static struct serio_dev magellan_dev = {
	interrupt:	magellan_interrupt,
	connect:	magellan_connect,
	disconnect:	magellan_disconnect,
};

/*
 * The functions for inserting/removing us as a module.
 */

#ifdef MODULE
int init_module(void)
#else
int magellan_init(void)
#endif
{
	serio_register_device(&magellan_dev);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	serio_unregister_device(&magellan_dev);
}
#endif
