/*
 *  analog.c  Version 2.0
 *
 *  Copyright (c) 1996-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * up to two analog (or CHF/FCS) joysticks on a single joystick port.
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

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/gameport.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

/*
 * Times, feature definitions.
 */

#define ANALOG_AXES_STD		0x0f
#define ANALOG_BUTTONS_STD	0xf0

#define ANALOG_BTNS_CHF		0x0100
#define ANALOG_HAT1_CHF		0x0200
#define ANALOG_HAT2_CHF		0x0400
#define ANALOG_ANY_CHF		0x0700
#define ANALOG_HAT_FCS		0x0800
#define ANALOG_HATS_ALL		0x0e00
#define ANALOG_BTN_TL		0x1000
#define ANALOG_BTN_TR		0x2000
#define ANALOG_BTN_TL2		0x4000
#define ANALOG_BTN_TR2		0x8000
#define ANALOG_BTNS_TLR		0x3000
#define ANALOG_BTNS_TLR2	0xc000
#define ANALOG_BTNS_GAMEPAD	0xf000
#define ANALOG_EXTENSIONS	0xff00

#define ANALOG_MAX_TIME		3000	/* 3 ms */
#define ANALOG_LOOP_TIME	2000	/* 2 t */
#define ANALOG_REFRESH_TIME	HZ/100	/* 10 ms */
#define ANALOG_AXIS_TIME	2	/* 20 ms */

#define ANALOG_MAX_NAME_LENGTH  128

static struct {
	int x;
	int y;
} analog_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1, 0}, { 0, 1}, {-1, 0}};

#define ANALOG_BUTTONS 	0
#define ANALOG_AXES	0
#define ANALOG_HATS	4

static int analog_hats[] = { ANALOG_HAT1_CHF, ANALOG_HAT2_CHF, ANALOG_ANY_CHF };
static int analog_axes[] = { ABS_X, ABS_Y, ABS_RUDDER, ABS_THROTTLE,
			     ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y };
static int analog_buttons[] = { BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2,
				BTN_TL, BTN_TR, BTN_TL2, BTN_TR2 };

struct analog {
	struct input_dev dev;
	int mask;
	char name[ANALOG_MAX_NAME_LENGTH];
};

struct analog_port {
	struct gameport *gameport;
	struct timer_list timer;
	struct analog analog[2];
	char mask;
	int bads;
	int reads;
	int speed;
	int loop;
	int timeout;
	int cooked;
	int axes[4];
	int buttons;
	int initial[4];
	int used;
	int axtime;
};

/*
 * Time macros.
 */

#ifdef __i386__
#ifdef CONFIG_X86_TSC
#define GET_TIME(x)	__asm__ __volatile__ ( "rdtsc" : "=a" (x) : : "dx" )
#define DELTA(x,y)	((y)-(x))
#define TIME_NAME "TSC"
#else
#define GET_TIME(x)	do { outb(0, 0x43); x = inb(0x40); x |= inb(0x40) << 8; } while (0)
#define DELTA(x,y)	((x)-(y)+((x)<(y)?1193180L/HZ:0))
#define TIME_NAME "PIT"
#endif
#elif __alpha__
#define GET_TIME(x)	__asm__ __volatile__ ( "rpcc %0" : "=r" (x) )
#define DELTA(x,y)	((y)-(x))
#define TIME_NAME "PCC"
#endif

#ifndef GET_TIME
#define FAKE_TIME
static unsigned long analog_faketime = 0;
#define GET_TIME(x)     do { x = analog_faketime++; } while(0)
#define DELTA(x,y)	((y)-(x))
#define TIME_NAME "Unreliable"
#endif

/*
 * analog_decode() decodes analog joystick data and reports input events.
 */

static void analog_decode(struct analog *analog, int *axes, int *initial, unsigned char buttons)
{
	struct input_dev *dev = &analog->dev;
	int i, j, k;
	int hat[3] = {0, 0, 0};

	if (analog->mask & ANALOG_ANY_CHF)
		switch (buttons & 0xf) {
			case 0x1: buttons = 0x01; break;
			case 0x2: buttons = 0x02; break;
			case 0x4: buttons = 0x04; break;
			case 0x8: buttons = 0x08; break;
			case 0x5: buttons = 0x10; break;
			case 0x9: buttons = 0x20; break;
			case 0xf: hat[0] = 1; break;
			case 0xb: hat[0] = 2; break;
			case 0x7: hat[0] = 3; break;
			case 0x3: hat[0] = 4; break;
			case 0xe: hat[1] = 1; break;
			case 0xa: hat[1] = 2; break;
			case 0x6: hat[1] = 3; break;
			case 0xc: hat[1] = 4; break;
		}

	k = ANALOG_BUTTONS;
	for (j = 0; j < 6; j++)
		if (analog->mask & (0x10 << j))
			input_report_btn(dev, analog_buttons[k++], (buttons >> j) & 1);

	if (analog->mask & ANALOG_BTN_TL)
		input_report_btn(dev, BTN_TL, axes[2] < (initial[2] >> 1));
	if (analog->mask & ANALOG_BTN_TR)
		input_report_btn(dev, BTN_TR, axes[3] < (initial[3] >> 1));
	if (analog->mask & ANALOG_BTN_TL2)
		input_report_btn(dev, BTN_TL2, axes[2] > (initial[2] + (initial[2] >> 1)));
	if (analog->mask & ANALOG_BTN_TR2)
		input_report_btn(dev, BTN_TR2, axes[3] > (initial[3] + (initial[3] >> 1)));

	if (analog->mask & ANALOG_HAT_FCS)
		for (j = 0; j < 4; j++)
			if (axes[3] < ((initial[3] * ((j << 1) + 1)) >> 3)) {
				hat[2] = j + 1;
				break;
			}

	k = ANALOG_AXES;
	for (j = 0; j < 4; j++)
		if (analog->mask & (1 << j))
			input_report_abs(dev, analog_axes[k++], axes[j]);

	k = ANALOG_HATS;
	for (i = 0; i < 3; i++)
		if (analog->mask & analog_hats[i]) {
			input_report_abs(dev, analog_axes[k++], analog_hat_to_axis[hat[i]].x);
			input_report_abs(dev, analog_axes[k++], analog_hat_to_axis[hat[i]].y);
		}
}

