/*
 *  linux/drivers/char/serial_clps711x.c
 *
 *  Driver for CLPS711x serial ports
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
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>

#include <asm/bitops.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/serial_core.h>

#include <asm/hardware/clps7111.h>

#define UART_NR		2

#define SERIAL_CLPS711X_NAME	"ttyAM"
#define SERIAL_CLPS711X_MAJOR	204
#define SERIAL_CLPS711X_MINOR	16
#define SERIAL_CLPS711X_NR	UART_NR

#define CALLOUT_CLPS711X_NAME	"cuaam"
#define CALLOUT_CLPS711X_MAJOR	205
#define CALLOUT_CLPS711X_MINOR	16
#define CALLOUT_CLPS711X_NR	UART_NR

static struct tty_driver normal, callout;
static struct tty_struct *clps711x_table[UART_NR];
static struct termios *clps711x_termios[UART_NR], *clps711x_termios_locked[UART_NR];

/*
 * We use the relevant SYSCON register as a base address for these ports.
 */
#define UBRLCR(port)		((port)->iobase + UBRLCR1 - SYSCON1)
#define UARTDR(port)		((port)->iobase + UARTDR1 - SYSCON1)
#define SYSFLG(port)		((port)->iobase + SYSFLG1 - SYSCON1)
#define SYSCON(port)		((port)->iobase + SYSCON1 - SYSCON1)

#define TX_IRQ(port)		((port)->irq)
#define RX_IRQ(port)		((port)->irq + 1)

#define UART_ANY_ERR		(UARTDR_FRMERR | UARTDR_PARERR | UARTDR_OVERR)

static void clps711xuart_stop_tx(struct uart_port *port, u_int from_tty)
{
	disable_irq(TX_IRQ(port));
}

static void clps711xuart_start_tx(struct uart_port *port, u_int nonempty, u_int from_tty)
{
	if (nonempty)
		enable_irq(TX_IRQ(port));
}

static void clps711xuart_stop_rx(struct uart_port *port)
{
	disable_irq(RX_IRQ(port));
}

static void clps711xuart_enable_ms(struct uart_port *port)
{
}

#if 0
static void ambauart_modem_status(struct uart_info *info)
{
	unsigned int status, delta;
	struct uart_icount *icount = &info->port->icount;

	UART_PUT_ICR(info->port, 0);

	status = UART_GET_FR(info->port) & AMBA_UARTFR_MODEM_ANY;

	delta = status ^ info->port->old_status;
	info->port->old_status = status;

	if (!delta)
		return;

	if (delta & AMBA_UARTFR_DCD)
		uart_handle_dcd_change(info, status & AMBA_UARTFR_DCD);

	if (delta & AMBA_UARTFR_DSR)
		icount->dsr++;

	if (delta & AMBA_UARTFR_CTS)
		uart_handle_cts_change(info, status & AMBA_UARTFR_CTS);

	wake_up_interruptible(&info->delta_msr_wait);
}
#endif

static void clps711xuart_int_rx(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	struct tty_struct *tty = info->tty;
	unsigned int status, ch, flg, ignored = 0;
	struct uart_port *port = info->port;

	status = clps_readl(SYSFLG(port));
	while (!(status & SYSFLG_URXFE)) {
		ch = clps_readl(UARTDR(port));

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;
		port->icount.rx++;

		flg = TTY_NORMAL;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		if (ch & UART_ANY_ERR)
			goto handle_error;

		if (uart_handle_sysrq_char(info, ch, regs))
			goto ignore_char;

	error_return:
		*tty->flip.flag_buf_ptr++ = flg;
		*tty->flip.char_buf_ptr++ = ch;
		tty->flip.count++;
	ignore_char:
		status = clps_readl(SYSFLG(port));
	}
out:
	tty_flip_buffer_push(tty);
	return;

handle_error:
	if (ch & UARTDR_PARERR)
		port->icount.parity++;
	else if (ch & UARTDR_FRMERR)
		port->icount.frame++;
	if (ch & UARTDR_OVERR)
		port->icount.overrun++;

	if (ch & port->ignore_status_mask) {
		if (++ignored > 100)
			goto out;
		goto ignore_char;
	}
	ch &= port->read_status_mask;

	if (ch & UARTDR_PARERR)
		flg = TTY_PARITY;
	else if (ch & UARTDR_FRMERR)
		flg = TTY_FRAME;

	if (ch & UARTDR_OVERR) {
		/*
		 * CHECK: does overrun affect the current character?
		 * ASSUMPTION: it does not.
		 */
		*tty->flip.flag_buf_ptr++ = flg;
		*tty->flip.char_buf_ptr++ = ch;
		tty->flip.count++;
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;
		ch = 0;
		flg = TTY_OVERRUN;
	}
#ifdef SUPPORT_SYSRQ
	info->sysrq = 0;
#endif
	goto error_return;
}

