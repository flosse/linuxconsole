/*
 *  linux/drivers/char/serial_core.c
 *
 *  Driver core for serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright 1999 ARM Limited
 *  Copyright (C) 2000-2001 Deep Blue Solutions Ltd.
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
 *  $Id$
 *
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/circ_buf.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/pm.h>
#include <linux/serial_core.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#undef	DEBUG

#ifndef CONFIG_PM
#define pm_access(pm)		do { } while (0)
#define pm_unregister(pm)	do { } while (0)
#endif

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

/*
 * This is used to lock changes in serial line configuration.
 */
static DECLARE_MUTEX(port_sem);

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

	spin_lock_irqsave(&info->lock, flags);
	info->ops->stop_tx(info->port, 1);
	spin_unlock_irqrestore(&info->lock, flags);
}

static void __uart_start(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	if (info->xmit.head != info->xmit.tail && info->xmit.buf &&
	    !tty->stopped && !tty->hw_stopped)
		info->ops->start_tx(info->port, 1, 1);
}

static void uart_start(struct tty_struct *tty)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	pm_access(info->state->pm);

	spin_lock_irqsave(&info->lock, flags);
	__uart_start(tty);
	spin_unlock_irqrestore(&info->lock, flags);
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

static inline void uart_update_altspeed(struct uart_info *info)
{
	if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
		info->tty->alt_speed = 57600;
	if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
		info->tty->alt_speed = 115200;
	if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
		info->tty->alt_speed = 230400;
	if ((info->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
		info->tty->alt_speed = 460800;
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

	if (info->port->type == PORT_UNKNOWN) {
		if (info->tty)
			set_bit(TTY_IO_ERROR, &info->tty->flags);
		free_page(page);
		goto errout;
	}

	if (info->xmit.buf)
		free_page(page);
	else
		info->xmit.buf = (unsigned char *) page;

	info->mctrl = 0;

	retval = info->ops->startup(info->port, info);
	if (retval) {
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
	if (info->tty)
		uart_update_altspeed(info);

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
		quot = info->port->uartclk / (16 * baud);

	return quot;
}

static void uart_change_speed(struct uart_info *info, struct termios *old_termios)
{
	struct uart_port *port = info->port;
	u_int quot, baud, cflag, bits, try;

	/*
	 * If we have no tty, termios, or the port does not exist,
	 * then we can't set the parameters for this port.
	 */
	if (!info->tty || !info->tty->termios ||
	    info->port->type == PORT_UNKNOWN)
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

	spin_lock_irqsave(&info->lock, flags);
	if (CIRC_SPACE(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE) != 0) {
		info->xmit.buf[info->xmit.head] = ch;
		info->xmit.head = (info->xmit.head + 1) & (UART_XMIT_SIZE - 1);
	}
	spin_unlock_irqrestore(&info->lock, flags);
}

static void uart_flush_chars(struct tty_struct *tty)
{
	uart_start(tty);
}

static int uart_write(struct tty_struct *tty, int from_user,
			  const u_char * buf, int count)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;
	int c, ret = 0;

	if (!tty || !info->xmit.buf || !tmp_buf)
		return 0;

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
			spin_lock_irqsave(&info->lock, flags);
			c1 = CIRC_SPACE_TO_END(info->xmit.head,
					       info->xmit.tail,
					       UART_XMIT_SIZE);
			if (c1 < c)
				c = c1;
			memcpy(info->xmit.buf + info->xmit.head, tmp_buf, c);
			info->xmit.head = (info->xmit.head + c) &
					  (UART_XMIT_SIZE - 1);
			spin_unlock_irqrestore(&info->lock, flags);
			buf += c;
			count -= c;
			ret += c;
		}
		up(&tmp_buf_sem);
	} else {
		spin_lock_irqsave(&info->lock, flags);
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
		spin_unlock_irqrestore(&info->lock, flags);
	}

	uart_start(tty);
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
	spin_lock_irqsave(&info->lock, flags);
	info->xmit.head = info->xmit.tail = 0;
	spin_unlock_irqrestore(&info->lock, flags);
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
		spin_lock_irqsave(&info->lock, flags);
		info->mctrl &= ~TIOCM_RTS;
		info->ops->set_mctrl(info->port, info->mctrl);
		spin_unlock_irqrestore(&info->lock, flags);
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
		spin_lock_irqsave(&info->lock, flags);
		info->mctrl |= TIOCM_RTS;
		info->ops->set_mctrl(info->port, info->mctrl);
		spin_unlock_irqrestore(&info->lock, flags);
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
	tmp.port	   = port->iobase;
	if (HIGH_BITS_OFFSET)
		tmp.port_high = port->iobase >> HIGH_BITS_OFFSET;
	tmp.irq		   = port->irq;
	tmp.flags	   = port->flags;
	tmp.xmit_fifo_size = port->fifosize;
	tmp.baud_base	   = port->uartclk / 16;
	tmp.close_delay	   = state->close_delay;
	tmp.closing_wait   = state->closing_wait;
	tmp.custom_divisor = state->custom_divisor;
	tmp.hub6	   = port->hub6;
	tmp.io_type        = port->iotype;
	tmp.iomem_reg_shift= port->regshift;
	tmp.iomem_base     = (void *)port->mapbase;

	if (copy_to_user(retinfo, &tmp, sizeof(*retinfo)))
		return -EFAULT;
	return 0;
}

