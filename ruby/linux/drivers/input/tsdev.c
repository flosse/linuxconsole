/*
 * $Id$
 *
 *  Copyright (c) 2001 "Crazy" james Simmons 
 *
 *  Input driver to Touchscreen device driver module.
 *
 *  Sponsored by SuSE, Transvirtual Technology
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

#define TSDEV_MINOR_BASE 	32
#define TSDEV_MINORS		32
#define TSDEV_MIX		31

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/config.h>
#include <linux/smp_lock.h>
#include <linux/random.h>

#ifndef CONFIG_INPUT_TSDEV_SCREEN_X
#define CONFIG_INPUT_TSDEV_SCREEN_X	1024
#endif
#ifndef CONFIG_INPUT_TSDEV_SCREEN_Y
#define CONFIG_INPUT_TSDEV_SCREEN_Y	768
#endif

struct tsdev {
	int exist;
	int open;
	int minor;
	wait_queue_head_t wait;
	struct tsdev_list *list;
	struct input_handle handle;
	devfs_handle_t devfs;
};

struct tsdev_list {
	struct fasync_struct *fasync;
	struct tsdev *tsdev;
	struct tsdev_list *next;
	int dx, dy, dz, oldx, oldy;
	signed char ps2[6];
	unsigned long buttons;
	unsigned char ready, buffer, bufsiz;
	unsigned char mode, imexseq, impsseq;
};

#define TSDEV_SEQ_LEN	6

static struct input_handler tsdev_handler;

static struct tsdev *tsdev_table[TSDEV_MINORS];
static struct tsdev tsdev_mix;

static int xres = CONFIG_INPUT_TSDEV_SCREEN_X;
static int yres = CONFIG_INPUT_TSDEV_SCREEN_Y;

static void tsdev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	struct tsdev *tsdevs[3] = { handle->private, &tsdev_mix, NULL };
	struct tsdev **tsdev = tsdevs;
	struct tsdev_list *list;
	int index, size;

	/* Yes it is not a mouse but it is great for the entropy pool */
	add_mouse_randomness((type << 4) ^ code ^ (code >> 4) ^ value);

	while (*tsdev) {
		list = (*tsdev)->list;
		while (list) {
			switch (type) {
				case EV_ABS:
					if (test_bit(BTN_TRIGGER, handle->dev->keybit))
						break;
					switch (code) {
						case ABS_X:	
							size = handle->dev->absmax[ABS_X] - handle->dev->absmin[ABS_X];
							list->dx += (value * xres - list->oldx) / size;
							list->oldx += list->dx * size;
							break;
						case ABS_Y:
							size = handle->dev->absmax[ABS_Y] - handle->dev->absmin[ABS_Y];
							list->dy -= (value * yres - list->oldy) / size;
							list->oldy -= list->dy * size;
							break;
					}
					break;

				case EV_REL:
					switch (code) {
						case REL_X:	list->dx += value; break;
						case REL_Y:	list->dy -= value; break;
						case REL_WHEEL:	if (list->mode) list->dz -= value; break;
					}
					break;

				case EV_KEY:
					switch (code) {
						case BTN_0:
						case BTN_TOUCH:
						case BTN_LEFT:   index = 0; break;
						case BTN_4:
						case BTN_EXTRA:  if (list->mode == 2) { index = 4; break; }
						case BTN_STYLUS:
						case BTN_1:
						case BTN_RIGHT:  index = 1; break;
						case BTN_3:
						case BTN_SIDE:   if (list->mode == 2) { index = 3; break; }
						case BTN_2:
						case BTN_STYLUS2:
						case BTN_MIDDLE: index = 2; break;	
						default: return;
					}
					switch (value) {
						case 0: clear_bit(index, &list->buttons); break;
						case 1: set_bit(index, &list->buttons); break;
						case 2: return;
					}
					break;
			}
					
			list->ready = 1;

			kill_fasync(&list->fasync, SIGIO, POLL_IN);

			list = list->next;
		}

		wake_up_interruptible(&((*tsdev)->wait));
		tsdev++;
	}
}

