/*
 *  a3d.c  Version 1.2
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * FP-Gaming Assasin 3D joystick driver for Linux
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

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>

#define A3D_MAX_START		400	/* 400 us */ 
#define A3D_MAX_STROBE		40	/* 40 us */ 
#define A3D_DELAY_READ		3	/* 3 ms */
#define A3D_MAX_LENGTH		40	/* 40*3 bits */
#define A3D_REFRESH_TIME	HZ/50	/* 20 ms */

#define A3D_MODE_A3D		1	/* Assassin 3D */
#define A3D_MODE_PAN		2	/* Panther */
#define A3D_MODE_OEM		3	/* Panther OEM version */
#define A3D_MODE_PXL		4	/* Panther XL */
#define A3D_MODE_PXLR		5	/* Panther XL w/ rudder */

char *a3d_names[] = { "FP-Gaming Assassin 3D", "MadCatz Panther", "OEM Panther",
			"MadCatz Panther XL", "MadCatz Panther XL w/ rudder" };

struct a3d {
	struct gameport *gameport;
	struct gameport adc;
	struct input_dev dev;
	struct timer_list timer;
	int axes[4];
	int buttons;
	int mode;
	int length;
	int used;
	int reads;
	int bads;
};

/*
 * a3d_read_packet() reads an Assassin 3D packet.
 */

static int a3d_read_packet(struct gameport *gameport, int length, char *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t, s;
	int i;

	i = 0;
	t = gameport_time(gameport, A3D_MAX_START);
	s = gameport_time(gameport, A3D_MAX_STROBE);

	__save_flags(flags);
	__cli();
	gameport_trigger(gameport);
	v = gameport_read(gameport);

	while (t > 0 && i < length) {
		t--;
		u = v; v = gameport_read(gameport);
		if (~v & u & 0x10) {
			data[i++] = v >> 5;
			t = s;
		}
	}

	__restore_flags(flags);

	return i;
}

/*
 * a3d_csum() computes checksum of triplet packet
 */

static int a3d_csum(char *data, int count)
{
	int i, csum = 0;
	for (i = 0; i < count - 2; i++) csum += data[i];
	return (csum & 0x3f) != ((data[count - 2] << 3) | data[count - 1]);
}

/*
 * a3d_timer() reads and analyzes A3D joystick data.
 */

static void a3d_timer(unsigned long private)
{
	struct a3d *a3d = (void *) private;
	struct input_dev *dev = &a3d->dev;
	char data[A3D_MAX_LENGTH];

	a3d->reads++;
	if (a3d_read_packet(a3d->gameport, a3d->length, data) != a3d->length
	    || data[0] != a3d->mode || a3d_csum(data, a3d->length)) { 
		a3d->bads++;
	} else

	switch (a3d->mode) {

		case A3D_MODE_A3D:
		case A3D_MODE_OEM:
		case A3D_MODE_PAN:

			input_report_rel(dev, REL_X, ((data[5] << 6) | (data[6] << 3) | data[ 7]) - ((data[5] & 4) << 7));
			input_report_rel(dev, REL_Y, ((data[8] << 6) | (data[9] << 3) | data[10]) - ((data[8] & 4) << 7));
			
			input_report_key(dev, BTN_RIGHT,  data[2] & 1);
			input_report_key(dev, BTN_LEFT,   data[3] & 2);
			input_report_key(dev, BTN_MIDDLE, data[3] & 4);

			a3d->axes[0] = ((char)((data[11] << 6) | (data[12] << 3) | (data[13]))) + 128;
			a3d->axes[1] = ((char)((data[14] << 6) | (data[15] << 3) | (data[16]))) + 128;
			a3d->axes[2] = ((char)((data[17] << 6) | (data[18] << 3) | (data[19]))) + 128;
			a3d->axes[3] = ((char)((data[20] << 6) | (data[21] << 3) | (data[22]))) + 128;

			a3d->buttons = ((data[3] << 3) | data[4]) & 0xf;

			break;

		case A3D_MODE_PXLR:
		case A3D_MODE_PXL:

			input_report_rel(dev, REL_X, ((data[ 9] << 6) | (data[10] << 3) | data[11]) - ((data[ 9] & 4) << 7));
			input_report_rel(dev, REL_Y, ((data[12] << 6) | (data[13] << 3) | data[14]) - ((data[12] & 4) << 7));

			input_report_key(dev, BTN_RIGHT,  data[2] & 1);
			input_report_key(dev, BTN_LEFT,   data[3] & 2);
			input_report_key(dev, BTN_MIDDLE, data[3] & 4);
			input_report_key(dev, BTN_SIDE,   data[7] & 2);
			input_report_key(dev, BTN_EXTRA,  data[7] & 4);

			input_report_abs(dev, ABS_X,        ((char)((data[15] << 6) | (data[16] << 3) | (data[17]))) + 128);
			input_report_abs(dev, ABS_Y,        ((char)((data[18] << 6) | (data[19] << 3) | (data[20]))) + 128);
			input_report_abs(dev, ABS_RUDDER,   ((char)((data[21] << 6) | (data[22] << 3) | (data[23]))) + 128);
			input_report_abs(dev, ABS_THROTTLE, ((char)((data[24] << 6) | (data[25] << 3) | (data[26]))) + 128);

			input_report_abs(dev, ABS_HAT0X, ( data[5]       & 1) - ((data[5] >> 2) & 1));
			input_report_abs(dev, ABS_HAT0Y, ((data[5] >> 1) & 1) - ((data[6] >> 2) & 1));
			input_report_abs(dev, ABS_HAT1X, ((data[4] >> 1) & 1) - ( data[3]       & 1));
			input_report_abs(dev, ABS_HAT1Y, ((data[4] >> 2) & 1) - ( data[4]       & 1));

			input_report_key(dev, BTN_TRIGGER, data[8] & 1);
			input_report_key(dev, BTN_THUMB,   data[8] & 2);
			input_report_key(dev, BTN_TOP,     data[8] & 4);
			input_report_key(dev, BTN_PINKIE,  data[7] & 1);

			break;
	}

	mod_timer(&a3d->timer, jiffies + A3D_REFRESH_TIME);
}