static int uart_set_info(struct uart_info *info,
			 struct serial_struct *newinfo)
{
	struct serial_struct new_serial;
	struct uart_state *state = info->state;
	struct uart_port *port = info->port;
	unsigned long new_port;
	unsigned int change_irq, change_port, old_flags;
	unsigned int old_custom_divisor;
	int retval = 0;

	if (copy_from_user(&new_serial, newinfo, sizeof(new_serial)))
		return -EFAULT;

	new_port = new_serial.port;
	if (HIGH_BITS_OFFSET)
		new_port += (unsigned long) new_serial.port_high << HIGH_BITS_OFFSET;

	new_serial.irq = irq_cannonicalize(new_serial.irq);

	/*
	 * This semaphore protects state->count.  It is also
	 * very useful to prevent opens.  Also, take the
	 * port configuration semaphore to make sure that a
	 * module insertion/removal doesn't change anything
	 * under us.
	 */
	down(&port_sem);
	down(&state->count_sem);

	change_irq  = new_serial.irq != port->irq;

	/*
	 * Since changing the 'type' of the port changes its resource
	 * allocations, we should treat type changes the same as
	 * IO port changes.
	 */
	change_port = new_port != port->iobase ||
		      (unsigned long)new_serial.iomem_base != port->mapbase ||
		      new_serial.hub6 != port->hub6 ||
		      new_serial.io_type != port->iotype ||
		      new_serial.iomem_reg_shift != port->regshift ||
		      new_serial.type != port->type;

	old_flags = port->flags;
	old_custom_divisor = state->custom_divisor;

