/*
 *  linux/drivers/char/serial_sa1100.c
 *
 *  Driver for SA11x0 serial ports
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
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/hardware.h>
#include <asm/mach/serial_sa1100.h>
#include <asm/arch/serial_reg.h>

#if defined(CONFIG_SERIAL_SA1100_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>

/* We've been assigned a range on the "Low-density serial ports" major */
#define SERIAL_SA1100_MAJOR	204
#define CALLOUT_SA1100_MAJOR	205
#define MINOR_START		5

#define NR_PORTS		3

#define SA1100_ISR_PASS_LIMIT	256

/*
 * Convert from ignore_status_mask or read_status_mask to UTSR[01]
 */
#define SM_TO_UTSR0(x)	((x) & 0xff)
#define SM_TO_UTSR1(x)	((x) >> 8)
#define UTSR0_TO_SM(x)	((x))
#define UTSR1_TO_SM(x)	((x) << 8)

#define UART_GET_UTCR0(port)		__raw_readl((port)->base + UTCR0)
#define UART_GET_UTCR1(port)		__raw_readl((port)->base + UTCR1)
#define UART_GET_UTCR2(port)		__raw_readl((port)->base + UTCR2)
#define UART_GET_UTCR3(port)		__raw_readl((port)->base + UTCR3)
#define UART_GET_UTSR0(port)		__raw_readl((port)->base + UTSR0)
#define UART_GET_UTSR1(port)		__raw_readl((port)->base + UTSR1)
#define UART_GET_CHAR(port)		__raw_readl((port)->base + UTDR)

#define UART_PUT_UTCR0(port,v)		__raw_writel((v),(port)->base + UTCR0)
#define UART_PUT_UTCR1(port,v)		__raw_writel((v),(port)->base + UTCR1)
#define UART_PUT_UTCR2(port,v)		__raw_writel((v),(port)->base + UTCR2)
#define UART_PUT_UTCR3(port,v)		__raw_writel((v),(port)->base + UTCR3)
#define UART_PUT_UTSR0(port,v)		__raw_writel((v),(port)->base + UTSR0)
#define UART_PUT_UTSR1(port,v)		__raw_writel((v),(port)->base + UTSR1)
#define UART_PUT_CHAR(port,v)		__raw_writel((v),(port)->base + UTDR)

static struct tty_driver normal, callout;
static struct tty_struct *sa1100_table[NR_PORTS];
static struct termios *sa1100_termios[NR_PORTS], *sa1100_termios_locked[NR_PORTS];
static struct uart_state sa1100_state[NR_PORTS];
#ifdef SUPPORT_SYSRQ
static struct console sa1100_console;
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
	if (nonempty) {
		u32 utcr3 = UART_GET_UTCR3(port);
		port->read_status_mask |= UTSR0_TO_SM(UTSR0_TFS);
		UART_PUT_UTCR3(port, utcr3 | UTCR3_TIE);
	}
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
}

static void
#ifdef SUPPORT_SYSRQ
sa1100_rx_chars(struct uart_info *info, struct pt_regs *regs)
#else
sa1100_rx_chars(struct uart_info *info)
#endif
{
	struct tty_struct *tty = info->tty;
	unsigned int status, ch, flg, ignored = 0;
	struct uart_icount *icount = &info->state->icount;
	struct uart_port *port = info->port;

	status = UTSR1_TO_SM(UART_GET_UTSR1(port)) | UTSR0_TO_SM(UART_GET_UTSR0(port));
	while (status & UTSR1_TO_SM(UTSR1_RNE)) {
		ch = UART_GET_CHAR(port);

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
#ifdef SUPPORT_SYSRQ
		if (info->sysrq) {
			if (ch && time_before(jiffies, info->sysrq)) {
				handle_sysrq(ch, regs, NULL, NULL);
				info->sysrq = 0;
				goto ignore_char;
			}
			info->sysrq = 0;
		}
#endif
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
#ifdef SUPPORT_SYSRQ
	info->sysrq = 0;
#endif
	goto error_return;
}

static void sa1100_tx_chars(struct uart_info *info)
{
	struct uart_port *port = info->port;

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

	status = UART_GET_UTSR0(port);
	do {
		if (status & (UTSR0_RFS | UTSR0_RID)) {
			/* Clear the receiver idle bit, if set */
			if (status & UTSR0_RID)
				UART_PUT_UTSR0(port, UTSR0_RID);
#ifdef SUPPORT_SYSRQ
			sa1100_rx_chars(info, regs);
#else
			sa1100_rx_chars(info);
#endif
		}

		/* Clear the relevent break bits */
		if (status & (UTSR0_RBB | UTSR0_REB))
			UART_PUT_UTSR0(port, status & (UTSR0_RBB | UTSR0_REB));

		if (status & UTSR0_RBB)
			info->state->icount.brk++;

		if (status & UTSR0_REB) {
#ifdef SUPPORT_SYSRQ
			if (info->state->line == sa1100_console.index &&
			    !info->sysrq) {
				info->sysrq = jiffies + HZ*5;
			}
#endif
		}
		status &= SM_TO_UTSR0(port->read_status_mask);
		if (status & UTSR0_TFS)
			sa1100_tx_chars(info);
		if (pass_counter++ > SA1100_ISR_PASS_LIMIT)
			break;
		status = UART_GET_UTSR0(port);
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
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void sa1100_set_mctrl(struct uart_port *port, u_int mctrl)
{
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
	retval = request_irq(port->irq, sa1100_int, 0, "serial_sa1100", info);
	if (retval)
		return retval;

	/*
	 * Finally, clear and enable interrupts
	 */
	UART_PUT_UTSR0(port, -1);
	UART_PUT_UTCR3(port, UTCR3_RXE | UTCR3_TXE | UTCR3_RIE);

	return 0;
}

static void sa1100_shutdown(struct uart_port *port, struct uart_info *info)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, info);

	/*
	 * Disable all interrupts, port and break condition.
	 */
	UART_PUT_UTCR3(port, 0);
}

static void sa1100_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	unsigned long flags;
	u_int utcr0, old_utcr3;

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

static struct uart_port sa1100_ports[NR_PORTS];

/*
 * Setup the SA1100 serial ports.  Note that we don't include the IrDA
 * port here since we have our own SIR/FIR driver (see drivers/net/irda)
 *
 * Note also that we support "console=ttySAx" where "x" is either 0 or 1.
 * Which serial port this ends up being depends on the machine you're
 * running this kernel on.  I'm not convinced that this is a good idea,
 * but that's the way it traditionally works.
 *
 * Note that NanoEngine UART3 becomes UART2, and UART2 is no longer
 * used here.
 */
static void sa1100_init_ports(void)
{
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0; i < NR_PORTS; i++) {
		sa1100_ports[i].uartclk  = 3686400;
		sa1100_ports[i].ops      = &sa1100_pops;
		sa1100_ports[i].fifosize = 8;
	}

	/*
	 * make transmit lines outputs, so that when the port
	 * is closed, the output is in the MARK state.
	 */
	PPDR |= PPC_TXD1 | PPC_TXD3;
	PPSR |= PPC_TXD1 | PPC_TXD3;
}

