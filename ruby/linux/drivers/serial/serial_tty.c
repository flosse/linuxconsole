/*
 *  linux/drivers/char/serial_core.c
 *
 *  Driver core for serial ports
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
 *
 *
 * This is a generic driver for ARM AMBA-type serial ports.  They
 * have a lot of 16550-like features, but are not register compatable.
 * Note that although they do have CTS, DCD and DSR inputs, they do
 * not have an RI input, nor do they have DTR or RTS outputs.  If
 * required, these have to be supplied via some other means (eg, GPIO)
 * and hooked into this driver.
 *
 * This could very easily become a generic serial driver for dumb UARTs
 * (eg, {82,16x}50, 21285, SA1100).
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

#ifndef CONFIG_PM
#define pm_access(pm) do { } while (0)
#endif

/*
 * Things needed by tty driver
 */
static int uart_refcount;

/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the copy_from_user blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static u_char *tmp_buf;
static DECLARE_MUTEX(tmp_buf_sem);

#define HIGH_BITS_OFFSET	((sizeof(long)-sizeof(int))*8)

static void uart_change_speed(struct uart_info *info, struct termios *old_termios);
static void uart_wait_until_sent(struct tty_struct *tty, int timeout);

/*
 * This routine is used by the interrupt handler to schedule processing in
 * the software interrupt portion of the driver.  It is expected that
 * interrupts will be disabled (and so the tasklet will be prevented
 * from running (CHECK)).
 */
void uart_event(struct uart_info *info, int event)
{
	info->event |= 1 << event;
	tasklet_schedule(&info->tlet);
}

static void uart_stop(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	save_flags(flags); cli();
	info->ops->stop_tx(info->port, 1);
	restore_flags(flags);
}

static void uart_start(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;
	int nonempty;

	pm_access(info->state->pm);

	save_flags(flags); cli();
	nonempty = (info->xmit.head != info->xmit.tail && info->xmit.buf);
	info->ops->start_tx(info->port, nonempty, 1);
	restore_flags(flags);
}

static void uart_tasklet_action(unsigned long data)
{
	struct uart_info *info = (struct uart_info *)data;
	struct tty_struct *tty;

	tty = info->tty;
	if (!tty || !test_and_clear_bit(EVT_WRITE_WAKEUP, &info->event))
		return;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
}

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

static inline u_int uart_calculate_quot(struct uart_info *info, u_int baud)
{
	u_int quot;

	/* Special case: B0 rate */
	if (!baud)
		baud = 9600;

	/* Old HI/VHI/custom speed handling */
	if (baud == 38400 &&
	    ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST))
		quot = info->state->custom_divisor;
	else
		quot = (info->port->uartclk / (16 * baud)) - 1;

	return quot;
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
	info->timeout += HZ/50;		/* Add .02 seconds of slop */

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
#define RELEVENT_IFLAG(iflag)	((iflag) & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))

	pm_access(info->state->pm);

	info->ops->change_speed(port, cflag, info->tty->termios->c_iflag, quot);
}

static void uart_put_char(struct tty_struct *tty, u_char ch)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	if (!tty || !info->xmit.buf)
		return;

	save_flags(flags); cli();
	if (CIRC_SPACE(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE) != 0) {
		info->xmit.buf[info->xmit.head] = ch;
		info->xmit.head = (info->xmit.head + 1) & (UART_XMIT_SIZE - 1);
	}
	restore_flags(flags);
}

static void uart_flush_chars(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	pm_access(info->state->pm);

	if (info->xmit.head == info->xmit.tail
	    || tty->stopped
	    || tty->hw_stopped
	    || !info->xmit.buf)
		return;

	save_flags(flags); cli();
	info->ops->start_tx(info->port, 1, 0);
	restore_flags(flags);
}

