/*
 *  inport.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Inport (ATI XL and Microsoft) Bus Mouse Driver for Linux
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

#include <asm/io.h>
#include <asm/irq.h>

#include <linux/module.h>
#include <linux/config.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/input.h>

#define INPORT_BASE		0x23c
#define INPORT_EXTENT		4

#define INPORT_CONTROL_PORT	INPORT_BASE + 0
#define INPORT_DATA_PORT	INPORT_BASE + 1
#define INPORT_SIGNATURE_PORT	INPORT_BASE + 2

#define INPORT_REG_BTNS	0x00
#define INPORT_REG_X		0x01
#define INPORT_REG_Y		0x02
#define INPORT_REG_MODE		0x07
#define INPORT_RESET		0x80

#ifdef CONFIG_INPUT_ATIXL
#define INPORT_NAME		"ATI XL"
#define INPORT_SPEED_30HZ	0x01
#define INPORT_SPEED_50HZ	0x02
#define INPORT_SPEED_100HZ	0x03
#define INPORT_SPEED_200HZ	0x04
#define INPORT_MODE_BASE	INPORT_SPEED_50HZ
#define INPORT_MODE_IRQ		0x08
#else
#define INPORT_NAME		"Microsoft"
#define INPORT_MODE_BASE	0x10
#define INPORT_MODE_IRQ		0x01
#endif
#define INPORT_MODE_HOLD	0x20

#define INPORT_IRQ		5

MODULE_PARM(inport_irq, "i");

static int inport_irq = INPORT_IRQ;
static int inport_used = 0;

static void inport_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static int inport_open(struct input_dev *dev)
{
	if (!inport_used++) {
		if (request_irq(inport_irq, inport_interrupt, 0, "inport", NULL))
			return -EBUSY;
		outb(INPORT_REG_MODE, INPORT_CONTROL_PORT);
		outb(INPORT_MODE_IRQ | INPORT_MODE_BASE, INPORT_DATA_PORT);
	}

	return 0;
}

static void inport_close(struct input_dev *dev)
{
	if (!--inport_used) {
		outb(INPORT_REG_MODE, INPORT_CONTROL_PORT);
		outb(INPORT_MODE_BASE, INPORT_DATA_PORT);
		free_irq(inport_irq, NULL);
	}
}

static struct input_dev inport_dev = {
	evbit:	{BIT(EV_KEY) | BIT(EV_REL)},
	keybit: {BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT)},
	relbit:	{BIT(REL_X) | BIT(REL_Y)},
	open:	inport_open,
	close:	inport_close
};

static void inport_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char buttons;

	outb(INPORT_REG_MODE, INPORT_CONTROL_PORT);
	outb(INPORT_MODE_HOLD | INPORT_MODE_IRQ | INPORT_MODE_BASE, INPORT_DATA_PORT);

	outb(INPORT_REG_X, INPORT_CONTROL_PORT);
	input_event(&inport_dev, EV_REL, REL_X, inb(INPORT_DATA_PORT));

	outb(INPORT_REG_Y, INPORT_CONTROL_PORT);
	input_event(&inport_dev, EV_REL, REL_Y, inb(INPORT_DATA_PORT));

	outb(INPORT_REG_BTNS, INPORT_CONTROL_PORT);
	buttons = inb(INPORT_DATA_PORT);

	input_event(&inport_dev, EV_KEY, BTN_MIDDLE,  buttons       & 1);
	input_event(&inport_dev, EV_KEY, BTN_LEFT,   (buttons >> 1) & 1);
	input_event(&inport_dev, EV_KEY, BTN_RIGHT,  (buttons >> 2) & 1);

	outb(INPORT_REG_MODE, INPORT_CONTROL_PORT);
	outb(INPORT_MODE_IRQ | INPORT_MODE_BASE, INPORT_DATA_PORT);
}

void __init inport_setup(char *str, int *ints)
{
        if (!ints[0]) inport_irq = ints[1];
}

int __init inport_init(void)
{
	unsigned char a,b,c;

	if (check_region(INPORT_BASE, INPORT_EXTENT))
		return -EBUSY;

	a = inb(INPORT_SIGNATURE_PORT);
	b = inb(INPORT_SIGNATURE_PORT);
	c = inb(INPORT_SIGNATURE_PORT);
	if (( a == b ) || ( a != c ))
		return -ENODEV;

	outb(INPORT_RESET, INPORT_CONTROL_PORT);
	outb(INPORT_REG_MODE, INPORT_CONTROL_PORT);
	outb(INPORT_MODE_BASE, INPORT_DATA_PORT);

	request_region(INPORT_BASE, INPORT_EXTENT, "inport");

	input_register_device(&inport_dev);

	printk(KERN_INFO "input%d: " INPORT_NAME " Inport mouse at %#x irq %d\n",
		inport_dev.number, INPORT_BASE, inport_irq);

	return 0;
}

void __exit inport_exit(void)
{
	input_unregister_device(&inport_dev);
	release_region(INPORT_BASE, INPORT_EXTENT);
}

module_init(inport_init);
module_exit(inport_exit);