	if (!capable(CAP_SYS_ADMIN)) {
		retval = -EPERM;
		if (change_irq || change_port ||
		    (new_serial.baud_base != port->uartclk / 16) ||
		    (new_serial.close_delay != state->close_delay) ||
		    (new_serial.closing_wait != state->closing_wait) ||
		    (new_serial.xmit_fifo_size != port->fifosize) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (port->flags & ~ASYNC_USR_MASK)))
			goto exit;
		port->flags = ((port->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		state->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	/*
	 * Ask the low level driver to verify the settings.
	 */
	if (port->ops->verify_port)
		retval = port->ops->verify_port(port, &new_serial);

	if ((new_serial.irq >= NR_IRQS) || (new_serial.irq < 0) ||
	    (new_serial.baud_base < 9600))
		retval = -EINVAL;

	if (retval)
		goto exit;

	if (change_port || change_irq) {
		retval = -EBUSY;

		/*
		 * Make sure that we are the sole user of this port.
		 */
		if (state->count > 1)
			goto exit;

		/*
		 * We need to shutdown the serial port at the old
		 * port/type/irq combination.
		 */
		uart_shutdown(info);
	}

	if (change_port) {
		unsigned long old_iobase, old_mapbase;
		unsigned int old_type, old_iotype, old_hub6, old_shift;

		old_iobase = port->iobase;
		old_mapbase = port->mapbase;
		old_type = port->type;
		old_hub6 = port->hub6;
		old_iotype = port->iotype;
		old_shift = port->regshift;

		/*
		 * Free and release old regions
		 */
		if (old_type != PORT_UNKNOWN)
			port->ops->release_port(port);

		port->iobase = new_port;
		port->type = new_serial.type;
		port->hub6 = new_serial.hub6;
		port->iotype = new_serial.io_type;
		port->regshift = new_serial.iomem_reg_shift;
		port->mapbase = (unsigned long)new_serial.iomem_base;

		/*
		 * Claim and map the new regions
		 */
		if (port->type != PORT_UNKNOWN)
			retval = port->ops->request_port(port);

		/*
		 * If we fail to request resources for the
		 * new port, try to restore the old settings.
		 */
		if (retval && old_type != PORT_UNKNOWN) {
			port->iobase = old_iobase;
			port->type = old_type;
			port->hub6 = old_hub6;
			port->iotype = old_iotype;
			port->regshift = old_shift;
			port->mapbase = old_mapbase;
			retval = port->ops->request_port(port);
			/*
			 * If we failed to restore the old settings,
			 * we fail like this.
			 */
			if (retval)
				port->type = PORT_UNKNOWN;

			/*
			 * We failed anyway.
			 */
			retval = -EBUSY;
		}
	}

	port->irq  = new_serial.irq;
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

check_and_exit:
	retval = 0;
	if (port->type == PORT_UNKNOWN)
		goto exit;
	if (info->flags & ASYNC_INITIALIZED) {
		if (((old_flags & info->flags) & ASYNC_SPD_MASK) ||
		    old_custom_divisor != state->custom_divisor) {
			uart_update_altspeed(info);
			uart_change_speed(info, NULL);
		}
	} else
		retval = uart_startup(info);
exit:
	up(&state->count_sem);
	up(&port_sem);
	return retval;
}


/*
 * uart_get_lsr_info - get line status register info
 */
static int uart_get_lsr_info(struct uart_info *info, unsigned int *value)
{
	u_int result;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	result = info->ops->tx_empty(info->port);
	spin_unlock_irqrestore(&info->lock, flags);

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
		result &= ~TIOCSER_TEMT;
	
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
	int ret = 0;

	if (get_user(arg, value))
		return -EFAULT;

	spin_lock_irq(&info->lock);
	old = info->mctrl;
	switch (cmd) {
	case TIOCMBIS:	info->mctrl |= arg;	break;
	case TIOCMBIC:	info->mctrl &= ~arg;	break;
	case TIOCMSET:	info->mctrl = arg;	break;
	default:	ret = -EINVAL;		break;
	}
	if (old != info->mctrl)
		info->ops->set_mctrl(info->port, info->mctrl);
	spin_unlock_irq(&info->lock);
	return ret;
}

static void uart_break_ctl(struct tty_struct *tty, int break_state)
{
	struct uart_info *info = tty->driver_data;
	unsigned long flags;

	if (info->port->type != PORT_UNKNOWN) {
		spin_lock_irqsave(&info->lock, flags);
		info->ops->break_ctl(info->port, break_state);
		spin_unlock_irqrestore(&info->lock, flags);
	}
}

static int uart_do_autoconfig(struct uart_info *info)
{
	struct uart_port *port = info->port;
	int flags, ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Take the 'count' lock.  This prevents count
	 * from incrementing, and hence any extra opens
	 * of the port while we're auto-configging.
	 */
	down(&info->state->count_sem);

	ret = -EBUSY;
	if (info->state->count == 1) {
		uart_shutdown(info);

		/*
		 * If we already have a port type configured,
		 * we must release its resources.
		 */
		if (port->type != PORT_UNKNOWN)
			port->ops->release_port(port);

		flags = UART_CONFIG_TYPE;
		if (port->flags & ASYNC_AUTO_IRQ)
			flags |= UART_CONFIG_IRQ;

		/*
		 * This will claim the ports resources if
		 * a port is found.
		 */
		port->ops->config_port(port, flags);

		ret = uart_startup(info);
	}
	up(&info->state->count_sem);
	return ret;
}

/*
 * Called from userspace.  We can use spin_lock_irq() here.
 */
static int uart_ioctl(struct tty_struct *tty, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct uart_info *info = tty->driver_data;
	struct uart_icount cprev, cnow;
	struct serial_icounter_struct icount;
	int ret = -ENOIOCTLCMD;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGSTRUCT) &&
	    (cmd != TIOCMIWAIT) && (cmd != TIOCGICOUNT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return -EIO;
	}

	switch (cmd) {
		case TIOCMGET:
			ret = uart_get_modem_info(info, (unsigned int *)arg);
			break;

		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			ret = uart_set_modem_info(info, cmd,
						  (unsigned int *)arg);
			break;

		case TIOCGSERIAL:
			ret = uart_get_info(info, (struct serial_struct *)arg);
			break;

		case TIOCSSERIAL:
			ret = uart_set_info(info, (struct serial_struct *)arg);
			break;

		case TIOCSERCONFIG:
			ret = uart_do_autoconfig(info);
			break;

		case TIOCSERGETLSR: /* Get line status register */
			ret = uart_get_lsr_info(info, (unsigned int *)arg);
			break;

		/*
		 * Wait for any of the 4 modem inputs (DCD,RI,DSR,CTS) to change
		 * - mask passed in arg for lines of interest
		 *   (use |'ed TIOCM_RNG/DSR/CD/CTS for masking)
		 * Caller should use TIOCGICOUNT to see which one it was
		 */
		case TIOCMIWAIT:
			spin_lock_irq(&info->lock);
			/* note the counters on entry */
			cprev = info->port->icount;
			/* Force modem status interrupts on */
			info->ops->enable_ms(info->port);
			spin_unlock_irq(&info->lock);
			while (1) {
				interruptible_sleep_on(&info->delta_msr_wait);
				/* see if a signal did it */
				if (signal_pending(current)) {
					ret = -ERESTARTSYS;
					break;
				}
				spin_lock_irq(&info->lock);
				cnow = info->port->icount; /* atomic copy */
				spin_unlock_irq(&info->lock);
				if (cnow.rng == cprev.rng && cnow.dsr == cprev.dsr &&
				    cnow.dcd == cprev.dcd && cnow.cts == cprev.cts) {
				    	ret = -EIO; /* no change => error */
				    	break;
				}
				if (((arg & TIOCM_RNG) && (cnow.rng != cprev.rng)) ||
				    ((arg & TIOCM_DSR) && (cnow.dsr != cprev.dsr)) ||
				    ((arg & TIOCM_CD)  && (cnow.dcd != cprev.dcd)) ||
				    ((arg & TIOCM_CTS) && (cnow.cts != cprev.cts))) {
				    	ret = 0;
				    	break;
				}
				cprev = cnow;
			}
			break;

		/*
		 * Get counter of input serial line interrupts (DCD,RI,DSR,CTS)
		 * Return: write counters to the user passed counter struct
		 * NB: both 1->0 and 0->1 transitions are counted except for
		 *     RI where only 0->1 is counted.
		 */
		case TIOCGICOUNT:
			spin_lock_irq(&info->lock);
			cnow = info->port->icount;
			spin_unlock_irq(&info->lock);

			icount.cts         = cnow.cts;
			icount.dsr         = cnow.dsr;
			icount.rng         = cnow.rng;
			icount.dcd         = cnow.dcd;
			icount.rx          = cnow.rx;
			icount.tx          = cnow.tx;
			icount.frame       = cnow.frame;
			icount.overrun     = cnow.overrun;
			icount.parity      = cnow.parity;
			icount.brk         = cnow.brk;
			icount.buf_overrun = cnow.buf_overrun;

			ret = copy_to_user((void *)arg, &icount, sizeof(icount))
					? -EFAULT : 0;
			break;

		case TIOCSERGWILD: /* obsolete */
		case TIOCSERSWILD: /* obsolete */
			ret = 0;
			break;

		default:
			if (info->ops->ioctl)
				ret = info->ops->ioctl(info->port, cmd, arg);
			break;
	}
	return ret;
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

	spin_lock_irqsave(&info->lock, flags);

	/* Handle transition to B0 status */
	if ((old_termios->c_cflag & CBAUD) && !(cflag & CBAUD)) {
		info->mctrl &= ~(TIOCM_RTS | TIOCM_DTR);
		info->ops->set_mctrl(info->port, info->mctrl);
	}

	/* Handle transition away from B0 status */
	if (!(old_termios->c_cflag & CBAUD) && (cflag & CBAUD)) {
		info->mctrl |= TIOCM_DTR;
		if (!(cflag & CRTSCTS) ||
		    !test_bit(TTY_THROTTLED, &tty->flags))
			info->mctrl |= TIOCM_RTS;
		info->ops->set_mctrl(info->port, info->mctrl);
	}

	/* Handle turning off CRTSCTS */
	if ((old_termios->c_cflag & CRTSCTS) && !(cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		__uart_start(tty);
	}
	spin_unlock_irqrestore(&info->lock, flags);

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
 * In 2.4.5, calls to this will be serialized via the BKL in
 *  linux/drivers/char/tty_io.c:tty_release()
 *  linux/drivers/char/tty_io.c:do_tty_handup()
 */
static void uart_close(struct tty_struct *tty, struct file *filp)
{
	struct uart_driver *drv = (struct uart_driver *)tty->driver.driver_state;
	struct uart_info *info = tty->driver_data;
	struct uart_state *state;
	unsigned long flags;

	if (!info)
		return;

	state = info->state;

#ifdef DEBUG
	printk("uart_close() called\n");
#endif

	/*
	 * This is safe, as long as the BKL exists in
	 * do_tty_hangup(), and we're protected by the BKL.
	 */
	if (tty_hung_up_p(filp))
		goto done;

	down(&state->count_sem);
	spin_lock_irqsave(&info->lock, flags);
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
		spin_unlock_irqrestore(&info->lock, flags);
		up(&state->count_sem);
		goto done;
	}
	info->flags |= ASYNC_CLOSING;
	spin_unlock_irqrestore(&info->lock, flags);
	up(&state->count_sem);

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
	if (drv->owner)
		__MOD_DEC_USE_COUNT(drv->owner);
}

static void uart_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct uart_info *info = (struct uart_info *) tty->driver_data;
	unsigned long char_time, expire;

	if (info->port->type == PORT_UNKNOWN ||
	    info->port->fifosize == 0)
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

/*
 * This is called with the BKL in effect
 *  linux/drivers/char/tty_io.c:do_tty_hangup()
 */
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
	 * then make the check up front and then exit.  Note that
	 * we have set TTY_IO_ERROR for a non-enabled port.
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
	down(&state->count_sem);
	spin_lock_irqsave(&info->lock, flags);
	if (!tty_hung_up_p(filp)) {
		extra_count = 1;
		state->count--;
	}
	spin_unlock_irqrestore(&info->lock, flags);
	info->blocked_open++;
	up(&state->count_sem);
	while (1) {
		spin_lock_irqsave(&info->lock, flags);
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (tty->termios->c_cflag & CBAUD)) {
			info->mctrl = TIOCM_DTR | TIOCM_RTS;
			info->ops->set_mctrl(info->port, info->mctrl);
		}
		spin_unlock_irqrestore(&info->lock, flags);
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
	down(&state->count_sem);
	if (extra_count)
		state->count++;
	info->blocked_open--;
	up(&state->count_sem);
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}

static struct uart_info *uart_get(struct uart_driver *drv, int line)
{
	struct uart_state *state = drv->state + line;
	struct uart_info *info;

