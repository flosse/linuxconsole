/*
 * $Id$
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * PDPI Lightning 4 gamecard driver for Linux.
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

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/malloc.h>

#define L4_PORT			0x201
#define L4_SELECT_ANALOG	0xa4
#define L4_SELECT_DIGITAL	0xa5
#define L4_SELECT_SECONDARY	0xa6
#define L4_CMD_ID		0x80
#define L4_CMD_GETCAL		0x92
#define L4_CMD_SETCAL		0x93
#define L4_ID			0x04
#define L4_BUSY			0x01
#define L4_TIMEOUT		80	/* 80 us */
#define L4_CALTIME		HZ/20	/* 50 ms */

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

struct l4 {
	unsigned char port, rev, cal;
	int calaxes[4];
	unsigned long caltime;
	struct gameport gameport;
} *l4_port[8];

/*
 * l4_wait_ready() waits for the L4 to become ready.
 */

static int l4_wait_ready(void)
{
	unsigned int t;
	t = L4_TIMEOUT;
	while ((inb(L4_PORT) & L4_BUSY) && t > 0) t--;
	return -(t<=0);
}

/*
 * l4_cooked_read() reads data from the Lightning 4.
 */

static int l4_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
	struct l4 *l4 = gameport->driver;
	unsigned char status;
	int i, result = -1;

	outb(L4_SELECT_ANALOG, L4_PORT);
	outb(L4_SELECT_DIGITAL + (l4->port >> 2), L4_PORT);

	if (inb(L4_PORT) & L4_BUSY) goto fail;
	outb(l4->port & 3, L4_PORT);

	if (l4_wait_ready()) goto fail;
	status = inb(L4_PORT);

	for (i = 0; i < 4; i++)
		if (status & (1 << i)) {
			if (l4_wait_ready()) goto fail;
			axes[i] = inb(L4_PORT);
			if (axes[i] > 252) axes[i] = -1;
		}

	if (status & 0x10) {
		if (l4_wait_ready()) goto fail;
		*buttons = inb(L4_PORT) & 0x0f;
	}

	if (l4->cal) {
		if (time_before(jiffies, l4->caltime)) {
			for (i = 0; i < 4; i++)
				axes[i] = l4->calaxes[i];
		} else l4->cal = 0;
	}

	result = 0;

fail:	outb(L4_SELECT_ANALOG, L4_PORT);	
	return result;
}

static int l4_open(struct gameport *gameport, int mode)
{
	struct l4 *l4 = gameport->driver;
        if (l4->port != 0 && mode != GAMEPORT_MODE_COOKED)
		return -1;
	outb(L4_SELECT_ANALOG, L4_PORT);
	return 0;
}

/*
 * l4_getcal() reads the L4 with calibration values.
 */

static int l4_getcal(int port, int *cal)
{
	int i, result = -1;
	
	outb(L4_SELECT_ANALOG, L4_PORT);
	outb(L4_SELECT_DIGITAL + (port >> 2), L4_PORT);

	if (inb(L4_PORT) & L4_BUSY) goto fail;
	outb(L4_CMD_GETCAL, L4_PORT);

	if (l4_wait_ready()) goto fail;
	if (inb(L4_PORT) != L4_SELECT_DIGITAL + (port >> 2)) goto fail;

	if (l4_wait_ready()) goto fail;
        outb(port & 3, L4_PORT);

	for (i = 0; i < 4; i++) {
		if (l4_wait_ready()) goto fail;
		cal[i] = inb(L4_PORT);
	}

	result = 0;

fail:	outb(L4_SELECT_ANALOG, L4_PORT);
	return result;
}

/*
 * l4_setcal() programs the L4 with calibration values.
 */

static int l4_setcal(int port, int *cal)
{
	int i, result = -1;

	outb(L4_SELECT_ANALOG, L4_PORT);
	outb(L4_SELECT_DIGITAL + (port >> 2), L4_PORT);

	if (inb(L4_PORT) & L4_BUSY) goto fail;
	outb(L4_CMD_SETCAL, L4_PORT);

	if (l4_wait_ready()) goto fail;
	if (inb(L4_PORT) != L4_SELECT_DIGITAL + (port >> 2)) goto fail;

	if (l4_wait_ready()) goto fail;
        outb(port & 3, L4_PORT);

	for (i = 0; i < 4; i++) {
		if (l4_wait_ready()) goto fail;
		outb(cal[i], L4_PORT);
	}

	result = 0;

fail:	outb(L4_SELECT_ANALOG, L4_PORT);
	return result;
}

