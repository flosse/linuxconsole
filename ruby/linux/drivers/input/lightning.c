/*
 *  joy-lightning.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * PDPI Lightning 4 gamecards and analog joysticks connected
 * to them.
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
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>

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

static struct js_port* __initdata l4_port = NULL;

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");

struct l4 {
	int port;
};

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
 * l4_read() reads data from the Lightning 4.
 */

static int l4_read(void *xl4, int **axes, int **buttons)
{
	struct l4 *l4 = xl4;
	int i;
	unsigned char status;

	outb(L4_SELECT_ANALOG, L4_PORT);
	outb(L4_SELECT_DIGITAL + (l4->port >> 2), L4_PORT);

	if (inb(L4_PORT) & L4_BUSY) return -1;
	outb(l4->port & 3, L4_PORT);

	if (l4_wait_ready()) return -1;
	status = inb(L4_PORT);

	for (i = 0; i < 4; i++)
		if (status & (1 << i)) {
			if (l4_wait_ready()) return -1;
			l4->an.axes[i] = inb(L4_PORT);
		}

	if (status & 0x10) {
		if (l4_wait_ready()) return -1;
		l4->an.buttons = inb(L4_PORT);
	}

	js_an_decode(&l4->an, axes, buttons);

	return 0;
}

/*
 * l4_getcal() reads the L4 with calibration values.
 */

static int l4_getcal(int port, int *cal)
{
	int i;
	
	outb(L4_SELECT_ANALOG, L4_PORT);
	outb(L4_SELECT_DIGITAL + (port >> 2), L4_PORT);

	if (inb(L4_PORT) & L4_BUSY) return -1;
	outb(L4_CMD_GETCAL, L4_PORT);

	if (l4_wait_ready()) return -1;
	if (inb(L4_PORT) != L4_SELECT_DIGITAL + (port >> 2)) return -1;

	if (l4_wait_ready()) return -1;
        outb(port & 3, L4_PORT);

	for (i = 0; i < 4; i++) {
		if (l4_wait_ready()) return -1;
		cal[i] = inb(L4_PORT);
	}

	return 0;
}

/*
 * l4_setcal() programs the L4 with calibration values.
 */

static int l4_setcal(int port, int *cal)
{
	int i;

	outb(L4_SELECT_ANALOG, L4_PORT);
	outb(L4_SELECT_DIGITAL + (port >> 2), L4_PORT);

	if (inb(L4_PORT) & L4_BUSY) return -1;
	outb(L4_CMD_SETCAL, L4_PORT);

	if (l4_wait_ready()) return -1;
	if (inb(L4_PORT) != L4_SELECT_DIGITAL + (port >> 2)) return -1;

	if (l4_wait_ready()) return -1;
        outb(port & 3, L4_PORT);

	for (i = 0; i < 4; i++) {
		if (l4_wait_ready()) return -1;
		outb(cal[i], L4_PORT);
	}

	return 0;
}

/*
 * l4_calibrate() calibrates the L4 for the attached device, so
 * that the device's resistance fits into the L4's 8-bit range.
 */

static void l4_calibrate(struct l4 *l4)
{
	int i;
	int cal[4];
	int axes[4];
	int t;

	l4_getcal(l4->port, cal);

	for (i = 0; i < 4; i++)
		axes[i] = l4->an.axes[i];
	
	if ((l4->an.extensions & JS_AN_BUTTON_PXY_X) && !(l4->an.extensions & JS_AN_BUTTON_PXY_U))
		axes[2] >>= 1;							/* Pad button X */

	if ((l4->an.extensions & JS_AN_BUTTON_PXY_Y) && !(l4->an.extensions & JS_AN_BUTTON_PXY_V))
		axes[3] >>= 1;							/* Pad button Y */

	if (l4->an.extensions & JS_AN_HAT_FCS) 
		axes[3] >>= 1;							/* FCS hat */

	if (((l4->an.mask[0] & 0xb) == 0xb) || ((l4->an.mask[1] & 0xb) == 0xb))
		axes[3] = (axes[0] + axes[1]) >> 1;				/* Throttle */

	for (i = 0; i < 4; i++) {
		t = (axes[i] * cal[i]) / 100;
		if (t > 255) t = 255;
		l4->an.axes[i] = (l4->an.axes[i] * cal[i]) / t;
		cal[i] = t;
	}

	l4_setcal(l4->port, cal);
}
	
/*
 * l4_open() is a callback from the file open routine.
 */