/*
 * analog_cooked_read() reads analog joystick data.
 */

static int analog_cooked_read(struct analog_port *port)
{
	struct gameport *gameport = port->gameport;
	unsigned int time[4], start, loop, now;
	unsigned char data[4], this, last;
	unsigned long flags;
	int i, j;
	
	__save_flags(flags);
	__cli();
	gameport_trigger(gameport);
	GET_TIME(now);
	__restore_flags(flags);

	start = now;
	this = port->mask;
	i = 0;

	do {
		loop = now;
		last = this;

		__cli();
		this = gameport_read(gameport) & port->mask;
		GET_TIME(now);
		__restore_flags(flags);

		if ((last ^ this) && (DELTA(loop, now) < port->loop)) {
			data[i] = last ^ this;
			time[i] = now;
			i++;
		}

	} while (this && (i < 4) && (DELTA(start, now) < port->timeout));

	this <<= 4;

	for (--i; i >= 0; i--) {
		this |= data[i];
		for (j = 0; j < 4; j++)
			if (data[i] & (1 << j))
				port->axes[j] = (DELTA(start, time[i]) << 8) / port->speed;
	}

	return -(this != port->mask);
}

static int analog_button_read(struct analog_port *port)
{
	port->buttons = (~gameport_read(port->gameport) >> 4) & 0xf;
	return 0;
}

/*
 * analog_timer() repeatedly polls the Logitech joysticks.
 */

static void analog_timer(unsigned long data)
{
	struct analog_port *port = (void *) data;

	if (port->cooked) {
		port->bads -= gameport_cooked_read(port->gameport, port->axes, &port->buttons);
		port->reads++;
	} else {
		if (!port->axtime--) {
			port->bads -= analog_cooked_read(port);
			port->reads++;
			port->axtime = 2;
		}
		analog_button_read(port);
	}

	analog_decode(port->analog, port->axes, port->initial, port->buttons);
	mod_timer(&port->timer, jiffies + ANALOG_REFRESH_TIME);
}

/*
 * analog_open() is a callback from the input open routine.
 */

static int analog_open(struct input_dev *dev)
{
	struct analog_port *port = dev->private;
	if (!port->used++)
		mod_timer(&port->timer, jiffies + ANALOG_REFRESH_TIME);	
	return 0;
}

/*
 * analog_close() is a callback from the input close routine.
 */

static void analog_close(struct input_dev *dev)
{
	struct analog_port *port = dev->private;
	if (!--port->used)
		del_timer(&port->timer);
}

/*
 * analog_calibrate_timer() calibrates the timer and computes loop
 * and timeout values for a joystick port.
 */

static void analog_calibrate_timer(struct analog_port *port)
{
	struct gameport *gameport = port->gameport;
	unsigned int i, t, tx, t1, t2, t3;
	unsigned long flags;

	save_flags(flags);
	cli();
	GET_TIME(t1);
#ifdef FAKE_TIME
	analog_faketime += 830;
#endif
	udelay(1000);
	GET_TIME(t2);
	GET_TIME(t3);
	restore_flags(flags);

	port->speed = DELTA(t2, t1) - DELTA(t3, t2);

	tx = 1 << 30;

	for(i = 0; i < 50; i++) {
		save_flags(flags);
		cli();
		GET_TIME(t1);
		for(t = 0; t < 50; t++) { gameport_read(gameport); GET_TIME(t2); }
		GET_TIME(t3);
		restore_flags(flags);
		udelay(i);
		if ((t = DELTA(t2,t1) - DELTA(t3,t2)) < tx) tx = t;
	}

        port->loop = (ANALOG_LOOP_TIME * t) / 50000;
	port->timeout = (ANALOG_MAX_TIME * port->speed) / 1000;
}

/*
 * analog_name() constructs a name for an analog joystick.
 */

