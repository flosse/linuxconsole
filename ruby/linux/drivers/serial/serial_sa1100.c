/*
 *  linux/drivers/char/serial_sa1100.c
 *
 *  Driver for SA1100 serial ports
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
 * 2001-02-14 Initial power management support - Jeff Sutherland, Accelent Systems Inc
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
#include <linux/malloc.h>
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
#include <asm/hardware.h>
#include <asm/hardware/serial_sa1100.h>
#include <asm/arch/serial_reg.h>

#include "serial_core.h"

#ifdef CONFIG_PM
#include <linux/pm.h>

static struct pm_dev *ser_sa1100_pm_dev;

/* local storage of critical uart registers */
static unsigned long uart_saved_regs[3][4];

#endif

#undef DEBUG

#ifdef DEBUG
#define DPRINTK( x... )  printk( ##x )
#else
#define DPRINTK( x... )
#endif


/* We've been assigned a range on the "Low-density serial ports" major */
#define  SERIAL_SA1100_MAJOR	204
#define CALLOUT_SA1100_MAJOR	205
#define MINOR_START		5

#define NR_PORTS		3
static int nr_available_ports;

#define SA1100_ISR_PASS_LIMIT	256

/*
 * Convert from ignore_status_mask or read_status_mask to UTSR[01]
 */
#define SM_TO_UTSR0(x)	((x) & 0xff)
#define SM_TO_UTSR1(x)	((x) >> 8)
#define UTSR0_TO_SM(x)	((x))
#define UTSR1_TO_SM(x)	((x) << 8)
#define IDX_TO_OFFSET(x) ((x) << 2)

#define UART_GET_UTCR0(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTCR0))
#define UART_GET_UTCR1(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTCR1))
#define UART_GET_UTCR2(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTCR2))
#define UART_GET_UTCR3(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTCR3))
#define UART_GET_UTSR0(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTSR0))
#define UART_GET_UTSR1(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTSR1))
#define UART_GET_CHAR(port)		__raw_readl((port)->base + IDX_TO_OFFSET(UTDR))

#define UART_PUT_UTCR0(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTCR0))
#define UART_PUT_UTCR1(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTCR1))
#define UART_PUT_UTCR2(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTCR2))
#define UART_PUT_UTCR3(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTCR3))
#define UART_PUT_UTSR0(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTSR0))
#define UART_PUT_UTSR1(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTSR1))
#define UART_PUT_CHAR(port,v)		__raw_writel((v),(port)->base + IDX_TO_OFFSET(UTDR))

static struct tty_driver normal, callout;
static struct tty_struct *sa1100_table[NR_PORTS];
static struct termios *sa1100_termios[NR_PORTS], *sa1100_termios_locked[NR_PORTS];
static struct uart_state sa1100_state[NR_PORTS];

#ifdef CONFIG_PM
static int ser_sa1100_pm_callback(struct pm_dev *pm_dev, pm_request_t req, void *data);
#endif

/*
 * interrupts disabled on entry
 */
static void sa1100_stop_tx(struct uart_port *port, u_int from_tty)
{
	u32 utcr3 = UART_GET_UTCR3(port);
	UART_PUT_UTCR3(port, utcr3 & ~UTCR3_TIE);
	port->read_status_mask &= ~UTSR0_TO_SM(UTSR0_TFS);
}

/*
 * interrupts may not be disabled on entry
 */
static void sa1100_start_tx(struct uart_port *port, u_int nonempty, u_int from_tty)
{
	DPRINTK("%s:%s: nonempty = %d.\n",__FILE__,__FUNCTION__,nonempty);
	if (nonempty) {
		u32 utcr3 = UART_GET_UTCR3(port);
		DPRINTK("UTCR3 = %0#10x for port %0#10x\n",utcr3,port->base);
		port->read_status_mask |= UTSR0_TO_SM(UTSR0_TFS);
		UART_PUT_UTCR3(port, utcr3 | UTCR3_TIE);
		DPRINTK("wrote %0#10x to port.\n",utcr3 | UTCR3_TIE);
	}
	DPRINTK("%s:%s:finished.\n",__FILE__,__FUNCTION__);
}

