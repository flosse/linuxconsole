/*
 *  iobug.c  Version 0.1
 *
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Serio communication debug module
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

#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/serio.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");

#define IOBUG_MAGIC	0x10BA6

struct iobug {
	int magic;
	struct serio *spy;
	struct serio fake;
};

static int iobug_write(struct serio *port, unsigned char data)
{
	struct iobug *iobug = port->private;
	printk("iobug.c: computer -> device %02x\n", data);
	iobug->spy->write(iobug->spy, data);
	return 0;
}

static int iobug_open(struct serio *port)
{
	struct iobug *iobug = port->private;
	MOD_INC_USE_COUNT;
	iobug->spy->open(iobug->spy);
	return 0;
}

static void iobug_close(struct serio *port)
{
	struct iobug *iobug = port->private;
	MOD_DEC_USE_COUNT;
	iobug->spy->close(iobug->spy);
}

static void iobug_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{
	struct iobug *iobug = serio->private;
	printk("iobug.c: computer <- device %02x\n", data);
	iobug->fake.dev->interrupt(&iobug->fake, data, flags);
}

static void iobug_connect(struct serio *port, struct serio_dev *dev)
{
	struct iobug *iobug = port->driver;

	if (iobug && iobug->magic == IOBUG_MAGIC)
		return;

	if (!(iobug = kmalloc(sizeof(struct iobug), GFP_KERNEL)))
                return;

	iobug->spy = port;
	iobug->magic = IOBUG_MAGIC;

	iobug->fake.type = port->type;
	iobug->fake.write = iobug_write;
	iobug->fake.open = iobug_open;
	iobug->fake.close = iobug_close;
	iobug->fake.driver = iobug;

	port->private = iobug;
	
	if (serio_open(port, dev)) {
		kfree(iobug);
		return;
	}

	serio_register_port(&iobug->fake);

	printk("serio%d: IO-Bug fake port on serio%d\n", iobug->fake.number, iobug->spy->number);
}

static void iobug_disconnect(struct serio *port)
{
	struct iobug *iobug = port->driver;
	serio_unregister_port(&iobug->fake);
	serio_close(port);
	kfree(iobug);
}

static struct serio_dev iobug_dev = {
	interrupt:	iobug_interrupt,
	connect:	iobug_connect,
	disconnect:	iobug_disconnect,
};

#ifdef MODULE
int init_module(void)
#else
int __init iobug_init(void)
#endif
{
	serio_register_device(&iobug_dev);
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	serio_unregister_device(&iobug_dev);
}
#endif