/*
 * l4_calibrate() calibrates the L4 for the attached device, so
 * that the device's resistance fits into the L4's 8-bit range.
 */

static void l4_calibrate(struct l4 *l4)
{
	int i;
	int cal[4] = {255,255,255,255};
	int axes[4];
	int t;

	if (l4->rev < 0x29)
		l4_getcal(l4->port, cal);
	else
		l4_setcal(l4->port, cal);

	printk(KERN_INFO "l4: initial calibration: %d %d %d %d\n", cal[0], cal[1], cal[2], cal[3]);

	l4_cooked_read(&l4->gameport, axes, &t);

	/* Fix buttons, hat, throttle & rudder */

	for (i = 0; i < 4; i++) {
		t = (axes[i] * cal[i]) / 100;
		if (t > 255) t = 255;
		if (t < 1) t = 1;
		l4->calaxes[i] = (axes[i] * cal[i]) / t;
		if (axes[i] < 0) l4->calaxes[i] = -1;
		if (l4->calaxes[i] > 252) l4->calaxes[i] = 252;
		cal[i] = t;
	}

	l4_setcal(l4->port, cal);

	printk(KERN_INFO "l4: new calibration: %d %d %d %d\n", cal[0], cal[1], cal[2], cal[3]);

	if (l4->rev < 0x29)  {
		l4->caltime = jiffies + L4_CALTIME;
		l4->cal = 1;
	}
}
	
int __init l4_init(void)
{
	int i, j, rev, cards = 0;
	struct gameport *gameport;
	struct l4 *l4;

	if (!request_region(L4_PORT, 1, "lightning"))
		return -1;

	for (i = 0; i < 2; i++) {

		outb(L4_SELECT_ANALOG, L4_PORT);
		outb(L4_SELECT_DIGITAL + i, L4_PORT);

		if (inb(L4_PORT) & L4_BUSY) continue;
		outb(L4_CMD_ID, L4_PORT);

		if (l4_wait_ready()) continue;
		if (inb(L4_PORT) != L4_SELECT_DIGITAL + i) continue;

		if (l4_wait_ready()) continue;
		if (inb(L4_PORT) != L4_ID) continue;

		if (l4_wait_ready()) continue;
		rev = inb(L4_PORT);

		if (!rev) continue;

		if (!(l4_port[i * 4] = kmalloc(sizeof(struct l4) * 4, GFP_KERNEL))) {
			printk(KERN_ERR "lightning: Out of memory allocating ports.\n");
			continue;
		}
		memset(l4_port[i * 4], 0, sizeof(struct l4) * 4);

		for (j = 0; j < 4; j++) {

			l4 = l4_port[i * 4 + j] = l4_port[i * 4] + j;

			l4->port = i * 4 + j;
			l4->rev = rev;

			gameport = &l4->gameport;
			gameport->driver = l4;
			gameport->open = l4_open;
			gameport->cooked_read = l4_cooked_read;
			gameport->type = GAMEPORT_EXT;

			if (!i && !j) {
				gameport->io = L4_PORT;
				gameport->size = 1;
			}
			
			gameport_register_port(gameport);
		}

		printk(KERN_INFO "gameport%d,%d,%d,%d: PDPI Lightning 4 %s card v%d.%d at %#x\n",
			l4_port[i * 4 + 0]->gameport.number, l4_port[i * 4 + 1]->gameport.number, 
			l4_port[i * 4 + 2]->gameport.number, l4_port[i * 4 + 3]->gameport.number, 
			i ? "secondary" : "primary", rev >> 4, rev, L4_PORT);

		cards++;
	}

	outb(L4_SELECT_ANALOG, L4_PORT);

	if (!cards) {
		release_region(L4_PORT, 1);
		return -1;
	}

	return 0;
}

void __init l4_exit(void)
{
	int i;
	int cal[4] = {59, 59, 59, 59};

	for (i = 0; i < 8; i++)
		if (l4_port[i]) {
			l4_setcal(l4_port[i]->port, cal);
			gameport_unregister_port(&l4_port[i]->gameport);
		}
	outb(L4_SELECT_ANALOG, L4_PORT);
	release_region(L4_PORT, 1);
}

module_init(l4_init);
module_exit(l4_exit);