static void clps711xuart_int_tx(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	struct uart_port *port = info->port;
	int count;

	if (port->x_char) {
		clps_writel(port->x_char, UARTDR(port));
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (info->xmit.head == info->xmit.tail
	    || info->tty->stopped
	    || info->tty->hw_stopped) {
		clps711xuart_stop_tx(info->port, 0);
		return;
	}

	count = port->fifosize >> 1;
	do {
		clps_writel(info->xmit.buf[info->xmit.tail], UARTDR(port));
		info->xmit.tail = (info->xmit.tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (info->xmit.head == info->xmit.tail)
			break;
	} while (--count > 0);

	if (CIRC_CNT(info->xmit.head,
		     info->xmit.tail,
		     UART_XMIT_SIZE) < WAKEUP_CHARS)
		uart_event(info, EVT_WRITE_WAKEUP);

	if (info->xmit.head == info->xmit.tail)
		clps711xuart_stop_tx(info->port, 0);
}

static u_int clps711xuart_tx_empty(struct uart_port *port)
{
	u_int status = clps_readl(SYSFLG(port));
	return status & SYSFLG_UBUSY ? 0 : TIOCSER_TEMT;
}

static u_int clps711xuart_get_mctrl(struct uart_port *port)
{
	unsigned int port_addr;
	unsigned int result = 0;
	unsigned int status;

	port_addr = SYSFLG(port);
	if (port_addr == SYSFLG1) {
		status = clps_readl(SYSFLG1);
		if (status & SYSFLG1_DCD)
			result |= TIOCM_CAR;
		if (status & SYSFLG1_DSR)
			result |= TIOCM_DSR;
		if (status & SYSFLG1_CTS)
			result |= TIOCM_CTS;
	}

	return result;
}

static void clps711xuart_set_mctrl_null(struct uart_port *port, u_int mctrl)
{
}

static void clps711xuart_break_ctl(struct uart_port *port, int break_state)
{
	unsigned int ubrlcr;

	ubrlcr = clps_readl(UBRLCR(port));
	if (break_state == -1)
		ubrlcr |= UBRLCR_BREAK;
	else
		ubrlcr &= ~UBRLCR_BREAK;
	clps_writel(ubrlcr, UBRLCR(port));
}

static int clps711xuart_startup(struct uart_port *port, struct uart_info *info)
{
	u_int syscon;
	int retval;

	/*
	 * Allocate the IRQs
	 */
	retval = request_irq(TX_IRQ(port), clps711xuart_int_tx, 0,
			     "clps711xuart_tx", info);
	if (retval)
		return retval;

	retval = request_irq(RX_IRQ(port), clps711xuart_int_rx, 0,
			     "clps711xuart_rx", info);
	if (retval) {
		free_irq(TX_IRQ(port), info);
		return retval;
	}

	port->ops->set_mctrl(port, info->mctrl);

	/*
	 * enable the port
	 */
	syscon = clps_readl(SYSCON(port));
	syscon |= SYSCON_UARTEN;
	clps_writel(syscon, SYSCON(port));

	return 0;
}

static void clps711xuart_shutdown(struct uart_port *port, struct uart_info *info)
{
	u_int ubrlcr, syscon;

	/*
	 * Free the interrupt
	 */
	free_irq(TX_IRQ(port), info);	/* TX interrupt */
	free_irq(RX_IRQ(port), info);	/* RX interrupt */

	/*
	 * disable the port
	 */
	syscon = clps_readl(SYSCON(port));
	syscon &= ~SYSCON_UARTEN;
	clps_writel(syscon, SYSCON(port));

	/*
	 * disable break condition and fifos
	 */
	ubrlcr = clps_readl(UBRLCR(port));
	ubrlcr &= ~(UBRLCR_FIFOEN | UBRLCR_BREAK);
	clps_writel(ubrlcr, UBRLCR(port));
}

static void clps711xuart_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	u_int ubrlcr;
	unsigned long flags;

#if DEBUG
	printk("clps711xuart_change_speed(cflag=0x%x, iflag=0x%x, quot=%d) called\n",
		cflag, iflag, quot);
#endif
	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5: ubrlcr = UBRLCR_WRDLEN5; break;
	case CS6: ubrlcr = UBRLCR_WRDLEN6; break;
	case CS7: ubrlcr = UBRLCR_WRDLEN7; break;
	default:  ubrlcr = UBRLCR_WRDLEN8; break; // CS8
	}
	if (cflag & CSTOPB)
		ubrlcr |= UBRLCR_XSTOP;
	if (cflag & PARENB) {
		ubrlcr |= UBRLCR_PRTEN;
		if (!(cflag & PARODD))
			ubrlcr |= UBRLCR_EVENPRT;
	}
	if (port->fifosize > 1)
		ubrlcr |= UBRLCR_FIFOEN;

	port->read_status_mask = UARTDR_OVERR;
	if (iflag & INPCK)
		port->read_status_mask |= UARTDR_PARERR | UARTDR_FRMERR;
//	if (iflag & (BRKINT | PARMRK))
//		port->read_status_mask |= AMBA_UARTRSR_BE;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (iflag & IGNPAR)
		port->ignore_status_mask |= UARTDR_FRMERR | UARTDR_PARERR;
	if (iflag & IGNBRK) {
//		port->ignore_status_mask |= AMBA_UARTRSR_BE;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns to (for real raw support).
		 */
		if (iflag & IGNPAR)
			port->ignore_status_mask |= UARTDR_OVERR;
	}

	quot -= 1;

	/* first, disable everything */
	save_flags(flags); cli();

	clps_writel(ubrlcr | quot, UBRLCR(port));

	restore_flags(flags);
}