static int uart_write(struct tty_struct *tty, int from_user,
			  const u_char * buf, int count)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;
	int c, ret = 0;

	if (!tty || !info->xmit.buf || !tmp_buf)
		return 0;

	save_flags(flags);
	if (from_user) {
		down(&tmp_buf_sem);
		while (1) {
			int c1;
			c = CIRC_SPACE_TO_END(info->xmit.head,
					      info->xmit.tail,
					      UART_XMIT_SIZE);
			if (count < c)
				c = count;
			if (c <= 0)
				break;

			c -= copy_from_user(tmp_buf, buf, c);
			if (!c) {
				if (!ret)
					ret = -EFAULT;
				break;
			}
			cli();
			c1 = CIRC_SPACE_TO_END(info->xmit.head,
					       info->xmit.tail,
					       UART_XMIT_SIZE);
			if (c1 < c)
				c = c1;
			memcpy(info->xmit.buf + info->xmit.head, tmp_buf, c);
			info->xmit.head = (info->xmit.head + c) &
					  (UART_XMIT_SIZE - 1);
			restore_flags(flags);
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		cli();
		while (1) {
			c = CIRC_SPACE_TO_END(info->xmit.head,
					      info->xmit.tail,
					      UART_XMIT_SIZE);
			if (count < c)
				c = count;
			if (c <= 0)
				break;
			memcpy(info->xmit.buf + info->xmit.head, buf, c);
			info->xmit.head = (info->xmit.head + c) &
					  (UART_XMIT_SIZE - 1);
			buf += c;
			count -= c;
			ret += c;
		}
		restore_flags(flags);
	}

	pm_access(info->state->pm);

	if (info->xmit.head != info->xmit.tail
	    && !tty->stopped
	    && !tty->hw_stopped)
		info->ops->start_tx(info->port, 1, 0);
	return ret;
}

static int uart_write_room(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;

	return CIRC_SPACE(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE);
}

static int uart_chars_in_buffer(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;

	return CIRC_CNT(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE);
}