void __init sa1100_register_uart_fns(struct sa1100_port_fns *fns)
{
	if (fns->enable_ms)
		sa1100_pops.enable_ms = fns->enable_ms;
	if (fns->get_mctrl)
		sa1100_pops.get_mctrl = fns->get_mctrl;
	if (fns->set_mctrl)
		sa1100_pops.set_mctrl = fns->set_mctrl;
	sa1100_pops.pm	      = fns->pm;
}

void __init sa1100_register_uart(int idx, int port)
{
	if (idx >= NR_PORTS) {
		printk(KERN_ERR __FUNCTION__ ": bad index number %d\n", idx);
		return;
	}

	switch (port) {
	case 1:
		sa1100_ports[idx].base = (unsigned long)&Ser1UTCR0;
		sa1100_ports[idx].irq  = IRQ_Ser1UART;
		break;

	case 2:
		sa1100_ports[idx].base = (unsigned long)&Ser2UTCR0;
		sa1100_ports[idx].irq  = IRQ_Ser2ICP;
		break;

	case 3:
		sa1100_ports[idx].base = (unsigned long)&Ser3UTCR0;
		sa1100_ports[idx].irq  = IRQ_Ser3UART;
		break;

	default:
		printk(KERN_ERR __FUNCTION__ ": bad port number %d\n", port);
	}
}


#ifdef CONFIG_SERIAL_SA1100_CONSOLE

/*
 * Interrupts are disabled on entering
 */
static void sa1100_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = sa1100_ports + co->index;
	u_int old_utcr3, status, i;

	/*
	 *	First, save UTCR3 and then disable interrupts
	 */
	old_utcr3 = UART_GET_UTCR3(port);
	UART_PUT_UTCR3(port, (old_utcr3 & ~(UTCR3_RIE | UTCR3_TIE)) | UTCR3_TXE);

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
	save_flags(flags);
	cli();
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

		quot = UART_GET_UTCR2(port) | UART_GET_UTCR1(port) << 8;
		quot &= 0xfff;
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
	port = uart_get_console(sa1100_ports, NR_PORTS, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits);
	else
		sa1100_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits);
}

static struct console sa1100_console = {
	name:		"ttySA",
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
	sa1100_console.driver = &normal;	
	register_console(&sa1100_console);
}

#define SA1100_CONSOLE	&sa1100_console
#else
#define SA1100_CONSOLE	NULL
#endif

static struct uart_register sa1100_reg = {
	owner:			THIS_MODULE,
	normal_major:		SERIAL_SA1100_MAJOR,
	normal_name:		"ttySA%d",
	normal_driver:		&normal,
	callout_major:		CALLOUT_SA1100_MAJOR,
	callout_name:		"cusa%d",
	callout_driver:		&callout,
	table:			sa1100_table,
	termios:		sa1100_termios,
	termios_locked:		sa1100_termios_locked,
	minor:			MINOR_START,
	nr:			NR_PORTS,
	state:			sa1100_state,
	port:			sa1100_ports,
	cons:			SA1100_CONSOLE,
};

static int __init sa1100_serial_init(void)
{
	sa1100_init_ports();
	return uart_register_port(&sa1100_reg);
}

static void __exit sa1100_serial_exit(void)
{
	uart_unregister_port(&sa1100_reg);
}

module_init(sa1100_serial_init);
module_exit(sa1100_serial_exit);

