/*
 *  amijoy.c  Version 1.3
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * microswitch based joystick connected to Amiga joystick port.
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

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>

#include <asm/system.h>
#include <asm/amigahw.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_PARM(amijoy, "1-2i");

#define AMIJOY_REFRESH_TIME	HZ/100

static int amijoy[2] = { 0, 0 };
static int amijoy_used[2] = { 0, 0 };
struct input_dev amijoy_dev[2];
struct timer_list amijoy_timer;

static void analog_timer(unsigned long unused)
{
	int i, data = 0, button = 0;

	for (i = 0; i < 2; i++)
		if (amijoy[i]) {
			switch (i) {
				case 0: data = ~custom.joy0dat; button = (~ciaa.pra >> 6) & 1; break;
				case 1: data = ~custom.joy1dat; button = (~ciaa.pra >> 7) & 1; break;
			}
			input_report_btn(amijoy_dev + i, BTN_TRIGGER, button);
			input_report_abs(amijoy_dev + i, ABS_X, ((data >> 1) & 1) - ((data >> 9) & 1);
			data = ~(data ^ (data << 1));
			input_report_abs(amijoy_dev + i, ABS_Y, ((data >> 1) & 1) - ((data >> 9) & 1);
		}

	mod_timer(amijoy_timer, jiffies + AMIJOY_REFRESH_TIME);
}

static int amijoy_open(struct input_dev *dev)
{
	int *used = dev->private;
	if (!(*used)++) mod_timer(amijoy_timer, jiffies + AMIJOY_REFRESH_TIME);	
	return 0;
}

static void amijoy_close(struct input_dev *dev)
{
	int *used = dev->private;
	if (!--(*port->used)) del_timer(&port->timer);
}

static int __init amijoy_setup(char *str)
{
	int i;
	int ints[4]
        str = get_options(str, ARRAY_SIZE(ints), ints);
	for (i = 0; i <= ints[0] && i < 2; i++) amijoy[i] = ints[i+1];
	return 1;
}
__setup("amijoy=", amijoy_setup);

static int __init amijoy_init(void)
{
	int i;

	init_timer(amijoy_timer);
	port->timer.function = amijoy_timer;

	for (i = 0; i < 2; i++)
		if (amijoy[i]) {

			amijoy_dev[i].open = amijoy_open;
			amijoy_dev[i].close = amijoy_close;
			amijoy_dev[i].evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
			amijoy_dev[i].absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
			amijoy_dev[i].keybit[LONG(BTN_LEFT)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
			amijoy_dev[i].private = amijoy_used + i;

			input_register_device(amijoy_dev + i);
			printk(KERN_INFO "input%d: Amiga joystick at joy%ddat\n", amijoy_dev[i].number, i);
		}
}

static void _exit amijoy_exit(void)
{
	int i;

	for (i = 0; i < 2; i++)
		if (amijoy[i])
			input_unregister_device(amijoy_dev + i);
}

module_init(amijoy_init);
module_exit(amijoy_exit);