static void uart_flush_buffer(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

#ifdef DEBUG
	printk("uart_flush_buffer(%d) called\n",
	       MINOR(tty->device) - tty->driver.minor_start);
#endif
	save_flags(flags); cli();
	info->xmit.head = info->xmit.tail = 0;
	restore_flags(flags);
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * This function is used to send a high-priority XON/XOFF character to
 * the device
 */
static void uart_send_xchar(struct tty_struct *tty, char ch)
{
	struct uart_info *info = tty->driver_data;

	info->port->x_char = ch;
	if (ch)
		info->ops->start_tx(info->port, 1, 0);
}

static void uart_throttle(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	if (I_IXOFF(tty))
		uart_send_xchar(tty, STOP_CHAR(tty));

	if (tty->termios->c_cflag & CRTSCTS) {
		save_flags(flags); cli();
		info->mctrl &= ~TIOCM_RTS;
		info->ops->set_mctrl(info->port, info->mctrl);
		restore_flags(flags);
	}
}

static void uart_unthrottle(struct tty_struct *tty)
{
	struct uart_info *info = (struct uart_info *) tty->driver_data;
	unsigned long flags;

	if (I_IXOFF(tty)) {
		if (info->port->x_char)
			info->port->x_char = 0;
		else
			uart_send_xchar(tty, START_CHAR(tty));
	}

	if (tty->termios->c_cflag & CRTSCTS) {
		save_flags(flags); cli();
		info->mctrl |= TIOCM_RTS;
		info->ops->set_mctrl(info->port, info->mctrl);
		restore_flags(flags);
	}
}

static int uart_get_info(struct uart_info *info, struct serial_struct *retinfo)
{
	struct uart_state *state = info->state;
	struct uart_port *port = info->port;
	struct serial_struct tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.type	   = port->type;
	tmp.line	   = port->line;
	tmp.port	   = port->base;
	if (HIGH_BITS_OFFSET)
		tmp.port_high = port->base >> HIGH_BITS_OFFSET;
	tmp.irq		   = port->irq;
	tmp.flags	   = port->flags;
	tmp.xmit_fifo_size = port->fifosize;
	tmp.baud_base	   = port->uartclk / 16;
	tmp.close_delay	   = state->close_delay;
	tmp.closing_wait   = state->closing_wait;
	tmp.custom_divisor = state->custom_divisor;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int uart_set_info(struct uart_info *info,
			 struct serial_struct *newinfo)
{
	struct serial_struct new_serial;
	struct uart_state *state;
	struct uart_port *port;
	unsigned long new_port;
	unsigned int change_irq, change_port;
	u_int old_flags, old_custom_divisor;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	state = info->state;
	old_flags = info->port->flags;
	old_custom_divisor = state->custom_divisor;
	port = info->port;

	new_port = new_serial.port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_serial.port_high << HIGH_BITS_OFFSET;

	change_irq  = new_serial.irq != port->irq;
	change_port = new_port != port->base;

	if (!capable(CAP_SYS_ADMIN)) {
		if (change_irq || change_port ||
		    (new_serial.baud_base != port->uartclk / 16) ||
		    (new_serial.close_delay != state->close_delay) ||
		    (new_serial.xmit_fifo_size != port->fifosize) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		state->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
	    (new_serial.baud_base < 9600))
		return -EINVAL;

#if 0
	if (new_serial.type && change_port) {
		for (i = 0; i < UART_NR; i++)
			if ((port != global_uart_state[i].port) &&
			    global_uart_state[i].port->base != new_port)
				return -EADDRINUSE;
	}
#endif

	if ((change_port || change_irq) && (state->count > 1))
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */
	port->uartclk = new_serial.baud_base * 16;
	port->flags = ((port->flags & ~ASYNC_FLAGS) |
			(new_serial.flags & ASYNC_FLAGS));
	info->flags = ((port->flags & ~ASYNC_INTERNAL_FLAGS) |
		       (info->flags & ASYNC_INTERNAL_FLAGS));
	state->custom_divisor = new_serial.custom_divisor;
	state->close_delay = new_serial.close_delay * HZ / 100;
	state->closing_wait = new_serial.closing_wait * HZ / 100;
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;
	port->fifosize = new_serial.xmit_fifo_size;

	if (change_port || change_irq) {
		/*
		 * We need to shutdown the serial port at the old
		 * port/irq combination.
		 */
		uart_shutdown(info);
		port->irq = new_serial.irq;
		port->base = new_port;
	}

check_and_exit:
	if (!port->base)
		return 0;
	if (info->flags & ASYNC_INITIALIZED) {
		if ((old_flags & ASYNC_SPD_MASK) !=
		    (port->flags & ASYNC_SPD_MASK) ||
		    (old_custom_divisor != state->custom_divisor)) {
			if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
				info->tty->alt_speed = 57600;
			if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
				info->tty->alt_speed = 115200;
			if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
				info->tty->alt_speed = 230400;
			if ((port->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
				info->tty->alt_speed = 460800;
			uart_change_speed(info, NULL);
		}
	} else
		retval = uart_startup(info);
	return retval;
}


/*
 * uart_get_lsr_info - get line status register info
 */
static int uart_get_lsr_info(struct uart_info *info, unsigned int *value)
{
	u_int result;
	unsigned long flags;

	save_flags(flags); cli();
	result = info->ops->tx_empty(info->port);
	restore_flags(flags);

	/*
	 * If we're about to load something into the transmit
	 * register, we'll pretend the transmitter isn't empty to
	 * avoid a race condition (depending on when the transmit
	 * interrupt happens).
	 */
	if (info->port->x_char ||
	    ((CIRC_CNT(info->xmit.head, info->xmit.tail,
		       UART_XMIT_SIZE) > 0) &&
	     !info->tty->stopped && !info->tty->hw_stopped))
		result &= TIOCSER_TEMT;
	
	return put_user(result, value);
}

static int uart_get_modem_info(struct uart_info *info, unsigned int *value)
{
	unsigned int result = info->mctrl;

	result |= info->ops->get_mctrl(info->port);

	return put_user(result, value);
}

static int uart_set_modem_info(struct uart_info *info, unsigned int cmd,
			  unsigned int *value)
{
	unsigned int arg, old;
	unsigned long flags;

	if (get_user(arg, value))
		return -EFAULT;

	save_flags(flags); cli();
	old = info->mctrl;
	switch (cmd) {
	case TIOCMBIS:
		info->mctrl |= arg;
		break;

	case TIOCMBIC:
		info->mctrl &= ~arg;
		break;

	case TIOCMSET:
		info->mctrl = arg;
		break;

	default:
		return -EINVAL;
	}
	if (old != info->mctrl)
		info->ops->set_mctrl(info->port, info->mctrl);
	restore_flags(flags);
	return 0;
}

static void uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	save_flags(flags); cli();
	info->ops->break_ctl(info->port, break_state);
	restore_flags(flags);
}

static int uart_ioctl(struct tty_struct *tty, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct uart_info *info = tty->driver_data;
	struct uart_icount cprev, cnow;
	struct serial_icounter_struct icount;
	unsigned long flags;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}

	switch (cmd) {
		case TIOCMGET:
			return uart_get_modem_info(info, (unsigned int *)arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return uart_set_modem_info(info, cmd,
						   (unsigned int *)arg);
		case TIOCGSERIAL:
			return uart_get_info(info,
					     (struct serial_struct *)arg);
		case TIOCSSERIAL:
			return uart_set_info(info,
					     (struct serial_struct *)arg);
		case TIOCSERGETLSR: /* Get line status register */
			return uart_get_lsr_info(info, (unsigned int *)arg);
		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			save_flags(flags); cli();
			/* note the counters on entry */
			cprev = info->port->icount;
			/* Force modem status interrupts on */
			info->ops->enable_ms(info->port);
			restore_flags(flags);
			while (1) {
				interruptible_sleep_on(&info->delta_msr_wait);
				/* see if a signal did it */
				if (signal_pending(current))
					return -ERESTARTSYS;
				save_flags(flags); cli();
				cnow = info->port->icount; /* atomic copy */
				restore_flags(flags);
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts)
					return -EIO; /* no change => error */
				if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
					return 0;
				}
				cprev = cnow;
			}
			/* NOTREACHED */

		/*
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		case TIOCGICOUNT:
			save_flags(flags); cli();
			cnow = info->port->icount;
			restore_flags(flags);
			icount.cts = cnow.cts;
			icount.dsr = cnow.dsr;
			icount.rng = cnow.rng;
			icount.dcd = cnow.dcd;
			icount.rx  = cnow.rx;
			icount.tx  = cnow.tx;
			icount.frame = cnow.frame;
			icount.overrun = cnow.overrun;
			icount.parity = cnow.parity;
			icount.brk = cnow.brk;
			icount.buf_overrun = cnow.buf_overrun;

			return copy_to_user((void *)arg, &icount, sizeof(icount))
					? -EFAULT : 0;

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static void uart_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;
	unsigned int cflag = tty->termios->c_cflag;

	if ((cflag ^ old_termios->c_cflag) == 0 &&
	    RELEVENT_IFLAG(tty->termios->c_iflag ^ old_termios->c_iflag) == 0)
		return;

	uart_change_speed(info, old_termios);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) &&
	    !(cflag & CBAUD)) {
		save_flags(flags); cli();
		info->mctrl &= ~(TIOCM_RTS | TIOCM_DTR);
		info->ops->set_mctrl(info->port, info->mctrl);
		restore_flags(flags);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) &&
	    (cflag & CBAUD)) {
		save_flags(flags); cli();
		info->mctrl |= TIOCM_DTR;
		if (!(cflag & CRTSCTS) ||
		    !test_bit(TTY_THROTTLED, &tty->flags))
			info->mctrl |= TIOCM_RTS;
		info->ops->set_mctrl(info->port, info->mctrl);
		restore_flags(flags);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		uart_start(tty);
	}

#if 0
	/*
	 * No need to wake up processes in open wait, since they
	 * sample the CLOCAL flag once, and don't recheck it.
	 * XXX  It's not clear whether the current behavior is correct
	 * or not.  Hence, this may change.....
	 */
	if (!(old_termios->c_cflag & CLOCAL) &&
	    (tty->termios->c_cflag & CLOCAL))
		wake_up_interruptible(&info->open_wait);
#endif
}

/*
 * In 2.4.5, calls to this will be serialized via the BKL
 */
static void uart_close(struct tty_struct *tty, struct file *filp)
{
	struct uart_register *reg = (struct uart_register *)tty->driver.driver_state;
	struct uart_info *info = tty->driver_data;
	struct uart_state *state;
	unsigned long flags;

	if (!info)
		return;

	state = info->state;

#ifdef DEBUG
	printk("uart_close() called\n");
#endif

	save_flags(flags); cli();

	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		goto done;
	}

	if ((tty->count == 1) && (state->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  state->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("uart_close: bad serial port count; tty->count is 1, "
		       "state->count is %d\n", state->count);
		state->count = 1;
	}
	if (--state->count < 0) {
		printk("rs_close: bad serial port count for %s%d: %d\n",
		       tty->driver.name, info->port->line, state->count);
		state->count = 0;
	}
	if (state->count) {
		restore_flags(flags);
		goto done;
	}
	info->flags |= ASYNC_CLOSING;
	restore_flags(flags);
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ASYNC_NORMAL_ACTIVE)
		info->state->normal_termios = *tty->termios;
	if (info->flags & ASYNC_CALLOUT_ACTIVE)
		info->state->callout_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->state->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->state->closing_wait);
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
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = NULL;
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
	if (reg->owner)
		__MOD_DEC_USE_COUNT(reg->owner);
}

static void uart_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct uart_info *info = (struct uart_info *) tty->driver_data;
	unsigned long char_time, expire;

	if (info->port->fifosize == 0)
		return;

	/*
	 * Set the check interval to be 1/5 of the estimated time to
	 * send a single character, and make it at least 1.  The check
	 * interval should also be less than the timeout.
	 *
	 * Note: we have to use pretty tight timings here to satisfy
	 * the NIST-PCTS.
	 */
	char_time = (info->timeout - HZ/50) / info->port->fifosize;
	char_time = char_time / 5;
	if (char_time == 0)
		char_time = 1;
	if (timeout && timeout < char_time)
		char_time = timeout;
	/*
	 * If the transmitter hasn't cleared in twice the approximate
	 * amount of time to send the entire FIFO, it probably won't
	 * ever clear.  This assumes the UART isn't doing flow
	 * control, which is currently the case.  Hence, if it ever
	 * takes longer than info->timeout, this is probably due to a
	 * UART bug of some kind.  So, we clamp the timeout parameter at
	 * 2*info->timeout.
	 */
	if (!timeout || timeout > 2 * info->timeout)
		timeout = 2 * info->timeout;

	expire = jiffies + timeout;
#ifdef DEBUG
	printk("uart_wait_until_sent(%d), jiff=%lu, expire=%lu...\n",
	       MINOR(tty->device) - tty->driver.minor_start, jiffies,
	       expire);
#endif
	while (!info->ops->tx_empty(info->port)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(char_time);
		if (signal_pending(current))
			break;
		if (timeout && time_after(jiffies, expire))
			break;
	}
	set_current_state(TASK_RUNNING); /* might not be needed */
}

