/*
 * $Id$
 *
 *  Copyright (c) 1999-2000 Vojtech Pavlik
 *
 *  The input layer module itself
 *
 *  Sponsored by SuSE
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

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/pm.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input layer module");

EXPORT_SYMBOL(input_register_device);
EXPORT_SYMBOL(input_unregister_device);
EXPORT_SYMBOL(input_register_handler);
EXPORT_SYMBOL(input_unregister_handler);
EXPORT_SYMBOL(input_register_minor);
EXPORT_SYMBOL(input_unregister_minor);
EXPORT_SYMBOL(input_open_device);
EXPORT_SYMBOL(input_close_device);
EXPORT_SYMBOL(input_accept_device);
EXPORT_SYMBOL(input_flush_device);
EXPORT_SYMBOL(input_event);

#define INPUT_MAJOR	13
#define INPUT_DEVICES	256

static struct input_dev *input_dev;
static struct input_handler *input_handler;
static struct input_handler *input_table[8];
static devfs_handle_t input_devfs_handle;
static int input_number;
static long input_devices[NBITS(INPUT_DEVICES)];

void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct input_handle *handle = dev->handle;

/*
 * Wake up the device if it is sleeping.
 */
	if (dev->pm_dev)
		pm_access(dev->pm_dev);

/*
 * Filter non-events, and bad input values out.
 */

	if (type > EV_MAX || !test_bit(type, dev->evbit))
		return;

	add_mouse_randomness((type << 4) ^ code ^ (code >> 4) ^ value);

	switch (type) {

		case EV_KEY:

			if (code > KEY_MAX || !test_bit(code, dev->keybit) || !!test_bit(code, dev->key) == value)
				return;

			if (value == 2) break;

			change_bit(code, dev->key);

			if (test_bit(EV_REP, dev->evbit) && dev->timer.function) {
				if (value) {
					mod_timer(&dev->timer, jiffies + dev->rep[REP_DELAY]);
					dev->repeat_key = code;
					break;
				}
				if (dev->repeat_key == code)
					del_timer(&dev->timer);
			}

			break;
		
		case EV_ABS:

			if (code > ABS_MAX || !test_bit(code, dev->absbit))
				return;

			if (dev->absfuzz[code]) {
				if ((value > dev->abs[code] - (dev->absfuzz[code] >> 1)) &&
				    (value < dev->abs[code] + (dev->absfuzz[code] >> 1)))
					return;

				if ((value > dev->abs[code] - dev->absfuzz[code]) &&
				    (value < dev->abs[code] + dev->absfuzz[code]))
					value = (dev->abs[code] * 3 + value) >> 2;

				if ((value > dev->abs[code] - (dev->absfuzz[code] << 1)) &&
				    (value < dev->abs[code] + (dev->absfuzz[code] << 1)))
					value = (dev->abs[code] + value) >> 1;
			}

			if (dev->abs[code] == value)
				return;

			dev->abs[code] = value;
			break;

		case EV_REL:

			if (code > REL_MAX || !test_bit(code, dev->relbit) || (value == 0))
				return;

			break;

		case EV_MSC:

			if (code > MSC_MAX || !test_bit(code, dev->mscbit))
				return;
	
			break;

		case EV_LED:
	
			if (code > LED_MAX || !test_bit(code, dev->ledbit) || !!test_bit(code, dev->led) == value)
				return;

			change_bit(code, dev->led);
			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_SND:
	
			if (code > SND_MAX || !test_bit(code, dev->sndbit) || !!test_bit(code, dev->snd) == value)
				return;

			change_bit(code, dev->snd);
			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_REP:

			if (code > REP_MAX || dev->rep[code] == value) return;

			dev->rep[code] = value;
			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_FF:
			if (dev->event) dev->event(dev, type, code, value);
			break;
	}

/*
 * Distribute the event to handler modules.
 */

	while (handle) {
		if (handle->open)
			handle->handler->event(handle, type, code, value);
		handle = handle->dnext;
	}
}

static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;
	input_event(dev, EV_KEY, dev->repeat_key, 2);
	mod_timer(&dev->timer, jiffies + dev->rep[REP_PERIOD]);
}

int input_accept_device(struct input_handle *handle, struct file *file)
{
	if (handle->dev->accept)
		return handle->dev->accept(handle->dev, file);

	return 0;
}

int input_open_device(struct input_handle *handle)
{
	if (handle->dev->pm_dev)
		pm_access(handle->dev->pm_dev);
	handle->open++;
	if (handle->dev->open)
		return handle->dev->open(handle->dev);
	return 0;
}

int input_flush_device(struct input_handle* handle, struct file* file)
{
	if (handle->dev->flush)
		return handle->dev->flush(handle->dev, file);

	return 0;
}

void input_close_device(struct input_handle *handle)
{
	if (handle->dev->pm_dev)
		pm_dev_idle(handle->dev->pm_dev);
	if (handle->dev->close)
		handle->dev->close(handle->dev);
	handle->open--;
}

static void input_link_handle(struct input_handle *handle)
{
	handle->dnext = handle->dev->handle;
	handle->hnext = handle->handler->handle;
	handle->dev->handle = handle;
	handle->handler->handle = handle;
}

/**
 *     input_find_and_remove - Find and remove node
 *
 *     @type:          data type
 *     @initval:       initial value
 *     @targ:          node to find
 *     @next:          next node in the list
 *
 *     Searches the linked list for the target node @targ. If the node
 *     is found, it is removed from the list.
 *
 *     If the node is not found, the end of the list will be hit,
 *     indicating that it wasn't in the list to begin with.
 *
 *     Returns nothing.
 */
