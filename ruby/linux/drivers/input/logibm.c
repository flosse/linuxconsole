/*
 *  logibm.c  Version 0.2
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  Logitech Bus Mouse Driver for Linux
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
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/input.h>

#define	LOGIBM_BASE		0x23c
#define	LOGIBM_EXTENT		4

#define	LOGIBM_DATA_PORT	LOGIBM_BASE + 0
#define	LOGIBM_SIGNATURE_PORT	LOGIBM_BASE + 1
#define	LOGIBM_CONTROL_PORT	LOGIBM_BASE + 2
#define	LOGIBM_CONFIG_PORT	LOGIBM_BASE + 3

#define	LOGIBM_ENABLE_IRQ	0x00
#define	LOGIBM_DISABLE_IRQ	0x10
#define	LOGIBM_READ_X_LOW	0x80
#define	LOGIBM_READ_X_HIGH	0xa0
#define	LOGIBM_READ_Y_LOW	0xc0
#define	LOGIBM_READ_Y_HIGH	0xe0

#define LOGIBM_DEFAULT_MODE	0x90
#define LOGIBM_CONFIG_BYTE	0x91
#define LOGIBM_SIGNATURE_BYTE	0xa5

#define LOGIBM_IRQ		5

MODULE_PARM(logibm_irq, "i");

static int logibm_irq = LOGIBM_IRQ;
static int logibm_used = 0;

static void logibm_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static int logibm_open(struct input_dev *dev)
{
	if (logibm_used++)
		return 0;
	if (request_irq(logibm_irq, logibm_interrupt, 0, "logibm", NULL)) {
		logibm_used--;
		printk(KERN_ERR "logibm.c: Can't allocate irq %d\n", logibm_irq);
		return -EBUSY;
	}
	outb(LOGIBM_ENABLE_IRQ, LOGIBM_CONTROL_PORT);
	return 0;
}

static void logibm_close(struct input_dev *dev)
{
	if (--logibm_used)
		return;
	outb(LOGIBM_DISABLE_IRQ, LOGIBM_CONTROL_PORT);
	free_irq(logibm_irq, NULL);
}

static struct input_dev logibm_dev = {
	evbit:	{BIT(EV_KEY) | BIT(EV_REL)},
	keybit: {BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT)},
	relbit:	{BIT(REL_X) | BIT(REL_Y)},
	open:	logibm_open,
	close:	logibm_close
};

static void logibm_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	char dx, dy;
	unsigned char buttons;

	outb(LOGIBM_READ_X_LOW, LOGIBM_CONTROL_PORT);
	dx = (inb(LOGIBM_DATA_PORT) & 0xf);
	outb(LOGIBM_READ_X_HIGH, LOGIBM_CONTROL_PORT);
	dx |= (inb(LOGIBM_DATA_PORT) & 0xf) << 4;
	outb(LOGIBM_READ_Y_LOW, LOGIBM_CONTROL_PORT);
	dy = (inb(LOGIBM_DATA_PORT) & 0xf);
	outb(LOGIBM_READ_Y_HIGH, LOGIBM_CONTROL_PORT);
	buttons = inb(LOGIBM_DATA_PORT);
	dy |= (buttons & 0xf) << 4;
	buttons = ~buttons;

	input_report_rel(&logibm_dev, REL_X, dx);
	input_report_rel(&logibm_dev, REL_Y, 255 - dy);
	input_report_btn(&logibm_dev, BTN_MIDDLE, buttons & 1);
	input_report_btn(&logibm_dev, BTN_LEFT,   buttons & 2);
	input_report_btn(&logibm_dev, BTN_RIGHT,  buttons & 4);
}

static int __init logibm_setup(char *str)
{
        int ints[4];
        str = get_options(str, ARRAY_SIZE(ints), ints);
        if (ints[0] > 0) logibm_irq = ints[1];
        return 1;
}

int __init logibm_init(void)
{
	if (request_region(LOGIBM_BASE, LOGIBM_EXTENT, "logibm")) {
		printk(KERN_ERR "logibm.c: Can't allocate ports at %#x\n", LOGIBM_BASE);
		return -EBUSY;
	}

	outb(LOGIBM_CONFIG_BYTE, LOGIBM_CONFIG_PORT);
	outb(LOGIBM_SIGNATURE_BYTE, LOGIBM_SIGNATURE_PORT);
	udelay(100);

	if (inb(LOGIBM_SIGNATURE_PORT) != LOGIBM_SIGNATURE_BYTE) {
		release_region(LOGIBM_BASE, LOGIBM_EXTENT);
		printk(KERN_ERR "logibm.c: Didn't find Logitech busmouse at %#x\n", LOGIBM_BASE);
		return -ENODEV;
	}

	outb(LOGIBM_DEFAULT_MODE, LOGIBM_CONFIG_PORT);
	outb(LOGIBM_DISABLE_IRQ, LOGIBM_CONTROL_PORT);

	input_register_device(&logibm_dev);

	printk(KERN_INFO "input%d: Logitech bus mouse at %#x irq %d\n",
		logibm_dev.number, LOGIBM_BASE, logibm_irq);

	return 0;
}

void __exit logibm_exit(void)
{
	input_unregister_device(&logibm_dev);
	release_region(LOGIBM_BASE, LOGIBM_EXTENT);
}

__setup("logibm_irq=", logibm_setup);
module_init(logibm_init);
module_exit(logibm_exit);
