/*
 * $Id$
 *
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 * 
 *  Sponsored by SuSE
 */

/*
 *  Input driver event debug module - dumps all events into syslog
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>

static void evbug_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	printk(KERN_DEBUG "evbug.c: Event. Dev: input%d, Type: %d, Code: %d, Value: %d\n", handle->dev->number, type, code, value);
}

static struct input_handle *evbug_connect(struct input_handler *handler, struct input_dev *dev, struct input_device_id *id)
{
	struct input_handle *handle;

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL)))
		return NULL;
	memset(handle, 0, sizeof(struct input_handle));

	handle->dev = dev;
	handle->handler = handler;

	input_open_device(handle);

	printk(KERN_DEBUG "evbug.c: Connected device: input%d\n", dev->number);

	return handle;
}

static void evbug_disconnect(struct input_handle *handle)
{
	printk(KERN_DEBUG "evbug.c: Disconnected device: input%d\n", handle->dev->number);
	
	input_close_device(handle);

	kfree(handle);
}

static struct input_device_id evbug_ids[] = {
	{ driver_info: 1 },	/* Matches all devices */
	{ },			/* Terminating zero entry */
};

MODULE_DEVICE_TABLE(input, evbug_ids);
	
static struct input_handler evbug_handler = {
	event:		evbug_event,
	connect:	evbug_connect,
	disconnect:	evbug_disconnect,
	name:		"evbug",
	id_table:	evbug_ids,
};

int __init evbug_init(void)
{
	input_register_handler(&evbug_handler);
	return 0;
}

void __exit evbug_exit(void)
{
	input_unregister_handler(&evbug_handler);
}

module_init(evbug_init);
module_exit(evbug_exit);
