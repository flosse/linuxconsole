/*
 *  linux/drivers/char/serial_core.c
 *
 *  Driver core for serial ports
 *
 *  Copyright 2001 James Simmons (jsimmons@transvirtual.com)	
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
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/pm.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <linux/serial_core.h>

#ifndef CONFIG_PM
#define pm_access(pm) do { } while (0)
#endif

static int uart_startup(struct uart_info *info)
{
        unsigned long flags;
        unsigned long page;
        int retval = 0;

        page = get_zeroed_page(GFP_KERNEL);
        if (!page)
                return -ENOMEM;

        save_flags(flags); cli();

        if (info->flags & ASYNC_INITIALIZED) {
                free_page(page);
                goto errout;
        }

        if (info->xmit.buf)
                free_page(page);
        else
                info->xmit.buf = (unsigned char *) page;

        info->mctrl = 0;

        if (info->ops->startup(info->port, info)) {
                if (capable(CAP_SYS_ADMIN)) {
                        if (info->tty)
                                set_bit(TTY_IO_ERROR, &info->tty->flags);
                        retval = 0;
                }
                goto errout;
        }

        if (info->tty)
                clear_bit(TTY_IO_ERROR, &info->tty->flags);
        info->xmit.head = info->xmit.tail = 0;

        /*
         * Set up the tty->alt_speed kludge
         */
        if (info->tty) {
                if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
                        info->tty->alt_speed = 57600;
                if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
                        info->tty->alt_speed = 115200;
                if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
                        info->tty->alt_speed = 230400;
                if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
                        info->tty->alt_speed = 460800;
        }

        /*
         * and set the speed of the serial port
         */
        uart_change_speed(info, NULL);

        /*
         * Setup the RTS and DTR signals once the port
         * is open and ready to respond.
         */
        if (info->tty->termios->c_cflag & CBAUD)
                info->mctrl = TIOCM_RTS | TIOCM_DTR;
        info->ops->set_mctrl(info->port, info->mctrl);

        info->flags |= ASYNC_INITIALIZED;
        retval = 0;

errout:
        restore_flags(flags);
        return retval;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void uart_shutdown(struct uart_info *info)
{
        unsigned long flags;

        if (!(info->flags & ASYNC_INITIALIZED))
                return;

        save_flags(flags); cli(); /* Disable interrupts */

        /*
         * clear delta_msr_wait queue to avoid mem leaks: we may free the irq
         * here so the queue might never be woken up
         */
        wake_up_interruptible(&info->delta_msr_wait);

        /*
         * Free the IRQ and disable the port
         */
        info->ops->shutdown(info->port, info);

        if (info->xmit.buf) {
                unsigned long pg = (unsigned long) info->xmit.buf;
                info->xmit.buf = NULL;
                free_page(pg);
        }

        if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
                info->mctrl &= ~(TIOCM_DTR|TIOCM_RTS);
        info->ops->set_mctrl(info->port, info->mctrl);

        /* kill off our tasklet */
        tasklet_kill(&info->tlet);
        if (info->tty)
                set_bit(TTY_IO_ERROR, &info->tty->flags);

        info->flags &= ~ASYNC_INITIALIZED;
        restore_flags(flags);
}

static void uart_change_speed(struct uart_info *info, struct termios *old_termios)
{
        struct uart_port *port = info->port;
        u_int quot, baud, cflag, bits, try;

        if (!info->tty || !info->tty->termios)
                return;

        cflag = info->tty->termios->c_cflag;

        /* byte size and parity */
        switch (cflag & CSIZE) {
        case CS5: bits = 7;  break;
        case CS6: bits = 8;  break;
        case CS7: bits = 9;  break;
        default:  bits = 10; break; // CS8
        }

        if (cflag & CSTOPB)
                bits++;
        if (cflag & PARENB)
                bits++;

        for (try = 0; try < 2; try ++) {
                /* Determine divisor based on baud rate */
                baud = tty_get_baud_rate(info->tty);
                quot = uart_calculate_quot(info, baud);
                if (quot)
                        break;

                /*
                 * Oops, the quotient was zero.  Try again with
                 * the old baud rate if possible.
                 */
                info->tty->termios->c_cflag &= ~CBAUD;
                if (old_termios) {
                        info->tty->termios->c_cflag |=
                                 (old_termios->c_cflag & CBAUD);
                        old_termios = NULL;
                        continue;
                }

                /*
                 * As a last resort, if the quotient is zero,
                 * default to 9600 bps
                 */
                info->tty->termios->c_cflag |= B9600;
        }

        info->timeout = (port->fifosize * HZ * bits * quot) /
                         (port->uartclk / 16);
        info->timeout += HZ/50;         /* Add .02 seconds of slop */

        if (cflag & CRTSCTS)
                info->flags |= ASYNC_CTS_FLOW;
        else
                info->flags &= ~ASYNC_CTS_FLOW;
        if (cflag & CLOCAL)
                info->flags &= ~ASYNC_CHECK_CD;
        else
                info->flags |= ASYNC_CHECK_CD;

        /*
         * Set up parity check flag
         */
#define RELEVENT_IFLAG(iflag)   ((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

        pm_access(info->state->pm);

        info->ops->change_speed(port, cflag, info->tty->termios->c_iflag, quot);}

#ifdef CONFIG_PM
/*
 *  Serial port power management.
 *
 * This is pretty coarse at the moment - either all on or all off.  We
 * should probably some day do finer power management here some day.
 *
 * We don't actually save any state; the serial driver has enough
 * state held internally to re-setup the port when we come out of D3.
 */
static int uart_pm(struct pm_dev *dev, pm_request_t rqst, void *data)
{
        if (rqst == PM_SUSPEND || rqst == PM_RESUME) {
                struct uart_state *state = dev->data;
                struct uart_port *port = state->port;
                struct uart_ops *ops = port->ops;
                int pm_state = (int)data;
                int running = state->info &&
                              state->info->flags & ASYNC_INITIALIZED;

//printk("pm: %08x: %d -> %d, %srunning\n", port->base, dev->state, pm_state, running ? "" : "not ");
                if (pm_state == 0) {
                        if (ops->pm)
                                ops->pm(port, pm_state, dev->state);
                        if (running) {
                                ops->set_mctrl(port, 0);
                                ops->startup(port, state->info);
                                uart_change_speed(state->info, NULL);
                                ops->set_mctrl(port, state->info->mctrl);
                                ops->start_tx(port, 1, 0);
                        }
                } else if (pm_state == 1) {
                        if (ops->pm)
                                ops->pm(port, pm_state, dev->state);
                } else {
                        if (running) {
                                ops->stop_tx(port, 0);
                                ops->set_mctrl(port, 0);
                                ops->stop_rx(port);
                                ops->shutdown(port, state->info);
                        }
                        if (ops->pm)
                                ops->pm(port, pm_state, dev->state);
                }
        }
        return 0;
}
#endif

