/*
 *  linux/drivers/char/serial_core.c
 *
 *  Driver core for serial TTYs. 
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/pm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

/*
 * This must be before serial_core.h so that info->sysrq is
 * included in the definition of uart_info.
 */
#if defined(CONFIG_SERIAL_CORE_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

#undef	DEBUG

int register_serial_tty(name, major, minor, number, subtype, proc)
{
	struct tty_driver *driver; 
	struct uart_driver *drv;
	struct uart_port *port;

	drv->state = kmalloc(sizeof(struct uart_state) * drv->nr +
                             sizeof(int), GFP_KERNEL);
        if (!drv->state)
                return -ENOMEM;

        memset(drv->state, 0, sizeof(struct uart_state) * drv->nr +
                        sizeof(int));

	driver->magic           = TTY_DRIVER_MAGIC;
	driver->driver_name     = name;
        driver->name            = name;
        driver->major           = major;
        driver->minor_start     = minor;
	driver->num		= nr;
	driver->type            = TTY_DRIVER_TYPE_SERIAL;
        driver->subtype         = SERIAL_TYPE_NORMAL;
	driver->init_termios    = tty_std_termios;
        driver->init_termios.c_cflag = B38400 | CS8 | CREAD | HUPCL | CLOCAL;
        driver->flags           = TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	driver->refcount        = (int *)(drv->state + drv->nr);
	//driver->table           = drv->table;
        //driver->termios         = drv->termios;
        //driver->termios_locked  = drv->termios_locked;
        driver->driver_state    = drv;

        driver->open            = uart_open;
        driver->close           = uart_close;
        driver->write           = uart_write;
        driver->put_char        = uart_put_char;
        driver->flush_chars     = uart_flush_chars;
        driver->write_room      = uart_write_room;
        driver->chars_in_buffer = uart_chars_in_buffer;
        driver->flush_buffer    = uart_flush_buffer;
        driver->ioctl           = uart_ioctl;
        driver->throttle        = uart_throttle;
        driver->unthrottle      = uart_unthrottle;
        driver->send_xchar      = uart_send_xchar;
        driver->set_termios     = uart_set_termios;
        driver->stop            = uart_stop;
        driver->start           = uart_start;
        driver->hangup          = uart_hangup;
        driver->break_ctl       = uart_break_ctl;
        driver->wait_until_sent = uart_wait_until_sent;
#ifdef CONFIG_PROC_FS
        driver->read_proc       = uart_read_proc;
#endif
        /*
         * The callout device is just like the normal device except for
         * the major number and the subtype code.
         */
        callout                 = normal + 1;
        *callout                = *normal;
        callout->name           = drv->callout_name;
        callout->major          = drv->callout_major;
        callout->subtype        = SERIAL_TYPE_CALLOUT;
        callout->read_proc      = NULL;
        callout->proc_entry     = NULL;


	uart_register_port(drv, port);
	return 0;
}

void unregister_serial_tty(void)
{
}

static int __init serial_tty_init(void)
{
	struct tty_driver *normal, *callout;

	register_serial_tty(normal);
        memcpy(callout, normal, sizeof(struct tty_driver));
	callout->name           = drv->name;
        callout->major          = drv->major;
        callout->subtype        = SERIAL_TYPE_CALLOUT;
        callout->read_proc      = NULL;
        callout->proc_entry     = NULL;
	register_serial_tty(callout); 
	return 0;
}

static void __exit serial_tty_exit(void)
{
}

module_init(serial_tty_init);
module_exit(serial_tty_exit);