static void uart_hangup(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	struct uart_state *state = info->state;

	uart_flush_buffer(tty);
	if (info->flags & ASYNC_CLOSING)
		return;
	uart_shutdown(info);
	info->event = 0;
	state->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	info->tty = NULL;
	wake_up_interruptible(&info->open_wait);
}

static int uart_block_til_ready(struct tty_struct *tty, struct file *filp,
				struct uart_info *info)
{
	DECLARE_WAITQUEUE(wait, current);
	struct uart_state *state = info->state;
	unsigned long flags;
	int do_clocal = 0, extra_count = 0, retval;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
		return (info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS;
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
		if (info->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
			return -EBUSY;
		info->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}

	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (info->flags & ASYNC_CALLOUT_ACTIVE) {
		if (state->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}

	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, state->count is dropped by one, so that
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
	save_flags(flags); cli();
	if (!tty_hung_up_p(filp)) {
		extra_count = 1;
		state->count--;
	}
	restore_flags(flags);
	info->blocked_open++;
	while (1) {
		save_flags(flags); cli();
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (tty->termios->c_cflag & CBAUD)) {
			info->mctrl = TIOCM_DTR | TIOCM_RTS;
			info->ops->set_mctrl(info->port, info->mctrl);
		}
		restore_flags(flags);
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ASYNC_INITIALIZED)) {
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
			break;
		}
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ASYNC_CLOSING) &&
		    (do_clocal ||
		     (info->ops->get_mctrl(info->port) & TIOCM_CAR)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&info->open_wait, &wait);
	if (extra_count)
		state->count++;
	info->blocked_open--;
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static struct uart_info *uart_get(struct uart_register *reg, int line)
{
	struct uart_state *state = reg->state + line;
	struct uart_info *info;

	state->count++;
	if (state->info)
		return state->info;
	info = kmalloc(sizeof(struct uart_info), GFP_KERNEL);
	if (info) {
		memset(info, 0, sizeof(struct uart_info));
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		init_waitqueue_head(&info->delta_msr_wait);
		info->port  = state->port;
		info->flags = info->port->flags;
		info->ops   = info->port->ops;
		info->state = state;
		tasklet_init(&info->tlet, uart_tasklet_action,
			     (unsigned long)info);
	}
	if (state->info) {
		kfree(info);
		return state->info;
	}
	state->info = info;
	return info;
}