/*
 * Interrupts enabled
 */
static void sa1100_stop_rx(struct uart_port *port)
{
	u32 utcr3 = UART_GET_UTCR3(port);
	UART_PUT_UTCR3(port, utcr3 & ~UTCR3_RIE);
}

/*
 * No modem control lines
 */
static void sa1100_enable_ms(struct uart_port *port)
{
	sa1100_mach_uart.enable_ms(port->base);
}

static void sa1100_rx_chars(struct uart_info *info)
{
	struct tty_struct *tty = info->tty;
	unsigned int status, ch, flg, ignored = 0;
	struct uart_icount *icount = &info->state->icount;
	struct uart_port *port = info->port;

	DPRINTK("%s\n",__FUNCTION__);
	
	status = UTSR1_TO_SM(UART_GET_UTSR1(port)) | UTSR0_TO_SM(UART_GET_UTSR0(port));
	while (status & UTSR1_TO_SM(UTSR1_RNE)) {
		ch = UART_GET_CHAR(port);

#ifdef CONFIG_REMOTE_DEBUG
		if (uart_rx_for_console(info, ch))
			goto ignore_char;
#endif
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;
		icount->rx++;

		flg = TTY_NORMAL;

		/*
		 * note that the error handling code is
		 * out of the main execution path
		 */
		if (status & UTSR1_TO_SM(UTSR1_PRE | UTSR1_FRE | UTSR1_ROR))
			goto handle_error;
	error_return:
		*tty->flip.flag_buf_ptr++ = flg;
		*tty->flip.char_buf_ptr++ = ch;
		tty->flip.count++;
	ignore_char:
		status = UTSR1_TO_SM(UART_GET_UTSR1(port)) | UTSR0_TO_SM(UART_GET_UTSR0(port));
	}
out:
	tty_flip_buffer_push(tty);
	return;

handle_error:
	if (status & UTSR1_TO_SM(UTSR1_PRE))
		icount->parity++;
	else if (status & UTSR1_TO_SM(UTSR1_FRE))
		icount->frame++;
	if (status & UTSR1_TO_SM(UTSR1_ROR))
		icount->overrun++;

	if (status & port->ignore_status_mask) {
		if (++ignored > 100)
			goto out;
		goto ignore_char;
	}

	status &= port->read_status_mask;

	if (status & UTSR1_TO_SM(UTSR1_PRE))
		flg = TTY_PARITY;
	else if (status & UTSR1_TO_SM(UTSR1_FRE))
		flg = TTY_FRAME;

	if (status & UTSR1_TO_SM(UTSR1_ROR)) {
		/*
		 * overrun does *not* affect the character
		 * we read from the FIFO
		 */
		*tty->flip.flag_buf_ptr++ = flg;
		*tty->flip.char_buf_ptr++ = ch;
		tty->flip.count++;
		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;
		ch = 0;
		flg = TTY_OVERRUN;
	}
	goto error_return;
}

static void sa1100_tx_chars(struct uart_info *info)
{
	struct uart_port *port = info->port;

	DPRINTK("%s\n",__FUNCTION__);
	
	if (info->x_char) {
		UART_PUT_CHAR(port, info->x_char);
		info->state->icount.tx++;
		info->x_char = 0;
		return;
	}
	if (info->xmit.head == info->xmit.tail
	    || info->tty->stopped
	    || info->tty->hw_stopped) {
		sa1100_stop_tx(info->port, 0);
		return;
	}

	/*
	 * Tried using FIFO (not checking TNF) for fifo fill:
	 * still had the '4 bytes repeated' problem.
	 */
	while (UART_GET_UTSR1(port) & UTSR1_TNF) {
		UART_PUT_CHAR(port, info->xmit.buf[info->xmit.tail]);
		info->xmit.tail = (info->xmit.tail + 1) & (UART_XMIT_SIZE - 1);
		info->state->icount.tx++;
		if (info->xmit.head == info->xmit.tail)
			break;
	}

	if (CIRC_CNT(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE) <
			WAKEUP_CHARS)
		uart_event(info, EVT_WRITE_WAKEUP);

	if (info->xmit.head == info->xmit.tail)
		sa1100_stop_tx(info->port, 0);
}

