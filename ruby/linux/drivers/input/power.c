/*
 * $Id$
 *
 *  Copyright (c) 2001 "Crazy" James Simmons  
 *
 *  Input driver Power Management.
 *
 *  Sponsored by SuSE, Transvirtual Technology.
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
 * e-mail - mail your message to <jsimmons@transvirtual.com>.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/delay.h>

static struct input_handler power_handler;

/*
 * Power management can't be done in a interrupt context. So we have to
 * use keventd.
 */
static int suspend_button_pushed = 0;
static void suspend_button_task_handler(void *data)
{
        //extern void pm_do_suspend(void);
        udelay(200); /* debounce */
        //pm_do_suspend();
        suspend_button_pushed = 0;
}

static struct tq_struct suspend_button_task = {
        routine: suspend_button_task_handler
};

static int power_event(struct input_handle *handle, unsigned int type, 
		       unsigned int code, int down)
{
	if (type != EV_KEY) return;

	switch (code) {
		case KEY_SUSPEND:
			printk("Handling power key\n");

			//pm_send_all(pm_request_t rqst, void *data);

			if (!suspend_button_pushed) {
                		suspend_button_pushed = 1;
                        	schedule_task(&suspend_button_task);
                	}
			break;
		case KEY_POWER:
			/* Hum power down the machine. */
			break;
		default:	
			return -1;
	}
	return 0;
}

static struct input_handle *power_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct input_handle *handle;
	int i;

	if (!test_bit(EV_KEY, dev->evbit))
		return NULL;

	for (i = KEY_RESERVED; i < BTN_MISC; i++)
		if (test_bit(i, dev->keybit)) break;

	if (i != KEY_SUSPEND || i != KEY_POWER)
 		return NULL;

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL)))
		return NULL;
	memset(handle, 0, sizeof(struct input_handle));

	handle->dev = dev;
	handle->handler = handler;

	input_open_device(handle);

	printk(KERN_INFO "power.c: Adding power management to input layer\n");
	return handle;
}

static void power_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	kfree(handle);
}
	
static struct input_handler power_handler = {
	event:		power_event,
	connect:	power_connect,
	disconnect:	power_disconnect,
};

static int __init power_init(void)
{
	input_register_handler(&power_handler);
	return 0;
}

static void __exit power_exit(void)
{
	input_unregister_handler(&power_handler);
}

module_init(power_init);
module_exit(power_exit);

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION("Input Power Management driver");