/*
 * Ugg, calls to uart_open are not serialised.
 */
static int uart_open(struct tty_struct *tty, struct file *filp)
{
	struct uart_register *reg = (struct uart_register *)tty->driver.driver_state;
	struct uart_info *info;
	int retval, line = MINOR(tty->device) - tty->driver.minor_start;

#ifdef DEBUG
	printk("uart_open(%d) called\n", line);
#endif

	retval = -ENODEV;
	if (line >= tty->driver.num)
		goto fail;

	if (!try_inc_mod_count(reg->owner))
		goto fail;

	info = uart_get(reg, line);
	retval = -ENOMEM;
	if (!info)
		goto out;

	tty->driver_data = info;
	info->tty = tty;
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	/*
	 * Make sure we have the temporary buffer allocated.  Note
	 * that we set retval appropriately above, and we rely on
	 * this.
	 */
	if (!tmp_buf) {
		unsigned long page = get_zeroed_page(GFP_KERNEL);
		if (tmp_buf)
			free_page(page);
		else if (!page)
			goto out;
		tmp_buf = (u_char *)page;
	}

	/*
	 * If the port is in the middle of closing, bail out now.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
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

	retval = uart_block_til_ready(tty, filp, info);
	if (retval)
		goto out;

	if (info->state->count == 1) {
		int changed_termios = 0;

		if (info->flags & ASYNC_SPLIT_TERMIOS) {
			if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
				*tty->termios = info->state->normal_termios;
			else
				*tty->termios = info->state->callout_termios;
			changed_termios = 1;
		}

#ifdef CONFIG_SERIAL_CORE_CONSOLE
		/*
		 * Copy across the serial console cflag setting
		 */
		{
			struct console *c = reg->cons;
			if (c && c->cflag && c->index == line) {
				tty->termios->c_cflag = c->cflag;
				c->cflag = 0;
				changed_termios = 1;
			}
		}
#endif
		if (changed_termios)
			uart_change_speed(info, NULL);
	}

	info->session = current->session;
	info->pgrp = current->pgrp;
	return 0;