static const char *clps711xuart_type(struct uart_port *port)
{
	return port->type == PORT_CLPS711X ? "CLPS711x" : NULL;
}

/*
 * Configure/autoconfigure the port.
 */
static void clps711xuart_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE)
		port->type = PORT_CLPS711X;
}

static void clps711xuart_release_port(struct uart_port *port)
{
}

static int clps711xuart_request_port(struct uart_port *port)
{
	return 0;
}

static struct uart_ops clps711x_pops = {
	tx_empty:	clps711xuart_tx_empty,
	set_mctrl:	clps711xuart_set_mctrl_null,
	get_mctrl:	clps711xuart_get_mctrl,
	stop_tx:	clps711xuart_stop_tx,
	start_tx:	clps711xuart_start_tx,
	stop_rx:	clps711xuart_stop_rx,
	enable_ms:	clps711xuart_enable_ms,
	break_ctl:	clps711xuart_break_ctl,
	startup:	clps711xuart_startup,
	shutdown:	clps711xuart_shutdown,
	change_speed:	clps711xuart_change_speed,
	type:		clps711xuart_type,
	config_port:	clps711xuart_config_port,
	release_port:	clps711xuart_release_port,
	request_port:	clps711xuart_request_port,
};

static struct uart_port clps711x_ports[UART_NR] = {
	{
		iobase:		SYSCON1,
		irq:		IRQ_UTXINT1, /* IRQ_URXINT1, IRQ_UMSINT */
		uartclk:	3686400,
		fifosize:	16,
		ops:		&clps711x_pops,
		flags:		ASYNC_BOOT_AUTOCONF,
	},
	{
		iobase:		SYSCON2,
		irq:		IRQ_UTXINT2, /* IRQ_URXINT2 */
		uartclk:	3686400,
		fifosize:	16,
		ops:		&clps711x_pops,
		flags:		ASYNC_BOOT_AUTOCONF,
	}
};

#ifdef CONFIG_SERIAL_CLPS711X_CONSOLE

#ifdef used_and_not_const_char_pointer
/*
 * This code is currently never used; console->read is never called.
 * Therefore, although we have an implementation, we don't use it.
 * FIXME: the "const char *s" should be fixed to "char *s" some day.
 * (when the definition in include/linux/console.h is also fixed)
 */
static int clps711xuart_console_read(struct uart_port *port, char *s, u_int count)
{
	u_int status;
	int c;
#if DEBUG
	printk("clps711xuart_console_read() called\n");
#endif

	c = 0;
	while (c < count) {
		status = clps_readl(SYSFLG(port));
		if (status & SYSFLG_URXFE) {
			// nothing more to get, return
			return c;
		} else {
			*s++ = clps_readl(UARTDR(port));
			c++;
		}
	}
	// return the count
	return c;
}
#endif

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 *
 *	Note that this is called with interrupts already disabled
 */