static int l4_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * l4_close() is a callback from the file release routine.
 */

static int l4_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * l4_probe() probes for joysticks on the L4 cards.
 */

static struct js_port __init *l4_probe(unsigned char *cards, int l4port, int mask0, int mask1, struct js_port *port)
{
	struct l4 inil4;
	struct l4 *l4 = &inil4;
	int cal[4] = {255,255,255,255};
	int i, numdev;
	unsigned char u;

	if (l4port < 0) return port;
	if (!cards[(l4port >> 2)]) return port;

	memset(l4, 0, sizeof(struct l4));
	l4->port = l4port;

	if (cards[l4port >> 2] > 0x28) l4_setcal(l4->port, cal);
	if (l4_read(l4, NULL, NULL)) return port;

	for (i = u = 0; i < 4; i++) if (l4->an.axes[i] < 253) u |= 1 << i;

	if ((numdev = js_an_probe_devs(&l4->an, u, mask0, mask1, port)) <= 0)
		return port;

	port = js_register_port(port, l4, numdev, sizeof(struct l4), l4_read);

	l4 = port->l4;

	for (i = 0; i < numdev; i++)
		printk(KERN_INFO "js%d: %s on L4 port %d\n",
			js_register_device(port, i, js_an_axes(i, &l4->an), js_an_buttons(i, &l4->an),
				js_an_name(i, &l4->an), l4_open, l4_close),
			js_an_name(i, &l4->an), l4->port);

	l4_calibrate(l4);
	l4_read(l4, port->axes, port->buttons);
	js_an_init_corr(&l4->an, port->axes, port->corr, 0);

	return port;
}

/*
 * l4_card_probe() probes for presence of the L4 card(s).
 */

static void __init l4_card_probe(unsigned char *cards)
{
	int i;
	unsigned char rev = 0;

	if (check_region(L4_PORT, 1)) return;

	for (i = 0; i < 2; i++) {

		outb(L4_SELECT_ANALOG, L4_PORT);
		outb(L4_SELECT_DIGITAL + i, L4_PORT);		/* Select card 0-1 */

		if (inb(L4_PORT) & L4_BUSY) continue;
		outb(L4_CMD_ID, L4_PORT);				/* Get card ID & rev */

		if (l4_wait_ready()) continue;
		if (inb(L4_PORT) != L4_SELECT_DIGITAL + i) continue;

		if (l4_wait_ready()) continue;
		if (inb(L4_PORT) != L4_ID) continue;

		if (l4_wait_ready()) continue;
		rev = inb(L4_PORT);

		cards[i] = rev; 

		printk(KERN_INFO "js: PDPI Lightning 4 %s card (ports %d-%d) firmware v%d.%d at %#x\n",
			i ? "secondary" : "primary", (i << 2), (i << 2) + 3, rev >> 4, rev & 0xf, L4_PORT);
	}

}

#ifndef MODULE
int __init l4_setup(SETUP_PARAM)
{
	int i;
	SETUP_PARSE(24);
	for (i = 0; i <= ints[0] && i < 24; i++) l4[i] = ints[i+1];
	return 1;
}
__setup("l4=", l4_setup);
#endif

#ifdef MODULE
int init_module(void)
#else
int __init l4_init(void)
#endif
{
	int i;
	unsigned char cards[2] = {0, 0};

	l4_card_probe(cards);

	if (l4[0] >= 0) {
		for (i = 0; (l4[i*3] >= 0) && i < 8; i++)
			l4_port = l4_probe(cards, l4[i*3], l4[i*3+1], l4[i*3+2], l4_port);
	} else {
		for (i = 0; i < 8; i++)
			l4_port = l4_probe(cards, i, 0, 0, l4_port);
	}

	if (!l4_port) {
#ifdef MODULE
		printk(KERN_WARNING "joy-lightning: no joysticks found\n");
#endif
		return -ENODEV;
	}

	request_region(L4_PORT, 1, "joystick (lightning)");

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	int cal[4] = {59, 59, 59, 59};
	struct l4 *l4;

	while (l4_port) {
		for (i = 0; i < l4_port->ndevs; i++)
			if (l4_port->devs[i])
				js_unregister_device(l4_port->devs[i]);
		l4 = l4_port->l4;
		l4_setcal(l4->port, cal);
		l4_port = js_unregister_port(l4_port);
	}
	outb(L4_SELECT_ANALOG, L4_PORT);
	release_region(L4_PORT, 1);
}
#endif