static void sa1100_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	struct uart_port *port = info->port;
	unsigned int status, pass_counter = 0;

//	DPRINTK("in sa1100 serial interrupt handler...\n");
	
	status = UART_GET_UTSR0(port);
	DPRINTK("irq: status = %08x,", status);
//	status &= SM_TO_UTSR0(port->read_status_mask);
	DPRINTK(" following mask op: %08x, mask: %08x\n",status,SM_TO_UTSR0(port->read_status_mask));
	do {
		if (status & (UTSR0_RFS | UTSR0_RID)) {
			/* Clear the receiver idle bit, if set */
			if (status & UTSR0_RID)
				UART_PUT_UTSR0(port, UTSR0_RID);
			sa1100_rx_chars(info);
		}
		if (status & (UTSR0_RBB | UTSR0_REB)) {
			/* Clear the relevent bits */
			UART_PUT_UTSR0(port, status & (UTSR0_RBB | UTSR0_REB));
			if (status & UTSR0_RBB) {
				info->state->icount.brk++;
			}
		}
		if (status & UTSR0_TFS)
			sa1100_tx_chars(info);
		if (pass_counter++ > SA1100_ISR_PASS_LIMIT)
			break;

		status = UART_GET_UTSR0(port); // & SM_TO_UTSR0(port->read_status_mask);
	} while (status & (UTSR0_TFS | UTSR0_RFS | UTSR0_RID));
}

/*
 * Return TIOCSER_TEMT when transmitter is not busy.
 */
static u_int sa1100_tx_empty(struct uart_port *port)
{
	return UART_GET_UTSR1(port) & UTSR1_TBY ? 0 : TIOCSER_TEMT;
}

static int sa1100_get_mctrl(struct uart_port *port)
{
	sa1100_mach_uart.get_mctrl(port->base);
}

static void sa1100_set_mctrl(struct uart_port *port, u_int mctrl)
{
	sa1100_mach_uart.set_mctrl(port->base, mctrl);
}

static void sa1100_break_ctl(struct uart_port *port, int break_state)
{
	u_int utcr3;

	utcr3 = UART_GET_UTCR3(port);
	if (break_state == -1)
		utcr3 |= UTCR3_BRK;
	else
		utcr3 &= ~UTCR3_BRK;
	UART_PUT_UTCR3(port, utcr3);
}

static int sa1100_startup(struct uart_port *port, struct uart_info *info)
{
	int retval;

	/*
	 * Allocate the IRQ
	 */
	DPRINTK("%s:",__FUNCTION__);
	
	retval = request_irq(port->irq, sa1100_int, 0, "serial_sa1100", info);
	if (retval) {
		DPRINTK("Error requesting interrupt %d, aborting...\n",port->irq);
		return retval;
	}
	/* turn on the power before twiddling any modem control lines */
	sa1100_mach_uart.on(port->base);  
	port->ops->set_mctrl(port, info->mctrl);

	/*
	 * Finally, clear and enable interrupts
	 */
	UART_PUT_UTSR0(port, -1);
	UART_PUT_UTCR3(port, UTCR3_RXE | UTCR3_RIE | UTCR3_TXE);

	DPRINTK("Successfully allocated irq%d.\n",port->irq);

	return 0;
}

static void sa1100_shutdown(struct uart_port *port, struct uart_info *info)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, info);

	/*
	 * Disable all interrupts, port and break condition
	 */
	UART_PUT_UTCR3(port, 0);
	sa1100_mach_uart.off(port->base);
	DPRINTK("%s:disabling irq%d\n",__FUNCTION__,port->irq);
}