	down(&state->count_sem);
	state->count++;
	if (state->info)
		goto out;

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
	if (state->info)
		kfree(info);
	else
		state->info = info;
out:
	up(&state->count_sem);
	return state->info;
}

/*
 * Make sure we have the temporary buffer allocated.  Note
 * that we set retval appropriately above, and we rely on
 * this.
 */
static inline int uart_alloc_tmpbuf(void)
{
	if (!tmp_buf) {
		unsigned long buf = get_zeroed_page(GFP_KERNEL);
		if (!tmp_buf) {
			if (buf)
				tmp_buf = (u_char *)buf;
			else
				return -ENOMEM;
		} else
			free_page(buf);
	}
	return 0;
}

/*
 * In 2.4.5, calls to uart_open are serialised by the BKL in
 *   linux/fs/devices.c:chrdev_open()
 * Note that if this fails, then uart_close() _will_ be called.
 */
static int uart_open(struct tty_struct *tty, struct file *filp)
{
	struct uart_driver *drv = (struct uart_driver *)tty->driver.driver_state;
	struct uart_info *info;
	int retval, line = MINOR(tty->device) - tty->driver.minor_start;

#ifdef DEBUG
	printk("uart_open(%d) called\n", line);
#endif

	retval = -ENODEV;
	if (line >= tty->driver.num)
		goto fail;

	if (!try_inc_mod_count(drv->owner))
		goto fail;

	info = uart_get(drv, line);
	retval = -ENOMEM;
	if (!info)
		goto out;

	/*
	 * Set the tty driver_data.  If we fail from this point on,
	 * the generic tty layer will cause uart_close(), which will
	 * decrement the module use count.
	 */
	tty->driver_data = info;
	info->tty = tty;
	info->tty->low_latency = (info->flags & ASYNC_LOW_LATENCY) ? 1 : 0;

	if (uart_alloc_tmpbuf())
		goto fail;

	/*
	 * If the port is in the middle of closing, bail out now.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ASYNC_CLOSING)) {
		if (info->flags & ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
		retval = (info->flags & ASYNC_HUP_NOTIFY) ?
			-EAGAIN : -ERESTARTSYS;
		goto fail;
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
		goto fail;

	retval = uart_block_til_ready(tty, filp, info);
	if (retval)
		goto fail;

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
			struct console *c = drv->cons;
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
	if (drv->owner)
		__MOD_DEC_USE_COUNT(drv->owner);
fail:
	return retval;
}

#ifdef CONFIG_PROC_FS

static const char *uart_type(struct uart_port *port)
{
	const char *str = NULL;

	if (port->ops->type)
		str = port->ops->type(port);

	if (!str)
		str = "unknown";

	return str;
}

static int uart_line_info(char *buf, struct uart_driver *drv, int i)
{
	struct uart_state *state = drv->state + i;
	struct uart_port *port = state->port;
	char stat_buf[32];
	u_int status;
	int ret;

	ret = sprintf(buf, "%d: uart:%s port:%08X irq:%d",
			port->line, uart_type(port),
			port->iobase, port->irq);

	if (port->type == PORT_UNKNOWN) {
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

#define INFOBIT(bit,str) \
	if (state->info && state->info->mctrl & (bit)) \
		strcat(stat_buf, (str))
#define STATBIT(bit,str) \
	if (status & (bit)) \
		strcat(stat_buf, (str))

	stat_buf[0] = '\0';
	stat_buf[1] = '\0';
	INFOBIT(TIOCM_RTS, "|RTS");
	STATBIT(TIOCM_CTS, "|CTS");
	INFOBIT(TIOCM_DTR, "|DTR");
	STATBIT(TIOCM_DSR, "|DSR");
	STATBIT(TIOCM_CAR, "|CD");
	STATBIT(TIOCM_RNG, "|RI");
	if (stat_buf[0])
		stat_buf[0] = ' ';
	strcat(stat_buf, "\n");

	ret += sprintf(buf + ret, stat_buf);
	return ret;
}

static int uart_read_proc(char *page, char **start, off_t off,
			  int count, int *eof, void *data)
{
	struct tty_driver *ttydrv = data;
	struct uart_driver *drv = ttydrv->driver_state;
	int i, len = 0, l;
	off_t begin = 0;

	len += sprintf(page, "serinfo:1.0 driver%s%s revision:%s\n",
			"", "", "");
	for (i = 0; i < drv->nr && len < PAGE_SIZE - 96; i++) {
		l = uart_line_info(page + len, drv, i);
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
struct uart_port * __init
uart_get_console(struct uart_port *ports, int nr, struct console *co)
{
	int idx = co->index;

	if (idx < 0 || idx >= nr || (ports[idx].iobase == 0 &&
				     ports[idx].membase == NULL))
		for (idx = 0; idx < nr; idx++)
			if (ports[idx].iobase != 0 ||
			    ports[idx].membase != NULL)
				break;

	co->index = idx;

	return ports + idx;
}

/**
 *	uart_parse_options - Parse serial port baud/parity/bits/flow contro.
 *	@options: pointer to option string
 *	@baud: pointer to an 'int' variable for the baud rate.
 *	@parity: pointer to an 'int' variable for the parity.
 *	@bits: pointer to an 'int' variable for the number of data bits.
 *	@flow: pointer to an 'int' variable for the flow control character.
 *
 *	uart_parse_options decodes a string containing the serial console
 *	options.  The format of the string is <baud><parity><bits><flow>,
 *	eg: 115200n8r
 */
void __init
uart_parse_options(char *options, int *baud, int *parity, int *bits, int *flow)
{
	char *s = options;

	*baud = simple_strtoul(s, NULL, 10);
	while (*s >= '0' && *s <= '9')
		s++;
	if (*s)
		*parity = *s++;
	if (*s)
		*bits = *s++ - '0';
	if (*s)
		*flow = *s;
}

/**
 *	uart_set_options - setup the serial console parameters
 *	@port: pointer to the serial ports uart_port structure
 *	@co: console pointer
 *	@baud: baud rate
 *	@parity: parity character - 'n' (none), 'o' (odd), 'e' (even)
 *	@bits: number of data bits
 *	@flow: flow control character - 'r' (rts)
 */
int __init
uart_set_options(struct uart_port *port, struct console *co,
		 int baud, int parity, int bits, int flow)
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
	quot = (port->uartclk / (16 * baud));
	port->ops->change_speed(port, cflag, 0, quot);

	return 0;
}

extern void ambauart_console_init(void);
extern void anakin_console_init(void);
extern void clps711xuart_console_init(void);
extern void rs285_console_init(void);
extern void sa1100_rs_console_init(void);
extern void serial8250_console_init(void);
extern void uart00_console_init(void);

/*
 * Central "initialise all serial consoles" container.  Needs to be killed.
 */
void __init uart_console_init(void)
{
#ifdef CONFIG_SERIAL_AMBA_CONSOLE
	ambauart_console_init();
#endif
#ifdef CONFIG_SERIAL_ANAKIN_CONSOLE
	anakin_console_init();
#endif
#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE
	clps711xuart_console_init();
#endif
#ifdef CONFIG_SERIAL_21285_CONSOLE
	rs285_console_init();
#endif
#ifdef CONFIG_SERIAL_SA1100_CONSOLE
	sa1100_rs_console_init();
#endif
#ifdef CONFIG_SERIAL_8250_CONSOLE
	serial8250_console_init();
#endif
#ifdef CONFIG_SERIAL_UART00_CONSOLE
	uart00_console_init();
#endif
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
static int uart_pm_set_state(struct uart_state *state, int pm_state, int oldstate)
{
	struct uart_port *port = state->port;
	struct uart_ops *ops = port->ops;
	int running = state->info &&
		      state->info->flags & ASYNC_INITIALIZED;

	if (port->type == PORT_UNKNOWN)
		return 0;

//printk("pm: %08x: %d -> %d, %srunning\n", port->iobase, dev->state, pm_state, running ? "" : "not ");
	if (pm_state == 0) {
		if (ops->pm)
			ops->pm(port, pm_state, oldstate);
		if (running) {
			ops->set_mctrl(port, 0);
			ops->startup(port, state->info);
			uart_change_speed(state->info, NULL);
			ops->set_mctrl(port, state->info->mctrl);
			ops->start_tx(port, 1, 0);
		}

		/*
		 * Re-enable the console device after suspending.
		 */
		if (state->cons && state->cons->index == port->line)
			state->cons->flags |= CON_ENABLED;
	} else if (pm_state == 1) {
		if (ops->pm)
			ops->pm(port, pm_state, oldstate);
	} else {
		/*
		 * Disable the console device before suspending.
		 */
		if (state->cons && state->cons->index == port->line)
			state->cons->flags &= ~CON_ENABLED;

		if (running) {
			ops->stop_tx(port, 0);
			ops->set_mctrl(port, 0);
			ops->stop_rx(port);
			ops->shutdown(port, state->info);
		}
		if (ops->pm)
			ops->pm(port, pm_state, oldstate);
	}
	return 0;
}

/*
 *  Wakeup support.
 */
static int uart_pm_set_wakeup(struct uart_state *state, int data)
{
	int err = 0;

	if (state->port->ops->set_wake)
		err = state->port->ops->set_wake(state->port, data);

	return err;
}

static int uart_pm(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	struct uart_state *state = dev->data;
	int err = 0;

	switch (rqst) {
	case PM_SUSPEND:
	case PM_RESUME:
		err = uart_pm_set_state(state, (int)data, dev->state);
		break;

	case PM_SET_WAKEUP:
		err = uart_pm_set_wakeup(state, (int)data);
		break;
	}
	return err;
}
#endif

static inline void
uart_report_port(struct uart_driver *drv, struct uart_port *port)
{
	printk("%s%d at ", drv->normal_name, port->line);
	switch (port->iotype) {
	case SERIAL_IO_PORT:
		printk("I/O 0x%x", port->iobase);
		break;
	case SERIAL_IO_HUB6:
		printk("I/O 0x%x offset 0x%x", port->iobase, port->hub6);
		break;
	case SERIAL_IO_MEM:
		printk("MEM 0x%x", port->mapbase);
		break;
	}
	printk(" (irq = %d) is a %s\n", port->irq, uart_type(port));
}

static void
uart_setup_port(struct uart_driver *drv, struct uart_state *state)
{
	struct uart_port *port = state->port;
	int flags = UART_CONFIG_TYPE;

	init_MUTEX(&state->count_sem);

	state->close_delay	= 5 * HZ / 10;
	state->closing_wait	= 30 * HZ;

	port->type = PORT_UNKNOWN;

#ifdef CONFIG_PM
	state->cons = drv->cons;
	state->pm = pm_register(PM_SYS_DEV, PM_SYS_COM, uart_pm);
	if (state->pm)
		state->pm->data = state;
#endif

	/*
	 * If there isn't a port here, don't do anything further.
	 */
	if (!port->iobase && !port->mapbase)
		return;

	/*
	 * Now do the auto configuration stuff.  Note that config_port
	 * is expected to claim the resources and map the port for us.
	 */
	if (port->flags & ASYNC_AUTO_IRQ)
		flags |= UART_CONFIG_IRQ;
	if (port->flags & ASYNC_BOOT_AUTOCONF)
		port->ops->config_port(port, flags);

	/*
	 * Only register this port if it is detected.
	 */
	if (port->type != PORT_UNKNOWN) {
		tty_register_devfs(drv->normal_driver, 0, drv->minor +
					state->port->line);
		tty_register_devfs(drv->callout_driver, 0, drv->minor +
					state->port->line);
		uart_report_port(drv, port);
	}

#ifdef CONFIG_PM
	/*
	 * Power down all ports by default, except the console if we have one.
	 */
	if (state->pm && (!drv->cons || port->line != drv->cons->index))
		pm_send(state->pm, PM_SUSPEND, (void *)3);
#endif
}

/*
 * Register a set of ports with the core driver.  Note that we don't
 * printk any information about the ports; that is up to the low level
 * driver to do if they so wish.
 */
int uart_register_driver(struct uart_driver *drv)
{
	struct tty_driver *normal, *callout;
	int i, retval;

	if (drv->state)
		panic("drv->state already allocated\n");

	/*
	 * Maybe we should be using a slab cache for this, especially if
	 * we have a large number of ports to handle.  Note that we also
	 * allocate space for an integer for reference counting.
	 */
	drv->state = kmalloc(sizeof(struct uart_state) * drv->nr +
			     sizeof(int), GFP_KERNEL);
	retval = -ENOMEM;
	if (!drv->state)
		goto out;

	memset(drv->state, 0, sizeof(struct uart_state) * drv->nr +
			sizeof(int));

	normal  = drv->normal_driver;
	callout = drv->callout_driver;

	normal->magic		= TTY_DRIVER_MAGIC;
	normal->driver_name	= drv->normal_name;
	normal->name		= drv->normal_name;
	normal->major		= drv->normal_major;
	normal->minor_start	= drv->minor;
	normal->num		= drv->nr;
	normal->type		= TTY_DRIVER_TYPE_SERIAL;
	normal->subtype		= SERIAL_TYPE_NORMAL;
	normal->init_termios	= tty_std_termios;
	normal->init_termios.c_cflag = B38400 | CS8 | CREAD | HUPCL | CLOCAL;
	normal->flags		= TTY_DRIVER_REAL_RAW | TTY_DRIVER_NO_DEVFS;
	normal->refcount	= (int *)(drv->state + drv->nr);
	normal->table		= drv->table;
	normal->termios		= drv->termios;
	normal->termios_locked	= drv->termios_locked;
	normal->driver_state    = drv;

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
#ifdef CONFIG_PROC_FS
	normal->read_proc	= uart_read_proc;
#endif

	/*
	 * The callout device is just like the normal device except for
	 * the major number and the subtype code.
	 */
	*callout		= *normal;
	callout->name		= drv->callout_name;
	callout->major		= drv->callout_major;
	callout->subtype	= SERIAL_TYPE_CALLOUT;
	callout->read_proc	= NULL;
	callout->proc_entry	= NULL;

	for (i = 0; i < drv->nr; i++) {
		struct uart_state *state = drv->state + i;

		state->callout_termios	= callout->init_termios;
		state->normal_termios	= normal->init_termios;
		state->port		= drv->port + i;
		state->port->line	= i;

		uart_setup_port(drv, state);
	}

	retval = tty_register_driver(normal);
	if (retval)
		goto out;

	retval = tty_register_driver(callout);
	if (retval)
		tty_unregister_driver(normal);

out:
	if (retval && drv->state)
		kfree(drv->state);
	return retval;
}

void uart_unregister_driver(struct uart_driver *drv)
{
	int i;

	for (i = 0; i < drv->nr; i++) {
		struct uart_state *state = drv->state + i;

		if (state->info && state->info->tty)
			tty_hangup(state->info->tty);

		pm_unregister(state->pm);

		if (state->port->type != PORT_UNKNOWN)
			state->port->ops->release_port(state->port);
		if (state->info) {
			tasklet_kill(&state->info->tlet);
			kfree(state->info);
		}
	}

	tty_unregister_driver(drv->normal_driver);
	tty_unregister_driver(drv->callout_driver);

	kfree(drv->state);
}

static int uart_match_port(struct uart_port *port1, struct uart_port *port2)
{
	if (port1->iotype != port2->iotype)
		return 0;
	switch (port1->iotype) {
	case SERIAL_IO_PORT:	return (port1->iobase == port2->iobase);
	case SERIAL_IO_MEM:	return (port1->membase == port2->membase);
	}
	return 0;
}

/**
 *	uart_register_port: register a port with the generic uart driver
 *	@reg: pointer to the uart low level driver structure for this port
 *	@port: uart port structure describing the port
 *
 *	Register a UART with the specified low level driver.  Detect the
 *	type of the port if ASYNC_BOOT_AUTOCONF is set, and detect the IRQ
 *	if ASYNC_AUTO_IRQ is set.
 *
 *	Returns negative error, or positive line number.
 */
int uart_register_port(struct uart_driver *drv, struct uart_port *port)
{
	struct uart_state *state = NULL;
	int i, flags = UART_CONFIG_TYPE;

	/*
	 * First, find a port entry which matches.  Note: if we do
	 * find a matching entry, and it has a non-zero use count,
	 * then we can't register the port.
	 */
	down(&port_sem);
	for (i = 0; i < drv->nr; i++) {
		if (uart_match_port(drv->state[i].port, port)) {
			down(&drv->state[i].count_sem);
			state = &drv->state[i];
			break;
		}
	}

	/*
	 * If we didn't find a matching entry, look for the first
	 * free entry.  We look for one which hasn't been previously
	 * used (indicated by zero iobase).
	 */
	if (!state) {
		for (i = 0; i < drv->nr; i++) {
			if (drv->state[i].port->type == PORT_UNKNOWN &&
			    drv->state[i].port->iobase == 0) {
				down(&drv->state[i].count_sem);
				if (drv->state[i].count == 0) {
					state = &drv->state[i];
					break;
				}
			}
		}
	}

	/*
	 * Ok, that also failed.  Find the first unused entry, which
	 * may be previously in use.
	 */
	if (!state) {
		for (i = 0; i < drv->nr; i++) {
			if (drv->state[i].port->type == PORT_UNKNOWN) {
				down(&drv->state[i].count_sem);
				if (drv->state[i].count == 0) {
					state = &drv->state[i];
					break;
				}
			}
		}
	}

	up(&port_sem);

	if (!state)
		return -ENOSPC;

	/*
	 * If we find a port that matches this one, and it appears to
	 * be in-use (even if it doesn't have a type) we shouldn't alter
	 * it underneath itself - the port may be open and trying to do
	 * useful work.
	 */
	if (state->count != 0 ||
	    (state->info && state->info->blocked_open != 0)) {
		up(&state->count_sem);
		return -EBUSY;
	}

	/*
	 * We're holding the lock for this port.  Copy the relevant data
	 * into the port structure.
	 */
	state->port->iobase   = port->iobase;
	state->port->membase  = port->membase;
	state->port->irq      = port->irq;
	state->port->uartclk  = port->uartclk;
	state->port->fifosize = port->fifosize;
	state->port->regshift = port->regshift;
	state->port->iotype   = port->iotype;
	state->port->flags    = port->flags;

#if 0 //def CONFIG_PM
	/* we have already registered the power management handlers */
	state->pm = pm_register(PM_SYS_DEV, PM_SYS_COM, uart_pm);
	if (state->pm) {
		state->pm->data = state;

		/*
		 * Power down all ports by default, except
		 * the console if we have one.
		 */
		if (!drv->cons || state->port->line != drv->cons->index)
			pm_send(state->pm, PM_SUSPEND, (void *)3);
	}
#endif

	if (state->port->flags & ASYNC_AUTO_IRQ)
		flags |= UART_CONFIG_IRQ;
	if (state->port->flags & ASYNC_BOOT_AUTOCONF)
		state->port->ops->config_port(state->port, flags);

	tty_register_devfs(drv->normal_driver, 0, drv->minor +
					state->port->line);
	tty_register_devfs(drv->callout_driver, 0, drv->minor +
					state->port->line);

	uart_report_port(drv, state->port);

	up(&state->count_sem);
	return i;
}

/*
 * Unregister the specified port index on the specified driver.
 */
void uart_unregister_port(struct uart_driver *drv, int line)
{
	struct uart_state *state;

	if (line < 0 || line >= drv->nr) {
		printk(KERN_ERR "Attempt to unregister %s%d\n",
			drv->normal_name, line);
		return;
	}

	state = drv->state + line;

	down(&state->count_sem);
	/*
	 * The port has already gone.  We have to hang up the line
	 * to kill all usage of this port.
	 */
	if (state->info && state->info->tty)
		tty_hangup(state->info->tty);

	/*
	 * Free the ports resources, if any.
	 */
	state->port->ops->release_port(state->port);

	/*
	 * Indicate that there isn't a port here anymore.
	 */
	state->port->type = PORT_UNKNOWN;

#if 0 // not yet
	/*
	 * No point in doing power management for hardware that
	 * isn't present.
	 */
	pm_unregister(state->pm);
#endif

	/*
	 * Remove the devices from devfs
	 */
	tty_unregister_devfs(drv->normal_driver, drv->minor + line);
	tty_unregister_devfs(drv->callout_driver, drv->minor + line);
	up(&state->count_sem);
}

EXPORT_SYMBOL(uart_event);
EXPORT_SYMBOL(uart_register_driver);
EXPORT_SYMBOL(uart_unregister_driver);
EXPORT_SYMBOL(uart_register_port);
EXPORT_SYMBOL(uart_unregister_port);

static int __init uart_init(void)
{
	return 0;
}

static void __exit uart_exit(void)
{
	free_page((unsigned long)tmp_buf);
	tmp_buf = NULL;
}

module_init(uart_init);
module_exit(uart_exit);

MODULE_DESCRIPTION("Serial driver core");
MODULE_LICENSE("GPL");
