/*
 *  linux/drivers/char/serial_anakin.c
 *
 *  Based on driver for AMBA serial ports, by ARM Limited,
 *  Deep Blue Solutions Ltd., Linus Torvalds and Theodore Ts'o.
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
 *
 *  Copyright (C) 2001 Blue Mug, Inc. for Acunia N.V.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   20-Apr-2001 TTC	Created
 *   05-May-2001 W/TTC	Updated for serial_core.c
 *   27-Jun-2001 jonm	Minor changes; add mctrl support, switch to 
 *   			SA_INTERRUPT. Works reliably now. No longer requires
 *   			changes to the serial_core API.
 *
 *  $Id$
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

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <linux/serial_core.h>

#include <asm/arch/serial_reg.h>

#define UART_NR			5

#define SERIAL_ANAKIN_NAME	"ttyAN"
#define SERIAL_ANAKIN_MAJOR	204
#define SERIAL_ANAKIN_MINOR	32

#define CALLOUT_ANAKIN_NAME	"cuaan"
#define CALLOUT_ANAKIN_MAJOR	205
#define CALLOUT_ANAKIN_MINOR	32

static struct tty_driver normal, callout;
static struct tty_struct *anakin_table[UART_NR];
static struct termios *anakin_termios[UART_NR], *anakin_termios_locked[UART_NR];
static struct uart_state anakin_state[UART_NR];
static u_int txenable[NR_IRQS];		/* Software interrupt register */

static inline unsigned int
anakin_in(struct uart_port *port, u_int offset)
{
	return __raw_readl(port->base + offset);
}

static inline void
anakin_out(struct uart_port *port, u_int offset, unsigned int value)
{
	__raw_writel(value, port->base + offset);
}

static void
anakin_stop_tx(struct uart_port *port, u_int from_tty)
{
	txenable[port->irq] = 0;
}

static inline void
anakin_transmit_buffer(struct uart_info *info)
{
	struct uart_port *port = info->port;

	while (!(anakin_in(port, 0x10) & TXEMPTY));
	anakin_out(port, 0x14, info->xmit.buf[info->xmit.tail]);
	anakin_out(port, 0x18, anakin_in(port, 0x18) | SENDREQUEST);
	info->xmit.tail = (info->xmit.tail + 1) & (UART_XMIT_SIZE-1);
        info->state->icount.tx++;

	if (info->xmit.head == info->xmit.tail)
		anakin_stop_tx(port, 0); 
}

static inline void
anakin_transmit_x_char(struct uart_info *info)
{
	struct uart_port *port = info->port;

	anakin_out(port, 0x14, info->x_char);
	anakin_out(port, 0x18, anakin_in(port, 0x18) | SENDREQUEST);
	info->state->icount.tx++;
	info->x_char = 0;
}

static void
anakin_start_tx(struct uart_port *port, u_int nonempty, u_int from_tty)
{
	unsigned int flags;

	save_flags_cli(flags);

	// is it this... or below: if (nonempty
	if (!txenable[port->irq]) {
		txenable[port->irq] = TXENABLE;

		if ((anakin_in(port, 0x10) & TXEMPTY) && nonempty) {
		    anakin_transmit_buffer((struct uart_info*)port->unused);
		}
	}

	restore_flags(flags);
}

static void
anakin_stop_rx(struct uart_port *port)
{
	unsigned long flags;

	save_flags_cli(flags);
	while (anakin_in(port, 0x10) & RXRELEASE) 
	    anakin_in(port, 0x14);
	anakin_out(port, 0x18, anakin_in(port, 0x18) | BLOCKRX);
	restore_flags(flags);
}

static void
anakin_enable_ms(struct uart_port *port)
{
}

static inline void
anakin_rx_chars(struct uart_info *info)
{
	unsigned int ch;
	struct tty_struct *tty = info->tty;

	if (!(anakin_in(info->port, 0x10) & RXRELEASE))
		return;

	ch = anakin_in(info->port, 0x14) & 0xff;

	if (tty->flip.count < TTY_FLIPBUF_SIZE) {
		*tty->flip.char_buf_ptr++ = ch;
		*tty->flip.flag_buf_ptr++ = TTY_NORMAL;
		info->state->icount.rx++;
		tty->flip.count++;
	} 
	tty_flip_buffer_push(tty);
}

static inline void
anakin_overrun_chars(struct uart_info *info)
{
	unsigned int ch;

	ch = anakin_in(info->port, 0x14);
	info->state->icount.overrun++;
}