static void sa1100_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	unsigned long flags;
	u_int utcr0, old_utcr3;

	DPRINTK("%s: setting baud to %d\n",__FUNCTION__,230400/(quot+1));
	
	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS7:	utcr0 = 0;		break;
	default:	utcr0 = UTCR0_DSS;	break;
	}
	if (cflag & CSTOPB)
		utcr0 |= UTCR0_SBS;
	if (cflag & PARENB) {
		utcr0 |= UTCR0_PE;
		if (!(cflag & PARODD))
			utcr0 |= UTCR0_OES;
	}

	port->read_status_mask = UTSR1_TO_SM(UTSR1_ROR);
	if (iflag & INPCK)
		port->read_status_mask |= UTSR1_TO_SM(UTSR1_FRE | UTSR1_PRE);
	if (iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UTSR0_TO_SM(UTSR0_RBB | UTSR0_REB);

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (iflag & IGNPAR)
		port->ignore_status_mask |= UTSR1_TO_SM(UTSR1_FRE | UTSR1_PRE);
	if (iflag & IGNBRK) {
		port->ignore_status_mask |= UTSR0_TO_SM(UTSR0_RBB | UTSR0_REB);
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (iflag & IGNPAR)
			port->ignore_status_mask |= UTSR1_TO_SM(UTSR1_ROR);
	}

	/* first, disable interrupts and drain transmitter */
	save_flags_cli(flags);
	old_utcr3 = UART_GET_UTCR3(port);
	UART_PUT_UTCR3(port, old_utcr3 & ~(UTCR3_RIE | UTCR3_TIE));
	restore_flags(flags);
	while (UART_GET_UTSR1(port) & UTSR1_TBY);

	/* then, disable everything */
	UART_PUT_UTCR3(port, 0);

	/* set the parity, stop bits and data size */
	UART_PUT_UTCR0(port, utcr0);

	/* set the baud rate */
	UART_PUT_UTCR1(port, ((quot & 0xf00) >> 8));
	UART_PUT_UTCR2(port, (quot & 0xff));

	UART_PUT_UTSR0(port, -1);

	UART_PUT_UTCR3(port, old_utcr3);

	DPRINTK("%s: baud set to %d\n",__FUNCTION__,230400/(quot+1));
}

static struct uart_ops sa1100_pops = {
	tx_empty:	sa1100_tx_empty,
	set_mctrl:	sa1100_set_mctrl,
	get_mctrl:	sa1100_get_mctrl,
	stop_tx:	sa1100_stop_tx,
	start_tx:	sa1100_start_tx,
	stop_rx:	sa1100_stop_rx,
	enable_ms:	sa1100_enable_ms,
	break_ctl:	sa1100_break_ctl,
	startup:	sa1100_startup,
	shutdown:	sa1100_shutdown,
	change_speed:	sa1100_change_speed,
};

static struct uart_port sa1100_ports[NR_PORTS] = {
	{
		uartclk:	3686400,
		fifosize:	0,
		ops:		&sa1100_pops,
	},
	{
		uartclk:	3686400,
		fifosize:	0,
		ops:		&sa1100_pops,
	},
	{
		uartclk:	3686400,
		fifosize:	0,
		ops:		&sa1100_pops,
	}
};

static void __init sa1100_init_ports(void)
{
	int ports = 0;
	int i;
	static int first = 1;

	if (!first)
		return;
	first = 0;

	i = sa1100_mach_uart.uart1_idx;
	if (i >= 0) {
		sa1100_ports[i].base = (unsigned long)&Ser1UTCR0;
		sa1100_ports[i].irq  = IRQ_Ser1UART;
		ports++;
	}
	i = sa1100_mach_uart.uart2_idx;
	if (i >= 0) {
		sa1100_ports[i].base = (unsigned long)&Ser2UTCR0;
		sa1100_ports[i].irq  = IRQ_Ser2ICP;
		ports++;
	}
	i = sa1100_mach_uart.uart3_idx;
	if (i >= 0) {
		sa1100_ports[i].base = (unsigned long)&Ser3UTCR0;
		sa1100_ports[i].irq  = IRQ_Ser3UART;
		ports++;
	}
	nr_available_ports = ports;

	printk("SA1100 serial driver configured:\n");
	for (i = 0; i < ports; i++) {
		int uart = ((sa1100_ports[i].base & 0x70000) + 0x10000) >> 17;
		printk ("\tttySA%d attached to UART%d\n", i, uart);
	}
}