out:
	if (reg->owner)
		__MOD_DEC_USE_COUNT(reg->owner);
fail:
	return retval;
}

#ifdef CONFIG_PROC_FS

static int uart_line_info(char *buf, struct uart_register *reg, int i)
{
	struct uart_state *state = reg->state + i;
	struct uart_port *port = state->port;
	char stat_buf[32];
	u_int status;
	int ret;

	ret = sprintf(buf, "%d: port:%08X irq:%d",
			port->line, port->base, port->irq);

	if (!port->base) {
		strcat(buf, "\n");
		return ret + 1;
	}

	status = port->ops->get_mctrl(port);

	ret += sprintf(buf + ret, " tx:%d rx:%d",
			port->icount.tx, port->icount.rx);
	if (port->icount.frame)
		ret += sprintf(buf + ret, " fe:%d",
			port->icount.frame);
	if (port->icount.parity)
		ret += sprintf(buf + ret, " pe:%d",
			port->icount.parity);
	if (port->icount.brk)
		ret += sprintf(buf + ret, " brk:%d",
			port->icount.brk);
	if (port->icount.overrun)
		ret += sprintf(buf + ret, " oe:%d",
			port->icount.overrun);

	stat_buf[0] = '\0';
	stat_buf[1] = '\0';
	if (state->info && state->info->mctrl & TIOCM_RTS)
		strcat(stat_buf, "|RTS");
	if (status & TIOCM_CTS)
		strcat(stat_buf, "|CTS");
	if (state->info && state->info->mctrl & TIOCM_DTR)
		strcat(stat_buf, "|DTR");
	if (status & TIOCM_DSR)
		strcat(stat_buf, "|DSR");
	if (status & TIOCM_CAR)
		strcat(stat_buf, "|CD");
	if (status & TIOCM_RNG)
		strcat(stat_buf, "|RI");
	if (stat_buf[0])
		stat_buf[0] = ' ';
	strcat(stat_buf, "\n");

	ret += sprintf(buf + ret, stat_buf);
	return ret;
}