static void analog_name(struct analog *analog)
{

	sprintf(analog->name, "Analog %d-axis %d-button",
		hweight8(analog->mask & 0x0f),
		hweight8(analog->mask & 0xf0) + (analog->mask & ANALOG_BTNS_CHF) * 2 +
		       hweight8(analog->mask & ANALOG_BTNS_GAMEPAD));

	if (analog->mask & ANALOG_HATS_ALL)
		sprintf(analog->name, "%s %d-hat",
			analog->name,
			hweight8(analog->mask & ANALOG_HATS_ALL));

	strcat(analog->name, " joystick");

	if (analog->mask & ANALOG_EXTENSIONS)
		sprintf(analog->name, "%s with%s%s%s extensions",
			analog->name,
			analog->mask & ANALOG_ANY_CHF ? " CHF" : "",
			analog->mask & ANALOG_HAT_FCS ? " FCS" : "",
			analog->mask & ANALOG_BTNS_GAMEPAD ? " GamePad" : "");
}

/*
 * analog_init_devices() sets up device-specific values and registers the input devices..
 */

static int analog_init_devices(struct analog_port *port)
{
	int i;
	struct analog *analog;

	switch (port->mask) {
		case 0x0:
			return -1;
		case 0x3:
			port->analog[0].mask = 0xf3; /* joystick 0, assuming 4-button */
			break;
		case 0x7:
			port->analog[0].mask = 0xf7; /* 3-axis, 4-button joystick */
			break;
		case 0xb:
			port->analog[0].mask = 0xfb; /* 3-axis, 4-button joystick */
			break;
		case 0xc:
			port->analog[0].mask = 0xcc; /* joystick 1 */
			break;
		case 0xf:
			port->analog[0].mask = 0xff; /* 4-axis 4-button joystick */
			break;
		default:
			printk(KERN_WARNING "joy-analog: Unknown joystick device found  "
				"(data=%#x), probably not analog joystick.\n", port->mask);
			return -1;
	}

	analog = port->analog;

	analog_name(analog);

	analog->dev.open = analog_open;
	analog->dev.close = analog_close;

	analog->dev.private = port;
	analog->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
	
	for (i = 0; i < 4; i++)
		set_bit(analog_axes[i], analog->dev.absbit);
	for (i = 0; i < 4; i++)
		set_bit(analog_buttons[i], analog->dev.keybit);

	input_register_device(&analog->dev);

	return 0;	
}

static void analog_connect(struct gameport *gameport, struct gameport_dev *dev)
{
	struct analog_port *port;
	int i;

	if (!(port = kmalloc(sizeof(struct analog_port), GFP_KERNEL)))
		return;
	memset(port, 0, sizeof(struct analog_port));

	gameport->private = port;

	port->gameport = gameport;
	init_timer(&port->timer);
	port->timer.data = (long) port;
	port->timer.function = analog_timer;

#if 0
	port->adi[0].mask = ....
	port->adi[1].mask = ....
#endif

	if (gameport_open(gameport, dev)) {
                kfree(port);
                return;
        }

	if (!gameport_set_mode(gameport, GAMEPORT_MODE_COOKED))
		port->cooked = 1;

	if (port->cooked) {
		gameport_cooked_read(gameport, port->axes, &port->buttons);
		for (i = 0; i < 4; i++)
			if (port->axes[i] != -1) port->mask |= 1 << i;
	} else {

		analog_calibrate_timer(port);

		gameport_trigger(gameport);
		port->mask = gameport_read(gameport);
		udelay(ANALOG_MAX_TIME);
		port->mask = (gameport_read(gameport) ^ port->mask) & port->mask & 0xf;

		analog_cooked_read(port);
	}

	analog_init_devices(port);

	for (i = 0; i < 2; i++) 
		if (port->analog[i].mask) {
			printk(KERN_INFO "input%d: %s at gameport%d",
				port->analog[i].dev.number, port->analog[i].name, gameport->number);
			if (port->cooked)
				printk(" [ADC port]\n");
			else
				printk(" ["TIME_NAME" timer, %d %sHz clock, %d ns res]\n",
				port->speed > 10000 ? (port->speed + 800) / 1000 : port->speed,
				port->speed > 10000 ? "M" : "k",
				port->loop * 1000000000 / ANALOG_LOOP_TIME / port->speed);
		}
}

static void analog_disconnect(struct gameport *gameport)
{
	int i;

	struct analog_port *port = gameport->private;
	for (i = 0; i < 2; i++)
		if (port->analog[i].mask)
			input_unregister_device(&port->analog[i].dev);
	gameport_close(gameport);
	kfree(port);
}

/*
 * The gameport device structure.
 */

static struct gameport_dev analog_dev = {
	connect:	analog_connect,
	disconnect:	analog_disconnect,
};

int __init analog_init(void)
{
	gameport_register_device(&analog_dev);
	return 0;
}

void __exit analog_exit(void)
{
	gameport_unregister_device(&analog_dev);
}

module_init(analog_init);
module_exit(analog_exit);