static int tsdev_fasync(int fd, struct file *file, int on)
{
	int retval;
	struct tsdev_list *list = file->private_data;
	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int tsdev_release(struct inode * inode, struct file * file)
{
	struct tsdev_list *list = file->private_data;
	struct tsdev_list **listptr;

	lock_kernel();
	listptr = &list->tsdev->list;
	tsdev_fasync(-1, file, 0);

	while (*listptr && (*listptr != list))
		listptr = &((*listptr)->next);
	*listptr = (*listptr)->next;

	if (!--list->tsdev->open) {
		if (list->tsdev->minor == TSDEV_MIX) {
			struct input_handle *handle = tsdev_handler.handle;
			while (handle) {
				struct tsdev *tsdev = handle->private;
				if (!tsdev->open) {
					if (tsdev->exist) {
						input_close_device(&tsdev->handle);
					} else {
						input_unregister_minor(tsdev->devfs);
						tsdev_table[tsdev->minor] = NULL;
						kfree(tsdev);
					}
				}
				handle = handle->hnext;
			}
		} else {
			if (!tsdev_mix.open) {
				if (list->tsdev->exist) {
					input_close_device(&list->tsdev->handle);
				} else {
					input_unregister_minor(list->tsdev->devfs);
					tsdev_table[list->tsdev->minor] = NULL;
					kfree(list->tsdev);
				}
			}
		}
	}
	
	kfree(list);
	unlock_kernel();

	return 0;
}

static int tsdev_open(struct inode * inode, struct file * file)
{
	struct tsdev_list *list;
	int i = MINOR(inode->i_rdev) - TSDEV_MINOR_BASE;

	if (i >= TSDEV_MINORS || !tsdev_table[i])
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct tsdev_list), GFP_KERNEL)))
		return -ENOMEM;
	memset(list, 0, sizeof(struct tsdev_list));

	list->tsdev = tsdev_table[i];
	list->next = tsdev_table[i]->list;
	tsdev_table[i]->list = list;
	file->private_data = list;

	if (!list->tsdev->open++) {
		if (list->tsdev->minor == TSDEV_MIX) {
			struct input_handle *handle = tsdev_handler.handle;
			while (handle) {
				struct tsdev *tsdev = handle->private;
				if (!tsdev->open)
					if (tsdev->exist)	
						input_open_device(handle);
				handle = handle->hnext;
			}
		} else {
			if (!tsdev_mix.open)
				if (list->tsdev->exist)	
					input_open_device(&list->tsdev->handle);
		}
	}

	return 0;
}

static ssize_t tsdev_write(struct file * file, const char * buffer, size_t count, loff_t *ppos)
{
	struct tsdev_list *list = file->private_data;
	unsigned char c;
	int i;

	for (i = 0; i < count; i++) {

		if (get_user(c, buffer + i))
			return -EFAULT;

		switch (c) {

			case 0xeb: /* Poll */
				break;

			case 0xf2: /* Get ID */
				break;

			case 0xe9: /* Get info */
				break;
		}

		list->buffer = list->bufsiz;
	}

	kill_fasync(&list->fasync, SIGIO, POLL_IN);

	wake_up_interruptible(&list->tsdev->wait);
		
	return count;
}

static ssize_t tsdev_read(struct file * file, char * buffer, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);
	struct tsdev_list *list = file->private_data;
	int retval = 0;

	if (!list->ready && !list->buffer) {

		add_wait_queue(&list->tsdev->wait, &wait);
		current->state = TASK_INTERRUPTIBLE;

		while (!list->ready) {

			if (file->f_flags & O_NONBLOCK) {
				retval = -EAGAIN;
				break;
			}
			if (signal_pending(current)) {
				retval = -ERESTARTSYS;
				break;
			}

			schedule();
		}

		current->state = TASK_RUNNING;
		remove_wait_queue(&list->tsdev->wait, &wait);
	}

	if (retval)
		return retval;

	if (count > list->buffer)
		count = list->buffer;

	if (copy_to_user(buffer, list->ps2 + list->bufsiz - list->buffer, count))
		return -EFAULT;
	
	list->buffer -= count;

	return count;	
}