static int uart_read_proc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	struct tty_driver *drv = data;
	struct uart_register *reg = drv->driver_state;
	int i, len = 0, l;
	off_t begin = 0;

	len += sprintf(page, "serinfo:1.0 driver%s%s revision:%s\n",
			"", "", "");
	for (i = 0; i < reg->nr && len < PAGE_SIZE - 96; i++) {
		l = uart_line_info(page + len, reg, i);
		len += l;
		if (len + begin > off + count)
			goto done;
		if (len + begin < off) {
			begin += len;
			len = 0;
		}
	}
	*eof = 1;
done:
	if (off >= len + begin)
		return 0;
	*start = page + (off - begin);
	return (count < begin + len - off) ? count : (begin + len - off);
}
#endif

#ifdef CONFIG_SERIAL_CORE_CONSOLE
/*
 *	Check whether an invalid uart number has been specified, and
 *	if so, search for the first available port that does have
 *	console support.
 */
struct uart_port * __init uart_get_console(struct uart_port *ports, int nr, struct console *co)
{
	int idx = co->index;

	if (idx >= nr || ports[idx].ops == NULL)
		for (idx = 0; idx < nr; idx++)
			if (ports[idx].ops)
				break;

	co->index = idx;

	return ports + idx;
}

void __init uart_parse_options(char *options, int *baud, int *parity, int *bits)
{
	char *s = options;

	*baud = simple_strtoul(s, NULL, 10);
	while (*s >= '0' && *s <= '9')
		s++;
	if (*s)
		*parity = *s++;
	if (*s)
		*bits = *s - '0';
}

int __init uart_set_options(struct uart_port *port, struct console *co, int baud, int parity, int bits)
{
	u_int cflag = CREAD | HUPCL | CLOCAL;
	u_int quot;

	/*
	 * Construct a cflag setting.
	 */
	switch (baud) {
	case 1200:	cflag |= B1200;			break;
	case 2400:	cflag |= B2400;			break;
	case 4800:	cflag |= B4800;			break;
	case 9600:	cflag |= B9600;			break;
	case 19200:	cflag |= B19200;		break;
	default:	cflag |= B38400;  baud = 38400;	break;
	case 57600:	cflag |= B57600;		break;
	case 115200:	cflag |= B115200;		break;
	case 230400:	cflag |= B230400;		break;
	case 460800:	cflag |= B460800;		break;
	}

	if (bits == 7)
		cflag |= CS7;
	else
		cflag |= CS8;

	switch (parity) {
	case 'o': case 'O':
		cflag |= PARODD;
		/*fall through*/
	case 'e': case 'E':
		cflag |= PARENB;
		break;
	}

	co->cflag = cflag;
	quot = (port->uartclk / (16 * baud)) - 1;
	port->ops->change_speed(port, cflag, 0, quot);

	return 0;
}
#endif /* CONFIG_SERIAL_CORE_CONSOLE */

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

/*
 * Register a set of ports with the core driver.  Note that we don't
 * printk any information about the ports; that is up to the low level
 * driver to do if they so wish.
 */
