/*
 * $Id$
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module that converts a tty line into a much simpler
 * 'serial io port' abstraction that the input device drivers use.
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
 *  Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/tty.h>
#include <linux/circ_buf.h>

static DECLARE_MUTEX(port_sem);

/*
 * Callback functions from the serio code.
 */

static int serport_serio_write(struct serio *serio, unsigned char data)
{
	struct uart_info *info = serio->driver;
	unsigned long flags;
	int retval = -1;

	if (!info->xmit.buf)
                return retval;

	save_flags(flags); cli();
        if (CIRC_SPACE(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE) != 0) {                info->xmit.buf[info->xmit.head] = ch;
                info->xmit.head = (info->xmit.head + 1) & (UART_XMIT_SIZE - 1);
		retval = 0;
        }
        restore_flags(flags);
	return retval;
}

static int serport_serio_open(struct serio *serio)
{
	struct uart_info *info = serio->driver;
	int retval = -ENODEV;

	/*        
	if (!try_inc_mod_count(drv->owner))
                goto fail;
	*/

	if (!info)
                goto out;
	/*
         * If the port is in the middle of closing, bail out now.
         */
	if (info->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
                retval = (info->flags & ASYNC_HUP_NOTIFY) ?
                        -EAGAIN : -ERESTARTSYS;
                goto out;
	}
	
	/*
         * Make sure the device is in D0 state.
         */
        if (info->state->count == 1)
#ifdef CONFIG_PM
                pm_send(info->state->pm, PM_RESUME, (void *)0);
#else
                if (info->ops->pm)
                        info->ops->pm(info->port, 0, 3);
#endif

        /*
         * Start up the serial port
         */
        retval = uart_startup(info);
        if (retval)
                goto out;
	uart_change_speed(info, NULL);
out:
        if (drv->owner)
                __MOD_DEC_USE_COUNT(drv->owner);
fail:
	return retval;
}

static void serport_serio_close(struct serio *serio)
{
	struct uart_info *info = serio->private;
	struct uart_state *state = info->state;

	down(&state->count_sem);
        save_flags(flags); cli();

	if (state->count) {
                restore_flags(flags);
                up(&state->count_sem);
                goto done;
        }
        info->flags |= ASYNC_CLOSING;
        restore_flags(flags);
        up(&state->count_sem);
	
        /*
         * At this point, we stop accepting input.  To do this, we
         * disable the receive line status interrupts.
         */
        if (info->flags & ASYNC_INITIALIZED) {
                info->ops->stop_rx(info->port);
                /*
                 * Before we drop DTR, make sure the UART transmitter
                 * has completely drained; this is especially
                 * important if there is a transmit FIFO!
                 */
                uart_wait_until_sent(tty, info->timeout);
        }
	uart_shutdown(info);
	info->event = 0;       
	
	if (info->blocked_open) {
                if (info->state->close_delay) {
                        set_current_state(TASK_INTERRUPTIBLE);
                        schedule_timeout(info->state->close_delay);
                        set_current_state(TASK_RUNNING);
                }
                wake_up_interruptible(&info->open_wait);
        } else {
#ifdef CONFIG_PM
                /*
                 * Put device into D3 state.
                 */
                pm_send(info->state->pm, PM_SUSPEND, (void *)3);
#else
                if (info->ops->pm)
                        info->ops->pm(info->port, 3, 0);
#endif
        }

        info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
                         ASYNC_CLOSING);
        wake_up_interruptible(&info->close_wait);
done:
        if (drv->owner)
                __MOD_DEC_USE_COUNT(drv->owner);
}

/*
 * The functions for insering/removing us as a module.
 */

int __init serport_init(void)
{
	struct uart_driver *input;

	uart_register_driver(&input)	
	return  0;
}

void __exit serport_exit(void)
{
	uart_unregister_driver(&input);
}

module_init(serport_init);
module_exit(serport_exit);