/*
 * a3d_adc_cooked_read() copies the acis and button data to the
 * callers arrays. It could do the read itself, but the caller could
 * call this more than 50 times a second, which would use too much CPU.
 */

int a3d_adc_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	struct a3d *a3d = gameport->private;
	int i;
	for (i = 0; i < 4; i++)
		axes[i] = a3d->axes[i] < 254 ? a3d->axes[i] : -1;
	*buttons = a3d->buttons; 
	return 0;
}

/*
 * a3d_adc_open() is the gameport open routine. It refuses to serve
 * any but cooked data.
 */

int a3d_adc_open(struct gameport *gameport, int mode)
{
	struct a3d *a3d = gameport->private;
	if (mode != GAMEPORT_MODE_COOKED)
		return -1;
	if (!a3d->used++)
		mod_timer(&a3d->timer, jiffies + A3D_REFRESH_TIME);	
	return 0;
}

/*
 * a3d_adc_close() is a callback from the input close routine.
 */

static void a3d_adc_close(struct gameport *gameport)
{
	struct a3d *a3d = gameport->private;
	if (!--a3d->used)
		del_timer(&a3d->timer);
}

/*
 * a3d_open() is a callback from the input open routine.
 */

static int a3d_open(struct input_dev *dev)
{
	struct a3d *a3d = dev->private;
	if (!a3d->used++)
		mod_timer(&a3d->timer, jiffies + A3D_REFRESH_TIME);	
	return 0;
}

/*
 * a3d_close() is a callback from the input close routine.
 */

static void a3d_close(struct input_dev *dev)
{
	struct a3d *a3d = dev->private;
	if (!--a3d->used)
		del_timer(&a3d->timer);
}

/*
 * a3d_connect() probes for A3D joysticks.
 */

static void a3d_connect(struct gameport *gameport, struct gameport_dev *dev)
{
	struct a3d *a3d;
	char data[A3D_MAX_LENGTH];
	int i;

	if (!(a3d = kmalloc(sizeof(struct a3d), GFP_KERNEL)))
		return;
	memset(a3d, 0, sizeof(struct a3d));

	gameport->private = a3d;

	a3d->gameport = gameport;
	init_timer(&a3d->timer);
	a3d->timer.data = (long) a3d;
	a3d->timer.function = a3d_timer;

	if (gameport_open(gameport, dev, GAMEPORT_MODE_RAW))
		goto fail1;

	i = a3d_read_packet(gameport, A3D_MAX_LENGTH, data);

	if (!i || a3d_csum(data, i))
		goto fail2;

	a3d->mode = data[0];

	if (!a3d->mode || a3d->mode >= 4) {
		printk(KERN_WARNING "a3d.c: Unknown A3D device detected "
			"(gameport%d, id=%d), contact <vojtech@suse.cz>\n", gameport->number, a3d->mode);
		goto fail2;
	}
	
	if (a3d->mode == A3D_MODE_PXL) {
		if ((char)((data[21] << 6) | (data[22] << 3) | (data[23])) < 126)
			a3d->mode = A3D_MODE_PXLR;
		a3d->length = 33;

		/* Fill the input structures */

		a3d->dev.open = a3d_open;
		a3d->dev.close = a3d_close;

	} else {
		a3d->length = 29;

		/* Fill the input & gameport structures */

		a3d->adc.open = a3d_adc_open;
		a3d->adc.close = a3d_adc_close;
		a3d->adc.cooked_read = a3d_adc_cooked_read;
	
		a3d->dev.open = a3d_open;
		a3d->dev.close = a3d_close;

		gameport_register_port(&a3d->adc);
		printk(KERN_INFO "gameport%d: %s ADC gameport on gameport%d\n",
			a3d->adc.number, a3d_names[a3d->mode], gameport->number);
	}

	input_register_device(&a3d->dev);
	printk(KERN_INFO "input%d: %s on gameport%d\n",
		a3d->dev.number, a3d_names[a3d->mode], gameport->number);

	return;
fail2:	gameport_close(gameport);
fail1:  kfree(a3d);
}

static void a3d_disconnect(struct gameport *gameport)
{

	struct a3d *a3d = gameport->private;
	input_unregister_device(&a3d->dev);
	if (a3d->mode < A3D_MODE_PXL)
		gameport_unregister_port(&a3d->adc);
	gameport_close(gameport);
	kfree(a3d);
}

static struct gameport_dev a3d_dev = {
	connect:	a3d_connect,
	disconnect:	a3d_disconnect,
};

int __init a3d_init(void)
{
	gameport_register_device(&a3d_dev);
	return 0;
}

void __exit a3d_exit(void)
{
	gameport_unregister_device(&a3d_dev);
}

module_init(a3d_init);
module_exit(a3d_exit);