static inline void
anakin_tx_chars(struct uart_info *info)
{
	if (info->x_char) {
		anakin_transmit_x_char(info);
		return; 
	}

	if (info->xmit.head == info->xmit.tail
	    || info->tty->stopped
	    || info->tty->hw_stopped) {
		anakin_stop_tx(info->port, 0);
		return;
	}

	anakin_transmit_buffer(info);

	if (CIRC_CNT(info->xmit.head,
		     info->xmit.tail,
		     UART_XMIT_SIZE) < WAKEUP_CHARS)
		uart_event(info, EVT_WRITE_WAKEUP);
}

static void
anakin_int(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int status;
	struct uart_info *info = dev_id;

	status = anakin_in(info->port, 0x1c);

	if (status & RX) 
		anakin_rx_chars(info);

	if (status & OVERRUN) 
		anakin_overrun_chars(info);

	if (txenable[info->port->irq] && (status & TX)) 
		anakin_tx_chars(info);
}

static u_int
anakin_tx_empty(struct uart_port *port)
{
	return anakin_in(port, 0x10) & TXEMPTY ? TIOCSER_TEMT : 0;
}

static u_int
anakin_get_mctrl(struct uart_port *port)
{
	unsigned int status = 0;

	status |= (anakin_in(port, 0x10) & CTS ? TIOCM_CTS : 0);
	status |= (anakin_in(port, 0x18) & DCD ? TIOCM_CAR : 0);
	status |= (anakin_in(port, 0x18) & DTR ? TIOCM_DTR : 0);
	status |= (anakin_in(port, 0x18) & RTS ? TIOCM_RTS : 0);
	
	return status;
}

static void
anakin_set_mctrl(struct uart_port *port, u_int mctrl)
{
	unsigned int status;

	status = anakin_in(port, 0x18);

	if (mctrl & TIOCM_RTS) 
		status |= RTS;
	else 
		status &= ~RTS;

	if (mctrl & TIOCM_CAR)
		status |= DCD;
	else 
		status &= ~DCD;

	anakin_out(port, 0x18, status);
}

static void
anakin_break_ctl(struct uart_port *port, int break_state)
{
	unsigned int status;

	status = anakin_in(port, 0x20);

	if (break_state == -1)
		status |= SETBREAK;
	else
		status &= ~SETBREAK;

	anakin_out(port, 0x20, status);
}

static int
anakin_startup(struct uart_port *port, struct uart_info *info)
{
	int retval;
	unsigned int read,write;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, anakin_int, SA_INTERRUPT, "serial_anakin", info);
	if (retval)
		return retval;

	port->ops->set_mctrl(port, info->mctrl);

	/*
	 * initialise the old status of the modem signals
	 */
	port->old_status = 0;

	/*
	 * Finally, disable IRQ and softIRQs for first byte)
	 */
	txenable[port->irq] = 0;
	read = anakin_in(port, 0x18);
	write = (read & ~(RTS | DTR | BLOCKRX)) | IRQENABLE;
	anakin_out(port, 0x18, write);

	/* Store the uart_info pointer so we can reference it in 
	 * anakin_start_tx() */
	port->unused = (u_int)info;
	
	return 0;
}

static void
anakin_shutdown(struct uart_port *port, struct uart_info *info)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, info);

	/*
	 * disable all interrupts, disable the port
	 */
	anakin_out(port, 0x18, anakin_in(port, 0x18) & ~IRQENABLE);
}

static void
anakin_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	unsigned int flags;

	save_flags_cli(flags);
	while (!(anakin_in(port, 0x10) & TXEMPTY));
	anakin_out(port, 0x10, (anakin_in(port, 0x10) & ~PRESCALER)
			| (quot << 3));

	//parity always set to none
	anakin_out(port, 0x18, anakin_in(port, 0x18) & ~PARITY);
	restore_flags(flags);
}

static const char *anakin_type(struct port *port)
{
	return port->type == PORT_ANAKIN ? "ANAKIN" : NULL;
}

static struct uart_ops anakin_pops = {
	tx_empty:	anakin_tx_empty,
	set_mctrl:	anakin_set_mctrl,
	get_mctrl:	anakin_get_mctrl,
	stop_tx:	anakin_stop_tx,
	start_tx:	anakin_start_tx,
	stop_rx:	anakin_stop_rx,
	enable_ms:	anakin_enable_ms,
	break_ctl:	anakin_break_ctl,
	startup:	anakin_startup,
	shutdown:	anakin_shutdown,
	change_speed:	anakin_change_speed,
	type:		anakin_type,
};