/* No kernel lock - fine */
static unsigned int tsdev_poll(struct file *file, poll_table *wait)
{
	struct tsdev_list *list = file->private_data;
	poll_wait(file, &list->tsdev->wait, wait);
	if (list->ready || list->buffer)
		return POLLIN | POLLRDNORM;
	return 0;
}

struct file_operations tsdev_fops = {
	owner:		THIS_MODULE,
	read:		tsdev_read,
	write:		tsdev_write,
	poll:		tsdev_poll,
	open:		tsdev_open,
	release:	tsdev_release,
	fasync:		tsdev_fasync,
};

static struct input_handle *tsdev_connect(struct input_handler *handler, struct input_dev *dev)
{
	struct tsdev *tsdev;
	int minor = 0;

	if (!test_bit(EV_KEY, dev->evbit) ||
	   (!test_bit(BTN_LEFT, dev->keybit) && !test_bit(BTN_TOUCH, dev->keybit)))
		return NULL;

	if ((!test_bit(EV_REL, dev->evbit) || !test_bit(REL_X, dev->relbit)) &&
	    (!test_bit(EV_ABS, dev->evbit) || !test_bit(ABS_X, dev->absbit)))
		return NULL;

	for (minor = 0; minor < TSDEV_MINORS && tsdev_table[minor]; minor++);
	if (minor == TSDEV_MINORS) {
		printk(KERN_ERR "tsdev: no more free tsdev devices\n");
		return NULL;
	}

	if (!(tsdev = kmalloc(sizeof(struct tsdev), GFP_KERNEL)))
		return NULL;
	memset(tsdev, 0, sizeof(struct tsdev));
	init_waitqueue_head(&tsdev->wait);

	tsdev->exist = 1;
	tsdev->minor = minor;
	tsdev_table[minor] = tsdev;

	tsdev->handle.dev = dev;
	tsdev->handle.handler = handler;
	tsdev->handle.private = tsdev;

	tsdev->devfs = input_register_minor("ts%d", minor, TSDEV_MINOR_BASE);

	if (tsdev_mix.open)
		input_open_device(&tsdev->handle);

//	printk(KERN_INFO "ts%d: PS/2 ts device for input%d\n", minor, dev->number);

	return &tsdev->handle;
}

static void tsdev_disconnect(struct input_handle *handle)
{
	struct tsdev *tsdev = handle->private;

	tsdev->exist = 0;

	if (tsdev->open) {
		input_close_device(handle);
	} else {
		if (tsdev_mix.open)
			input_close_device(handle);
		input_unregister_minor(tsdev->devfs);
		tsdev_table[tsdev->minor] = NULL;
		kfree(tsdev);
	}
}
	
static struct input_handler tsdev_handler = {
	event:		tsdev_event,
	connect:	tsdev_connect,
	disconnect:	tsdev_disconnect,
	fops:		&tsdev_fops,
	minor:		TSDEV_MINOR_BASE,
};

static int __init tsdev_init(void)
{
	input_register_handler(&tsdev_handler);

	memset(&tsdev_mix, 0, sizeof(struct tsdev));
	init_waitqueue_head(&tsdev_mix.wait);
	tsdev_table[TSDEV_MIX] = &tsdev_mix;
	tsdev_mix.exist = 1;
	tsdev_mix.minor = TSDEV_MIX;
	tsdev_mix.devfs = input_register_minor("mice", TSDEV_MIX, TSDEV_MINOR_BASE);

	printk(KERN_INFO "mice: PS/2 ts device common for all mice\n");

	return 0;
}

static void __exit tsdev_exit(void)
{
	input_unregister_minor(tsdev_mix.devfs);
	input_unregister_handler(&tsdev_handler);
}

module_init(tsdev_init);
module_exit(tsdev_exit);

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input driver to xxPS/2 or ImPS/2 device driver");
MODULE_PARM(xres, "i");
MODULE_PARM_DESC(xres, "Horizontal screen resolution");
MODULE_PARM(yres, "i");
MODULE_PARM_DESC(yres, "Vertical screen resolution");