#define input_find_and_remove(type, initval, targ, next) { \
       type **ptr;                                        \
       for (ptr = &initval; *ptr; ptr = &((*ptr)->next))  \
               if (*ptr == targ) break;                   \
       if (*ptr && (*ptr)->next) *ptr = (*ptr)->next;   }

static void input_unlink_handle(struct input_handle *handle)
{
	input_find_and_remove(struct input_handle, handle->dev->handle, handle,
                              	dnext);
        input_find_and_remove(struct input_handle, handle->handler->handle,
				handle, hnext);
}

void input_register_device(struct input_dev *dev)
{
	struct input_handler *handler = input_handler;
	struct input_handle *handle;

/*
 * Initialize repeat timer to default values.
 */

	init_timer(&dev->timer);
	dev->timer.data = (long) dev;
	dev->timer.function = input_repeat_key;
	dev->rep[REP_DELAY] = HZ/4;
	dev->rep[REP_PERIOD] = HZ/33;

/*
 * Add the device.
 */

	if (input_number >= INPUT_DEVICES) {
		printk(KERN_WARNING "input: ran out of input device numbers!\n");
		dev->number = input_number;
	} else {
		dev->number = find_first_zero_bit(input_devices, INPUT_DEVICES);
		set_bit(dev->number, input_devices);
	}
		
	dev->next = input_dev;	
	input_dev = dev;
	input_number++;

/*
 * Notify handlers.
 */

	while (handler) {
		if ((handle = handler->connect(handler, dev)))
			input_link_handle(handle);
		handler = handler->next;
	}
}

void input_unregister_device(struct input_dev *dev)
{
	struct input_handle *handle = dev->handle;
	struct input_handle *dnext;

	if (!dev) return;

/*
 * Turn off power management for the device.
 */
	if (dev->pm_dev)
		pm_unregister(dev->pm_dev);

/*
 * Kill any pending repeat timers.
 */

	del_timer(&dev->timer);

/*
 * Notify handlers.
 */

	while (handle) {
		dnext = handle->dnext;
		input_unlink_handle(handle);
		handle->handler->disconnect(handle);
		handle = dnext;
	}

/*
 * Remove the device.
 */
	input_find_and_remove(struct input_dev, input_dev, dev, next);
	input_number--;

	if (dev->number < INPUT_DEVICES)
		clear_bit(dev->number, input_devices);
}

void input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev = input_dev;
	struct input_handle *handle;

	if (!handler) return;

/*
 * Add minors if needed.
 */

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = handler;

/*
 * Add the handler.
 */

	handler->next = input_handler;	
	input_handler = handler;
	
/*
 * Notify it about all existing devices.
 */

	while (dev) {
		if ((handle = handler->connect(handler, dev)))
			input_link_handle(handle);
		dev = dev->next;
	}
}

void input_unregister_handler(struct input_handler *handler)
{
	struct input_handle *handle = handler->handle;
	struct input_handle *hnext;

/*
 * Tell the handler to disconnect from all devices it keeps open.
 */

	while (handle) {
		hnext = handle->hnext;
		input_unlink_handle(handle);
		handler->disconnect(handle);
		handle = hnext;
	}

/*
 * Remove it.
 */
	input_find_and_remove(struct input_handler, input_handler, handler,
				next);

/*
 * Remove minors.
 */

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = NULL;
}

static int input_open_file(struct inode *inode, struct file *file)
{
	struct input_handler *handler = input_table[MINOR(inode->i_rdev) >> 5];
	struct file_operations *old_fops, *new_fops = NULL;
	int err;

	/* No load-on-demand here? */
	if (!handler || !(new_fops = fops_get(handler->fops)))
		return -ENODEV;

	/*
	 * That's _really_ odd. Usually NULL ->open means "nothing special",
	 * not "no device". Oh, well...
	 */
	if (!new_fops->open) {
		fops_put(new_fops);
		return -ENODEV;
	}
	old_fops = file->f_op;
	file->f_op = new_fops;

	lock_kernel();
	err = new_fops->open(inode, file);
	unlock_kernel();

	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

static struct file_operations input_fops = {
	owner: THIS_MODULE,
	open: input_open_file,
};

devfs_handle_t input_register_minor(char *name, int minor, int minor_base)
{
	char devfs_name[16];
	sprintf(devfs_name, name, minor);
	return devfs_register(input_devfs_handle, devfs_name, DEVFS_FL_DEFAULT, INPUT_MAJOR, minor + minor_base,
		S_IFCHR | S_IRUGO | S_IWUSR, &input_fops, NULL);
}

void input_unregister_minor(devfs_handle_t handle)
{
	devfs_unregister(handle);
}

static int __init input_init(void)
{
	if (devfs_register_chrdev(INPUT_MAJOR, "input", &input_fops)) {
		printk(KERN_ERR "input: unable to register char major %d", INPUT_MAJOR);
		return -EBUSY;
	}
	input_devfs_handle = devfs_mk_dir(NULL, "input", NULL);
	return 0;
}

static void __exit input_exit(void)
{
	devfs_unregister(input_devfs_handle);
        if (devfs_unregister_chrdev(INPUT_MAJOR, "input"))
                printk(KERN_ERR "input: can't unregister char major %d", INPUT_MAJOR);
}

module_init(input_init);
module_exit(input_exit);