int uart_register_port(struct uart_register *reg)
{
	struct tty_driver *normal, *callout;
	int i, retval;

	normal  = reg->normal_driver;
	callout = reg->callout_driver;

	normal->magic		= TTY_DRIVER_MAGIC;
	normal->driver_name	= reg->normal_name;
	normal->name		= reg->normal_name;
	normal->major		= reg->normal_major;
	normal->minor_start	= reg->minor;
	normal->num		= reg->nr;
	normal->type		= TTY_DRIVER_TYPE_SERIAL;
	normal->subtype		= SERIAL_TYPE_NORMAL;
	normal->init_termios	= tty_std_termios;
	normal->init_termios.c_cflag = B38400 | CS8 | CREAD | HUPCL | CLOCAL;
	normal->flags		= TTY_DRIVER_REAL_RAW;
	normal->refcount	= &uart_refcount;
	normal->table		= reg->table;
	normal->termios		= reg->termios;
	normal->termios_locked	= reg->termios_locked;
	normal->driver_state    = reg;

	normal->open		= uart_open;
	normal->close		= uart_close;
	normal->write		= uart_write;
	normal->put_char	= uart_put_char;
	normal->flush_chars	= uart_flush_chars;
	normal->write_room	= uart_write_room;
	normal->chars_in_buffer	= uart_chars_in_buffer;
	normal->flush_buffer	= uart_flush_buffer;
	normal->ioctl		= uart_ioctl;
	normal->throttle	= uart_throttle;
	normal->unthrottle	= uart_unthrottle;
	normal->send_xchar	= uart_send_xchar;
	normal->set_termios	= uart_set_termios;
	normal->stop		= uart_stop;
	normal->start		= uart_start;
	normal->hangup		= uart_hangup;
	normal->break_ctl	= uart_break_ctl;
	normal->wait_until_sent	= uart_wait_until_sent;
	normal->read_proc	= uart_read_proc;

	/*
	 * The callout device is just like the normal device except for
	 * the major number and the subtype code.
	 */
	callout			= normal + 1;
	*callout		= *normal;
	callout->name		= reg->callout_name;
	callout->major		= reg->callout_major;
	callout->subtype	= SERIAL_TYPE_CALLOUT;
	callout->read_proc	= NULL;
	callout->proc_entry	= NULL;

	if (reg->state)
		panic("reg->state already allocated\n");

	/*
	 * Maybe we should be using a slab cache for this,
	 * especially if we have a large number of ports
	 * to handle.
	 */
	reg->state = kmalloc(sizeof(struct uart_state) * reg->nr, GFP_KERNEL);
	retval = -ENOMEM;
	if (!reg->state)
		goto out;

	memset(reg->state, 0, sizeof(struct uart_state) * reg->nr);

	for (i = 0; i < reg->nr; i++) {
		struct uart_state *state = reg->state + i;

		state->close_delay	= 5 * HZ / 10;
		state->closing_wait	= 30 * HZ;
		state->callout_termios	= callout->init_termios;
		state->normal_termios	= normal->init_termios;
		state->port		= reg->port + i;
		state->port->line	= i;
#ifdef CONFIG_PM
		state->pm		= pm_register(PM_SYS_DEV,
						PM_SYS_COM, uart_pm);
		if (state->pm)
			state->pm->data = state;
		/*
		 * Power down all ports by default, except
		 * the console if we have one.
		 */
		if (!reg->cons && i != reg->cons->index)
			pm_send(state->pm, PM_SUSPEND, (void *)3);
#endif
	}

	retval = tty_register_driver(normal);
	if (retval)
		goto out;

	retval = tty_register_driver(callout);
	if (retval)
		tty_unregister_driver(normal);

out:
	if (retval && reg->state)
		kfree(reg->state);
	return retval;
}

void uart_unregister_port(struct uart_register *reg)
{
	int i;

	tty_unregister_driver(reg->normal_driver);
	tty_unregister_driver(reg->callout_driver);

	for (i = 0; i < reg->nr; i++) {
		struct uart_state *state = reg->state + i;

#ifdef CONFIG_PM
		pm_unregister(state->pm);
#endif
		if (state->info) {
			tasklet_kill(&state->info->tlet);
			kfree(state->info);
		}
	}
	kfree(reg->state);
}

EXPORT_SYMBOL(uart_register_port);
EXPORT_SYMBOL(uart_unregister_port);
