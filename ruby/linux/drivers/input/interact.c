/*
 * $Id$
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Toby Deshane
 *
 *  Sponsored by SuSE
 */

/*
 * InterAct HammerHead/FX gamepad driver for Linux
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
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>

#define INTERACT_MAX_START	400	/* 400 us */
#define INTERACT_MAX_STROBE	40	/* 40 us */
#define INTERACT_MAX_LENGTH	32	/* 32 bits */
#define INTERACT_REFRESH_TIME	HZ/50	/* 20 ms */

#define INTERACT_TYPE_HHFX	1	/* HammerHead/FX */

struct interact {
	struct gameport *gameport;
	struct input_dev dev;
	struct timer_list timer;
	int type;
	int used;
	int bads;
	int reads;
};

static unsigned char interact_abs[] = { ABS_RX, ABS_RY, ABS_X, ABS_Y };
static short interact_btn[] = { BTN_TR, BTN_X, BTN_Y, BTN_Z, BTN_A, BTN_B, BTN_C, BTN_TL, BTN_TL2, BTN_TR2, BTN_MODE, BTN_SELECT };

/*
 * interact_read_packet() reads and Hammerhead/FX joystick data.
 */

static int interact_read_packet(struct gameport *gameport, int length, u32 *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t, s;
	int i;

	i = 0;
	data[0] = data[1] = data[2] = 0;
	t = gameport_time(gameport, INTERACT_MAX_START);
	s = gameport_time(gameport, INTERACT_MAX_STROBE);

	__save_flags(flags);
	__cli();
	gameport_trigger(gameport);
	v = gameport_read(gameport);

	while (t > 0 && i < length) {
		t--;
		u = v; v = gameport_read(gameport);
		if (v & ~u & 0x40) {
			data[0] = (data[0] << 1) | ((v >> 4) & 1);
			data[1] = (data[1] << 1) | ((v >> 5) & 1);
			data[2] = (data[1] << 1) | ((v >> 7) & 1);
			i++;
			t = s;
		}
	}

	__restore_flags(flags);

	return i;
}

/*
 * interact_timer() reads and analyzes InterAct joystick data.
 */

static void interact_timer(unsigned long private)
{
	struct interact *interact = (struct interact *) private;
	struct input_dev *dev = &interact->dev;
	u32 data[3];
	int i;

	interact->reads++;

	if (interact_read_packet(interact->gameport, INTERACT_MAX_LENGTH, data) < INTERACT_MAX_LENGTH) {
		interact->bads++;
	} else

	switch (interact->type) {

		case INTERACT_TYPE_HHFX:

#if 0
			printk("data0: %08x, data1: %08x data2: %08x\n", data[0], data[1], data[2]); 
#endif

			for (i = 0; i < 4; i++)
				input_report_abs(dev, interact_abs[i], (data[i & 1] >> ((i >> 1) << 3)) & 0xff);

			for (i = 0; i < 2; i++)
				input_report_abs(dev, ABS_HAT0Y - i,
					((data[1] >> ((i << 1) + 17)) & 1)  - ((data[1] >> ((i << 1) + 16)) & 1));

			for (i = 0; i < 8; i++)
				input_report_key(dev, interact_btn[i], (data[0] >> (i + 16)) & 1);

			for (i = 0; i < 4; i++)
				input_report_key(dev, interact_btn[i + 8], (data[1] >> (i + 20)) & 1);

			break;

	}

	mod_timer(&interact->timer, jiffies + INTERACT_REFRESH_TIME);

}

/*
 * interact_open() is a callback from the input open routine.
 */

static int interact_open(struct input_dev *dev)
{
	struct interact *interact = dev->private;
	if (!interact->used++)
		mod_timer(&interact->timer, jiffies + INTERACT_REFRESH_TIME);	
	return 0;
}

/*
 * interact_close() is a callback from the input close routine.
 */

static void interact_close(struct input_dev *dev)
{
	struct interact *interact = dev->private;
	if (!--interact->used)
		del_timer(&interact->timer);
}

/*
 * interact_connect() probes for InterAct joysticks.
 */

static void interact_connect(struct gameport *gameport, struct gameport_dev *dev)
{
	struct interact *interact;
	__u32 data[3];
	int i, t;

	if (!(interact = kmalloc(sizeof(struct interact), GFP_KERNEL)))
		return;
	memset(interact, 0, sizeof(struct interact));

	gameport->private = interact;

	interact->gameport = gameport;
	init_timer(&interact->timer);
	interact->timer.data = (long) interact;
	interact->timer.function = interact_timer;

	if (gameport_open(gameport, dev, GAMEPORT_MODE_RAW))
		goto fail1;

	i = interact_read_packet(gameport, INTERACT_MAX_LENGTH, data);

	if (i != 32 || (data[0] >> 24) != 0x0c || (data[1] >> 24) != 0x02)
		goto fail2;

	interact->type = INTERACT_TYPE_HHFX;

	interact->dev.private = interact;
	interact->dev.open = interact_open;
	interact->dev.close = interact_close;
	interact->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

	for (i = 0; i < 4; i++) {
		t = interact_abs[i];
		set_bit(t, interact->dev.absbit);
		interact->dev.absmin[t] = 0;
		interact->dev.absmax[t] = 255;
	}

	for (i = 0; i < 2; i++) {
		t = ABS_HAT0X + i;
		set_bit(t, interact->dev.absbit);
		interact->dev.absmin[t] = -1;
		interact->dev.absmax[t] = 1;
	}

	for (i = 0; i < 12; i++)
		set_bit(interact_btn[i], interact->dev.keybit);

	input_register_device(&interact->dev);
	printk(KERN_INFO "input%d: HammerHead/FX on gameport%d.0\n",
		interact->dev.number, gameport->number);

	return;
fail2:	gameport_close(gameport);
fail1:  kfree(interact);
}

static void interact_disconnect(struct gameport *gameport)
{
	struct interact *interact = gameport->private;
	input_unregister_device(&interact->dev);
	gameport_close(gameport);
	kfree(interact);
}

static struct gameport_dev interact_dev = {
	connect:	interact_connect,
	disconnect:	interact_disconnect,
};

int __init interact_init(void)
{
	gameport_register_device(&interact_dev);
	return 0;
}

void __exit interact_exit(void)
{
	gameport_unregister_device(&interact_dev);
}

module_init(interact_init);
module_exit(interact_exit);