#ifdef CONFIG_SERIAL_SA1100_CONSOLE

static void sa1100_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = sa1100_ports + co->index;
	unsigned long flags;
	u_int old_utcr3, status, i;

	/*
	 *	First, save UTCR3 and then disable interrupts
	 */

	DPRINTK("%s:writing chars to console device...\n",__FUNCTION__);
	
	save_flags_cli(flags);
	old_utcr3 = UART_GET_UTCR3(port);
	UART_PUT_UTCR3(port, old_utcr3 & ~(UTCR3_RIE | UTCR3_TIE));

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = UART_GET_UTSR1(port);
		} while (!(status & UTSR1_TNF));
		UART_PUT_CHAR(port, s[i]);
		if (s[i] == '\n') {
			do {
				status = UART_GET_UTSR1(port);
			} while (!(status & UTSR1_TNF));
			UART_PUT_CHAR(port, '\r');
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore UTCR3
	 */
	do {
		status = UART_GET_UTSR1(port);
	} while (status & UTSR1_TBY);
	restore_flags(flags);
	UART_PUT_UTCR3(port, old_utcr3);
}

static kdev_t sa1100_console_device(struct console *co)
{
	return MKDEV(SERIAL_SA1100_MAJOR, MINOR_START + co->index);
}

static int sa1100_console_wait_key(struct console *co)
{
	struct uart_port *port = sa1100_ports + co->index;
	unsigned long flags;
	u_int old_utcr3, status, ch;

	/*
	 * Save UTCR3 and disable interrupts
	 */

	DPRINTK("%s:Waiting for console input...\n",__FUNCTION__);
	
	save_flags_cli(flags);
	old_utcr3 = UART_GET_UTCR3(port);
	UART_PUT_UTCR3(port, old_utcr3 & ~(UTCR3_RIE | UTCR3_TIE));
	restore_flags(flags);

	/*
	 * Wait for a character
	 */
	do {
		status = UART_GET_UTSR1(port);
	} while (!(status & UTSR1_RNE));
	ch = UART_GET_CHAR(port);

	/*
	 * Restore UTCR3
	 */
	UART_PUT_UTCR3(port, old_utcr3);

	return ch;
}

/*
 * If the port was already initialised (eg, by a boot loader), try to determine
 * the current setup.
 */
static void __init
sa1100_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	u_int utcr3;

	utcr3 = UART_GET_UTCR3(port) & (UTCR3_RXE | UTCR3_TXE);
	if (utcr3 == (UTCR3_RXE | UTCR3_TXE)) {
		/* ok, the port was enabled */
		u_int utcr0, quot;

		utcr0 = UART_GET_UTCR0(port);

		*parity = 'n';
		if (utcr0 & UTCR0_PE) {
			if (utcr0 & UTCR0_OES)
				*parity = 'e';
			else
				*parity = 'o';
		}

		if (utcr0 & UTCR0_DSS)
			*bits = 8;
		else
			*bits = 7;

		quot = ((UART_GET_UTCR1(port) & 0x0f) << 8) | UART_GET_UTCR2(port);
		*baud = port->uartclk / (16 * (quot + 1));
	}
}

static int __init
sa1100_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = CONFIG_SA1100_DEFAULT_BAUDRATE;
	int bits = 8;
	int parity = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(sa1100_ports, nr_available_ports, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits);
	else
		sa1100_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits);
}