static struct uart_port anakin_ports[UART_NR] = {
	{
		base:		IO_BASE + UART0,
		irq:		IRQ_UART0,
		uartclk:	3686400,
		fifosize:	0,
		ops:		&anakin_pops,
	},
	{
		base:		IO_BASE + UART1,
		irq:		IRQ_UART1,
		uartclk:	3686400,
		fifosize:	0,
		ops:		&anakin_pops,
	},
	{
		base:		IO_BASE + UART2,
		irq:		IRQ_UART2,
		uartclk:	3686400,
		fifosize:	0,
		ops:		&anakin_pops,
	},
	{
		base:		IO_BASE + UART3,
		irq:		IRQ_UART3,
		uartclk:	3686400,
		fifosize:	0,
		ops:		&anakin_pops,
	},
	{
		base:		IO_BASE + UART4,
		irq:		IRQ_UART4,
		uartclk:	3686400,
		fifosize:	0,
		ops:		&anakin_pops,
	},
};


#ifdef CONFIG_SERIAL_ANAKIN_CONSOLE

static void
anakin_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = anakin_ports + co->index;
	unsigned int flags, status, i;

	/*
	 *	First save the status then disable the interrupts
	 */
	save_flags_cli(flags);
	status = anakin_in(port, 0x18);
	anakin_out(port, 0x18, status & ~IRQENABLE);
	restore_flags(flags);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++, s++) {
		while (!(anakin_in(port, 0x10) & TXEMPTY));

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		anakin_out(port, 0x14, *s);
		anakin_out(port, 0x18, anakin_in(port, 0x18) | SENDREQUEST);

		if (*s == 10) {
			while (!(anakin_in(port, 0x10) & TXEMPTY));
			anakin_out(port, 0x14, 13);
			anakin_out(port, 0x18, anakin_in(port, 0x18)
					| SENDREQUEST);
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the interrupts
	 */
	while (!(anakin_in(port, 0x10) & TXEMPTY));

	if (status & IRQENABLE)
		save_flags_cli(flags);
 		anakin_out(port, 0x18, anakin_in(port, 0x18) | IRQENABLE);
		restore_flags(flags);
}

static kdev_t
anakin_console_device(struct console *co)
{
	return MKDEV(SERIAL_ANAKIN_MAJOR, SERIAL_ANAKIN_MINOR + co->index);
}

static int
anakin_console_wait_key(struct console *co)
{
	struct uart_port *port = anakin_ports + co->index;
	unsigned int flags, status, ch;

	save_flags_cli(flags);
	status = anakin_in(port, 0x18);
	anakin_out(port, 0x18, status & ~IRQENABLE);
	restore_flags(flags);

	while (!(anakin_in(port, 0x10) & RXRELEASE));
	ch = anakin_in(port, 0x14);

	if (status & IRQENABLE) {
		save_flags_cli(flags);
		anakin_out(port, 0x18, anakin_in(port, 0x18) | IRQENABLE);
		restore_flags(flags);
	}
	return ch;
}

/*
 * Read the current UART setup.
 */
static void __init
anakin_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	int paritycode;

	*baud = GETBAUD (anakin_in(port, 0x10) & PRESCALER);
	paritycode = GETPARITY(anakin_in(port, 0x18) & PARITY);
	switch (paritycode) {
	  case NONEPARITY: *parity = 'n'; break;
	  case ODDPARITY: *parity = 'o'; break;
	  case EVENPARITY: *parity = 'e'; break;
	}
	*bits = 8;
}

static int __init
anakin_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = CONFIG_ANAKIN_DEFAULT_BAUDRATE;
	int bits = 8;
	int parity = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(anakin_ports, UART_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits);
	else
		anakin_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits);
}

static struct console anakin_console = {
	name:		SERIAL_ANAKIN_NAME,
	write:		anakin_console_write,
	device:		anakin_console_device,
	wait_key:	anakin_console_wait_key,
	setup:		anakin_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};

void __init
anakin_console_init(void)
{
	register_console(&anakin_console);
}

#define ANAKIN_CONSOLE		&anakin_console
#else
#define ANAKIN_CONSOLE		NULL
#endif

static struct uart_register anakin_reg = {
	normal_major:		SERIAL_ANAKIN_MAJOR,
	normal_name:		SERIAL_ANAKIN_NAME,
	normal_driver:		&normal,
	callout_major:		CALLOUT_ANAKIN_MAJOR,
	callout_name:		CALLOUT_ANAKIN_NAME,
	callout_driver:		&callout,
	table:			anakin_table,
	termios:		anakin_termios,
	termios_locked:		anakin_termios_locked,
	minor:			SERIAL_ANAKIN_MINOR,
	nr:			UART_NR,
	state:			anakin_state,
	port:			anakin_ports,
	cons:			ANAKIN_CONSOLE,
};

static int __init
anakin_init(void)
{
	return uart_register_port(&anakin_reg);
}

__initcall(anakin_init);

MODULE_DESCRIPTION("Anakin serial driver");
MODULE_AUTHOR("Tak-Shing Chan <chan@aleph1.co.uk>");
MODULE_SUPPORTED_DEVICE("ttyAN");
MODULE_LICENSE("GPL");

EXPORT_NO_SYMBOLS;