static void clps711xuart_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = clps711x_ports + co->index;
	unsigned int status, syscon;
	int i;

	/*
	 *	Ensure that the port is enabled.
	 */
	syscon = clps_readl(SYSCON(port));
	clps_writel(syscon | SYSCON_UARTEN, SYSCON(port));

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = clps_readl(SYSFLG(port));
		} while (status & SYSFLG_UTXFF);
		clps_writel(s[i], UARTDR(port));
		if (s[i] == '\n') {
			do {
				status = clps_readl(SYSFLG(port));
			} while (status & SYSFLG_UTXFF);
			clps_writel('\r', UARTDR(port));
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the uart state.
	 */
	do {
		status = clps_readl(SYSFLG(port));
	} while (status & SYSFLG_UBUSY);

	clps_writel(syscon, SYSCON(port));
}

static kdev_t clps711xuart_console_device(struct console *co)
{
	return MKDEV(SERIAL_CLPS711X_MAJOR, SERIAL_CLPS711X_MINOR + co->index);
}

/*
 *	Receive character from the serial port
 *	  what about interrupts?
 */
static int clps711xuart_console_wait_key(struct console *co)
{
	struct uart_port *port = clps711x_ports + co->index;
	u_int status, syscon, ch;

	/*
	 *	Ensure that the port is enabled.
	 */
	syscon = clps_readl(SYSCON(port));
	clps_writel(syscon | SYSCON_UARTEN, SYSCON(port));

	do {
		status = clps_readl(SYSFLG(port));
	} while (status & SYSFLG_URXFE);
	ch = clps_readl(UARTDR(port)) & 0xff;

	clps_writel(syscon, SYSCON(port));

	return ch;
}

static void __init
clps711xuart_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	if (clps_readl(SYSCON(port)) & SYSCON_UARTEN) {
		u_int ubrlcr, quot;

		ubrlcr = clps_readl(UBRLCR(port));

		*parity = 'n';
		if (ubrlcr & UBRLCR_PRTEN) {
			if (ubrlcr & UBRLCR_EVENPRT)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if ((ubrlcr & UBRLCR_WRDLEN_MASK) == UBRLCR_WRDLEN7)
			*bits = 7;
		else
			*bits = 8;

		quot = ubrlcr & UBRLCR_BAUD_MASK;
		*baud = port->uartclk / (16 * (quot + 1));
	}
}

static int __init clps711xuart_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(clps711x_ports, UART_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		clps711xuart_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console clps711x_console = {
	name:		SERIAL_CLPS711X_NAME,
	write:		clps711xuart_console_write,
#ifdef used_and_not_const_char_pointer
	read:		clps711xuart_console_read,
#endif
	device:		clps711xuart_console_device,
	wait_key:	clps711xuart_console_wait_key,
	setup:		clps711xuart_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};

void __init clps711xuart_console_init(void)
{
	register_console(&clps711x_console);
}

#define CLPS711X_CONSOLE	&clps711x_console
#else
#define CLPS711X_CONSOLE	NULL
#endif

static struct uart_driver clps711x_reg = {
#ifdef CONFIG_DEVFS_FS
	normal_name:		SERIAL_CLPS711X_NAME,
	callout_name:		CALLOUT_CLPS711X_NAME,
#else
	normal_name:		SERIAL_CLPS711X_NAME,
	callout_name:		CALLOUT_CLPS711X_NAME,
#endif

	normal_major:		SERIAL_CLPS711X_MAJOR,
	normal_driver:		&normal,
	callout_major:		CALLOUT_CLPS711X_MAJOR,
	callout_driver:		&callout,

	table:			clps711x_table,
	termios:		clps711x_termios,
	termios_locked:		clps711x_termios_locked,

	minor:			SERIAL_CLPS711X_MINOR,
	nr:			UART_NR,

	port:			clps711x_ports,
	cons:			CLPS711X_CONSOLE,
};

static int __init clps711xuart_init(void)
{
	return uart_register_driver(&clps711x_reg);
}

static void __exit clps711xuart_exit(void)
{
	uart_unregister_driver(&clps711x_reg);
}

module_init(clps711xuart_init);
module_exit(clps711xuart_exit);

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Deep Blue Solutions Ltd");
MODULE_DESCRIPTION("CLPS-711x generic serial driver");
MODULE_LICENSE("GPL");