static struct console sa1100_console = {
	name:		"ttySA",
#ifdef CONFIG_REMOTE_DEBUG
	read:		uart_console_read,
#endif
	write:		sa1100_console_write,
	device:		sa1100_console_device,
	wait_key:	sa1100_console_wait_key,
	setup:		sa1100_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};

void __init sa1100_rs_console_init(void)
{
	sa1100_init_ports();
	register_console(&sa1100_console);
}

#define SA1100_CONSOLE	&sa1100_console
#else
#define SA1100_CONSOLE	NULL
#endif

static struct uart_register sa1100_reg = {
	normal_major:		SERIAL_SA1100_MAJOR,
	normal_name:		"ttySA",
	normal_driver:		&normal,
	callout_major:		CALLOUT_SA1100_MAJOR,
	callout_name:		"cusa",
	callout_driver:		&callout,
	table:			sa1100_table,
	termios:		sa1100_termios,
	termios_locked:		sa1100_termios_locked,
	minor:			MINOR_START,
	state:			sa1100_state,
	port:			sa1100_ports,
	cons:			SA1100_CONSOLE,
};

static int __init sa1100_uart_init(void)
{
	sa1100_init_ports();
	sa1100_reg.nr = nr_available_ports;

#ifdef CONFIG_PM
	ser_sa1100_pm_dev = pm_register(PM_SYS_DEV, 0, ser_sa1100_pm_callback);
	DPRINTK("%s:registering ser_sa1100_pm_callback = %0#10x\n",__FUNCTION__,ser_1110_pm_dev);
#endif
	
	return uart_register_port(&sa1100_reg);
}

__initcall(sa1100_uart_init);

#ifdef CONFIG_PM
static int ser_sa1100_pm_callback(struct pm_dev *pm_dev, pm_request_t req, void *data)
{
	int i, j;
	
	switch (req) {
	case PM_SUSPEND: /* enter D1-D3 */
		// save all uart registers
		// This scheme wastes a little storage but is super fast on the way down because
		// of optimisations the compiler can do. This isn't optimal: characters coming in
		// or going out when this event appears will get trashed.  Hmmm.... Do we care?

#ifdef CONFIG_SERIAL_SA1100_CONSOLE
		unregister_console(SA1100_CONSOLE);
#endif	
		for (i=0;i<3;i++){
			for (j=0;j<4;j++) {  //don't need to save status or data ports. 
				uart_saved_regs[i][j]= *(unsigned long *)(sa1100_ports[i].base + (j<<2));
				DPRINTK("%s: saving register UTCR%d of uart %d...\n",__FUNCTION__,j,i);
			}
			UART_PUT_UTCR3(&sa1100_ports[i],0);  //disable interrupts BUT DON'T DEREGISTER
			sa1100_mach_uart.off(sa1100_ports[i].base);
		}	
		break;

	case PM_RESUME:  /* enter D0 */
		// restore all uart registers (we can afford to take our time now...)
		for (i=0;i<3;i++) {
			UART_PUT_UTCR0(&sa1100_ports[i],uart_saved_regs[i][UTCR0]);
			UART_PUT_UTCR1(&sa1100_ports[i],uart_saved_regs[i][UTCR1]);
			UART_PUT_UTCR2(&sa1100_ports[i],uart_saved_regs[i][UTCR2]);
			if (uart_saved_regs[i][UTCR3] && UTCR3_RIE | UTCR3_TXE | UTCR3_RXE) {
			       	//port had some or all enabled on entry, must have been open...
				UART_PUT_UTSR0(&sa1100_ports[i],-1);
				UART_PUT_UTCR3(&sa1100_ports[i],uart_saved_regs[i][UTCR3]);
				sa1100_mach_uart.on(sa1100_ports[i].base);
			}
		}				
	
#ifdef CONFIG_SERIAL_SA1100_CONSOLE
		register_console(SA1100_CONSOLE);
#endif
                break;
	}

	return 0;
}
#endif

