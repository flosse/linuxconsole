/*
 *  linux/drivers/char/serial_8250.c
 *
 *  Driver for 8250/16550-type serial ports
 *
 *  Based on drivers/char/serial.c, by Linus Torvalds, Theodore Ts'o.
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *  $Id$
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/serial_reg.h>
#include <linux/serialP.h>
#include <linux/delay.h>
#include <linux/serial_core.h>
#include <linux/kmod.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include "serial_8250.h"

#include <asm/serial.h>

static struct old_serial_port old_serial_port[] = {
	SERIAL_PORT_DFNS /* defined in asm/serial.h */
};

#define UART_NR	ARRAY_SIZE(old_serial_port)

static struct tty_driver normal, callout;
static struct tty_struct *serial8250_table[UART_NR];
static struct termios *serial8250_termios[UART_NR], *serial8250_termios_locked[UART_NR];
#ifdef SUPPORT_SYSRQ
static struct console serial8250_console;
#endif
static struct uart_info *IRQ_ports[NR_IRQS];

#if defined(CONFIG_SERIAL_RSA) && defined(MODULE)

#define PORT_RSA_MAX 4
static int probe_rsa[PORT_RSA_MAX];
static int force_rsa[PORT_RSA_MAX];

MODULE_PARM(probe_rsa, "1-" __MODULE_STRING(PORT_RSA_MAX) "i");
MODULE_PARM_DESC(probe_rsa, "Probe I/O ports for RSA");
MODULE_PARM(force_rsa, "1-" __MODULE_STRING(PORT_RSA_MAX) "i");
MODULE_PARM_DESC(force_rsa, "Force I/O ports for RSA");
#endif /* CONFIG_SERIAL_RSA  */

#define port_acr	unused[0]	/* 8bit */
#define port_ier	unused[1]	/* 8bit */
#define port_rev	unused[2]	/* 8bit */
#define port_lcr	unused[3]	/* 8bit */

/*
 * Here we define the default xmit fifo size used for each type of UART.
 */
static struct serial_uart_config uart_config[PORT_MAX_8250+1] = {
	{ "unknown",	1,	0 },
	{ "8250",	1,	0 },
	{ "16450",	1,	0 },
	{ "16550",	1,	0 },
	{ "16550A",	16,	UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "Cirrus",	1, 	0 },
	{ "ST16650",	1,	UART_CLEAR_FIFO | UART_STARTECH },
	{ "ST16650V2",	32,	UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
	{ "TI16750",	64,	UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "Startech",	1,	0 },
	{ "16C950/954",	128,	UART_CLEAR_FIFO | UART_USE_FIFO },
	{ "ST16654",	64,	UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
	{ "XR16850",	128,	UART_CLEAR_FIFO | UART_USE_FIFO | UART_STARTECH },
	{ "RSA",	2048,	UART_CLEAR_FIFO | UART_USE_FIFO }
};

static _INLINE_ unsigned int serial_in(struct uart_port *port, int offset)
{
	offset <<= port->regshift;

	switch (port->iotype) {
#ifdef CONFIG_HUB6
	case SERIAL_IO_HUB6:
		outb(port->hub6 - 1 + offset, port->iobase);
		return inb(port->iobase + 1);
#endif

	case SERIAL_IO_MEM:
		return readb((unsigned long)port->membase + offset);

	default:
		return inb(port->iobase + offset);
	}
}

static _INLINE_ void
serial_out(struct uart_port *port, int offset, int value)
{
	offset <<= port->regshift;

	switch (port->iotype) {
#ifdef CONFIG_HUB6
	case SERIAL_IO_HUB6:
		outb(port->hub6 - 1 + offset, port->iobase);
		outb(value, port->iobase + 1);
		break;
#endif

	case SERIAL_IO_MEM:
		writeb(value, (unsigned long)port->membase + offset);
		break;

	default:
		outb(value, port->iobase + offset);
	}
}

/*
 * We used to support using pause I/O for certain machines.  We
 * haven't supported this for a while, but just in case it's badly
 * needed for certain old 386 machines, I've left these #define's
 * in....
 */
#define serial_inp(port, offset)		serial_in(port, offset)
#define serial_outp(port, offset, value)	serial_out(port, offset, value)


/*
 * For the 16C950
 */
static void serial_icr_write(struct uart_port *port, int offset, int  value)
{
	serial_out(port, UART_SCR, offset);
	serial_out(port, UART_ICR, value);
}

static unsigned int serial_icr_read(struct uart_port *port, int offset)
{
	unsigned int value;

	serial_icr_write(port, UART_ACR, port->port_acr | UART_ACR_ICRRD);
	serial_out(port, UART_SCR, offset);
	value = serial_in(port, UART_ICR);
	serial_icr_write(port, UART_ACR, port->port_acr);

	return value;
}

#ifdef CONFIG_SERIAL_RSA
/* Attempts to turn on the RSA FIFO.  Returns zero on failure */
static int enable_rsa(struct uart_port *port)
{
	unsigned char mode;
	int result;
	unsigned long flags;

	save_flags(flags); cli();
	mode = serial_inp(port, UART_RSA_MSR);
	result = mode & UART_RSA_MSR_FIFO;

	if (!result) {
		serial_outp(port, UART_RSA_MSR, mode | UART_RSA_MSR_FIFO);
		mode = serial_inp(port, UART_RSA_MSR);
		result = mode & UART_RSA_MSR_FIFO;
	}

	restore_flags(flags);
	return result;
}

/* Attempts to turn off the RSA FIFO.  Returns zero on failure */
static int disable_rsa(struct uart_port *port)
{
	unsigned char mode;
	int result;
	unsigned long flags;

	save_flags(flags); cli();
	mode = serial_inp(port, UART_RSA_MSR);
	result = !(mode & UART_RSA_MSR_FIFO);

	if (!result) {
		serial_outp(port, UART_RSA_MSR, mode & ~UART_RSA_MSR_FIFO);
		mode = serial_inp(port, UART_RSA_MSR);
		result = !(mode & UART_RSA_MSR_FIFO);
	}

	restore_flags(flags);
	return result;
}
#endif /* CONFIG_SERIAL_RSA */

/*
 * This is a quickie test to see how big the FIFO is.
 * It doesn't work at all the time, more's the pity.
 */
static int size_fifo(struct uart_port *port)
{
	unsigned char old_fcr, old_mcr, old_dll, old_dlm;
	int count;

	old_fcr = serial_inp(port, UART_FCR);
	old_mcr = serial_inp(port, UART_MCR);
	serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO |
		    UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
	serial_outp(port, UART_MCR, UART_MCR_LOOP);
	serial_outp(port, UART_LCR, UART_LCR_DLAB);
	old_dll = serial_inp(port, UART_DLL);
	old_dlm = serial_inp(port, UART_DLM);
	serial_outp(port, UART_DLL, 0x01);
	serial_outp(port, UART_DLM, 0x00);
	serial_outp(port, UART_LCR, 0x03);
	for (count = 0; count < 256; count++)
		serial_outp(port, UART_TX, count);
	mdelay(20);
	for (count = 0; (serial_inp(port, UART_LSR) & UART_LSR_DR) &&
	     (count < 256); count++)
		serial_inp(port, UART_RX);
	serial_outp(port, UART_FCR, old_fcr);
	serial_outp(port, UART_MCR, old_mcr);
	serial_outp(port, UART_LCR, UART_LCR_DLAB);
	serial_outp(port, UART_DLL, old_dll);
	serial_outp(port, UART_DLM, old_dlm);

	return count;
}

/*
 * This is a helper routine to autodetect StarTech/Exar/Oxsemi UART's.
 * When this function is called we know it is at least a StarTech
 * 16650 V2, but it might be one of several StarTech UARTs, or one of
 * its clones.  (We treat the broken original StarTech 16650 V1 as a
 * 16550, and why not?  Startech doesn't seem to even acknowledge its
 * existence.)
 * 
 * What evil have men's minds wrought...
 */
static void
autoconfig_startech_uarts(struct uart_port *port)
{
	unsigned char scratch, scratch2, scratch3, scratch4;

	/*
	 * First we check to see if it's an Oxford Semiconductor UART.
	 *
	 * If we have to do this here because some non-National
	 * Semiconductor clone chips lock up if you try writing to the
	 * LSR register (which serial_icr_read does)
	 */
	if (port->type == PORT_16550A) {
		/*
		 * EFR [4] must be set else this test fails
		 *
		 * This shouldn't be necessary, but Mike Hudson
		 * (Exoray@isys.ca) claims that it's needed for 952
		 * dual UART's (which are not recommended for new designs).
		 */
		port->port_acr = 0;
		serial_out(port, UART_LCR, 0xBF);
		serial_out(port, UART_EFR, 0x10);
		serial_out(port, UART_LCR, 0x00);
		/* Check for Oxford Semiconductor 16C950 */
		scratch = serial_icr_read(port, UART_ID1);
		scratch2 = serial_icr_read(port, UART_ID2);
		scratch3 = serial_icr_read(port, UART_ID3);
		
		if (scratch == 0x16 && scratch2 == 0xC9 &&
		    (scratch3 == 0x50 || scratch3 == 0x52 ||
		     scratch3 == 0x54)) {
			port->type = PORT_16C950;
			port->port_rev = serial_icr_read(port, UART_REV) |
				(scratch3 << 8);
			return;
		}
	}
	
	/*
	 * We check for a XR16C850 by setting DLL and DLM to 0, and
	 * then reading back DLL and DLM.  If DLM reads back 0x10,
	 * then the UART is a XR16C850 and the DLL contains the chip
	 * revision.  If DLM reads back 0x14, then the UART is a
	 * XR16C854.
	 */

	/* Save the DLL and DLM */

	serial_outp(port, UART_LCR, UART_LCR_DLAB);
	scratch3 = serial_inp(port, UART_DLL);
	scratch4 = serial_inp(port, UART_DLM);

	serial_outp(port, UART_DLL, 0);
	serial_outp(port, UART_DLM, 0);
	scratch2 = serial_inp(port, UART_DLL);
	scratch = serial_inp(port, UART_DLM);
	serial_outp(port, UART_LCR, 0);

	if (scratch == 0x10 || scratch == 0x14) {
		if (scratch == 0x10)
			port->port_rev = scratch2;
		port->type = PORT_16850;
		return;
	}

	/* Restore the DLL and DLM */

	serial_outp(port, UART_LCR, UART_LCR_DLAB);
	serial_outp(port, UART_DLL, scratch3);
	serial_outp(port, UART_DLM, scratch4);
	serial_outp(port, UART_LCR, 0);

	/*
	 * We distinguish between the '654 and the '650 by counting
	 * how many bytes are in the FIFO.  I'm using this for now,
	 * since that's the technique that was sent to me in the
	 * serial driver update, but I'm not convinced this works.
	 * I've had problems doing this in the past.  -TYT
	 */
	if (size_fifo(port) == 64)
		port->type = PORT_16654;
	else
		port->type = PORT_16650V2;
}

/*
 * This routine is called by rs_init() to initialize a specific serial
 * port.  It determines what type of UART chip this serial port is
 * using: 8250, 16450, 16550, 16550A.  The important question is
 * whether or not this UART is a 16550A or not, since this will
 * determine whether or not we can use its FIFO features or not.
 */
static void autoconfig(struct uart_port *port, unsigned int probeflags)
{
	unsigned char status1, status2, scratch, scratch2, scratch3;
	unsigned char save_lcr, save_mcr;
	unsigned long flags;

#ifdef SERIAL_DEBUG_AUTOCONF
	printk("Testing ttyS%d (0x%04lx, 0x%08lx)...\n",
		port->line, port->iobase, port->mapbase);
#endif

	if (!port->iobase && !port->mapbase)
		return;

	save_flags(flags); cli();

	if (!(port->flags & ASYNC_BUGGY_UART)) {
		/*
		 * Do a simple existence test first; if we fail this,
		 * there's no point trying anything else.
		 * 
		 * 0x80 is used as a nonsense port to prevent against
		 * false positives due to ISA bus float.  The
		 * assumption is that 0x80 is a non-existent port;
		 * which should be safe since include/asm/io.h also
		 * makes this assumption.
		 */
		scratch = serial_inp(port, UART_IER);
		serial_outp(port, UART_IER, 0);
#ifdef __i386__
		outb(0xff, 0x080);
#endif
		scratch2 = serial_inp(port, UART_IER);
		serial_outp(port, UART_IER, 0x0F);
#ifdef __i386__
		outb(0, 0x080);
#endif
		scratch3 = serial_inp(port, UART_IER);
		serial_outp(port, UART_IER, scratch);
		if (scratch2 || scratch3 != 0x0F) {
#ifdef SERIAL_DEBUG_AUTOCONF
			printk("serial: ttyS%d: simple autoconfig failed "
			       "(%02x, %02x)\n", port->line, 
			       scratch2, scratch3);
#endif
			restore_flags(flags);
			return;		/* We failed; there's nothing here */
		}
	}

	save_mcr = serial_in(port, UART_MCR);
	save_lcr = serial_in(port, UART_LCR);

	/* 
	 * Check to see if a UART is really there.  Certain broken
	 * internal modems based on the Rockwell chipset fail this
	 * test, because they apparently don't implement the loopback
	 * test mode.  So this test is skipped on the COM 1 through
	 * COM 4 ports.  This *should* be safe, since no board
	 * manufacturer would be stupid enough to design a board
	 * that conflicts with COM 1-4 --- we hope!
	 */
	if (!(port->flags & ASYNC_SKIP_TEST)) {
		serial_outp(port, UART_MCR, UART_MCR_LOOP | 0x0A);
		status1 = serial_inp(port, UART_MSR) & 0xF0;
		serial_outp(port, UART_MCR, save_mcr);
		if (status1 != 0x90) {
#ifdef SERIAL_DEBUG_AUTOCONF
			printk("serial: ttyS%d: no UART loopback failed\n",
			       port->line);
#endif
			restore_flags(flags);
			return;
		}
	}
	serial_outp(port, UART_LCR, 0xBF); /* set up for StarTech test */
	serial_outp(port, UART_EFR, 0);	/* EFR is the same as FCR */
	serial_outp(port, UART_LCR, 0);
	serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO);
	scratch = serial_in(port, UART_IIR) >> 6;
	switch (scratch) {
		case 0:
			port->type = PORT_16450;
			break;
		case 1:
			port->type = PORT_UNKNOWN;
			break;
		case 2:
			port->type = PORT_16550;
			break;
		case 3:
			port->type = PORT_16550A;
			break;
	}
	if (port->type == PORT_16550A) {
		/* Check for Startech UART's */
		serial_outp(port, UART_LCR, UART_LCR_DLAB);
		if (serial_in(port, UART_EFR) == 0) {
			port->type = PORT_16650;
		} else {
			serial_outp(port, UART_LCR, 0xBF);
			if (serial_in(port, UART_EFR) == 0)
				autoconfig_startech_uarts(port);
		}
	}
	if (port->type == PORT_16550A) {
		/* Check for TI 16750 */
		serial_outp(port, UART_LCR, save_lcr | UART_LCR_DLAB);
		serial_outp(port, UART_FCR,
			    UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
		scratch = serial_in(port, UART_IIR) >> 5;
		if (scratch == 7) {
			/*
			 * If this is a 16750, and not a cheap UART
			 * clone, then it should only go into 64 byte
			 * mode if the UART_FCR7_64BYTE bit was set
			 * while UART_LCR_DLAB was latched.
			 */
 			serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO);
			serial_outp(port, UART_LCR, 0);
			serial_outp(port, UART_FCR,
				    UART_FCR_ENABLE_FIFO | UART_FCR7_64BYTE);
			scratch = serial_in(port, UART_IIR) >> 5;
			if (scratch == 6)
				port->type = PORT_16750;
		}
		serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO);
	}
#if defined(CONFIG_SERIAL_RSA) && defined(MODULE)
	/*
	 * Only probe for RSA ports if we got the region.
	 */
	if (port->type == PORT_16550A && probeflags & PROBE_RSA) {
		int i;

		for (i = 0 ; i < PORT_RSA_MAX ; ++i) {
			if (!probe_rsa[i] && !force_rsa[i])
				break;
			if (((probe_rsa[i] != port->iobase) ||
			     check_region(port->iobase + UART_RSA_BASE, 16)) &&
			    (force_rsa[i] != port->iobase))
				continue;
			if (!enable_rsa(port))
				continue;
			port->type = PORT_RSA;
			port->uartclk = SERIAL_RSA_BAUD_BASE * 16;
			break;
		}
	}
#endif
	serial_outp(port, UART_LCR, save_lcr);
	if (port->type == PORT_16450) {
		scratch = serial_in(port, UART_SCR);
		serial_outp(port, UART_SCR, 0xa5);
		status1 = serial_in(port, UART_SCR);
		serial_outp(port, UART_SCR, 0x5a);
		status2 = serial_in(port, UART_SCR);
		serial_outp(port, UART_SCR, scratch);

		if ((status1 != 0xa5) || (status2 != 0x5a))
			port->type = PORT_8250;
	}
	port->fifosize = uart_config[port->type].dfl_xmit_fifo_size;

	if (port->type == PORT_UNKNOWN) {
		restore_flags(flags);
		return;
	}

#ifdef CONFIG_SERIAL_RSA
	if (port->iobase && port->type == PORT_RSA) {
		release_region(port->iobase, 8);
		request_region(port->iobase + UART_RSA_BASE, 16,
			       "serial_rsa");
	}
#endif

	/*
	 * Reset the UART.
	 */
#ifdef CONFIG_SERIAL_RSA
	if (port->type == PORT_RSA)
		serial_outp(port, UART_RSA_FRR, 0);
#endif
	serial_outp(port, UART_MCR, save_mcr);
	serial_outp(port, UART_FCR, (UART_FCR_ENABLE_FIFO |
				     UART_FCR_CLEAR_RCVR |
				     UART_FCR_CLEAR_XMIT));
	serial_outp(port, UART_FCR, 0);
	(void)serial_in(port, UART_RX);
	serial_outp(port, UART_IER, 0);
	
	restore_flags(flags);
}

static void autoconfig_irq(struct uart_port *port)
{
	unsigned char save_mcr, save_ier;
	unsigned long irqs;
	int irq;

#ifdef CONFIG_SERIAL_MANY_PORTS
	unsigned char save_ICP = 0;
	unsigned short ICP = 0;

	if (port->flags & ASYNC_FOURPORT) {
		ICP = (port->iobase & 0xfe0) | 0x1f;
		save_ICP = inb_p(ICP);
		outb_p(0x80, ICP);
		(void) inb_p(ICP);
	}
#endif

	/* forget possible initially masked and pending IRQ */
	probe_irq_off(probe_irq_on());
	save_mcr = serial_inp(port, UART_MCR);
	save_ier = serial_inp(port, UART_IER);
	serial_outp(port, UART_MCR, UART_MCR_OUT1 | UART_MCR_OUT2);
	
	irqs = probe_irq_on();
	serial_outp(port, UART_MCR, 0);
	udelay (10);
	if (port->flags & ASYNC_FOURPORT)  {
		serial_outp(port, UART_MCR,
			    UART_MCR_DTR | UART_MCR_RTS);
	} else {
		serial_outp(port, UART_MCR,
			    UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2);
	}
	serial_outp(port, UART_IER, 0x0f);	/* enable all intrs */
	(void)serial_inp(port, UART_LSR);
	(void)serial_inp(port, UART_RX);
	(void)serial_inp(port, UART_IIR);
	(void)serial_inp(port, UART_MSR);
	serial_outp(port, UART_TX, 0xFF);
	udelay (20);
	irq = probe_irq_off(irqs);

	serial_outp(port, UART_MCR, save_mcr);
	serial_outp(port, UART_IER, save_ier);
#ifdef CONFIG_SERIAL_MANY_PORTS
	if (port->flags & ASYNC_FOURPORT)
		outb_p(save_ICP, ICP);
#endif
	port->irq = (irq > 0)? irq : 0;
}

static void serial8250_stop_tx(struct uart_port *port, u_int from_tty)
{
	if (port->port_ier & UART_IER_THRI) {
		port->port_ier &= ~UART_IER_THRI;
		serial_out(port, UART_IER, port->port_ier);
	}
	if (port->type == PORT_16C950) {
		port->port_acr |= UART_ACR_TXDIS;
		serial_icr_write(port, UART_ACR, port->port_acr);
	}
}

static void serial8250_start_tx(struct uart_port *port, u_int nonempty, u_int from_tty)
{
	if (nonempty && !(port->port_ier & UART_IER_THRI)) {
		port->port_ier |= UART_IER_THRI;
		serial_out(port, UART_IER, port->port_ier);
	}
	/*
	 * We only do this from uart_start
	 */
	if (from_tty && port->type == PORT_16C950) {
		port->port_acr &= ~UART_ACR_TXDIS;
		serial_icr_write(port, UART_ACR, port->port_acr);
	}
}

static void serial8250_stop_rx(struct uart_port *port)
{
	port->port_ier &= ~UART_IER_RLSI;
	port->read_status_mask &= ~UART_LSR_DR;
	serial_out(port, UART_IER, port->port_ier);
}

static void serial8250_enable_ms(struct uart_port *port)
{
	port->port_ier |= UART_IER_MSI;
	serial_out(port, UART_IER, port->port_ier);
}

static _INLINE_ void
receive_chars(struct uart_info *info, int *status, struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	struct uart_port *port = info->port;
	unsigned char ch;
	int max_count = 256;

	do {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			tty->flip.tqueue.routine((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				return; // if TTY_DONT_FLIP is set
		}
		ch = serial_inp(port, UART_RX);
		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		port->icount.rx++;

		if (*status & (UART_LSR_BI | UART_LSR_PE |
			       UART_LSR_FE | UART_LSR_OE)) {
			/*
			 * For statistics only
			 */
			if (*status & UART_LSR_BI) {
				*status &= ~(UART_LSR_FE | UART_LSR_PE);
				port->icount.brk++;
				/*
				 * We do the SysRQ and SAK checking
				 * here because otherwise the break
				 * may get masked by ignore_status_mask
				 * or read_status_mask.
				 */
				uart_handle_break(info, &sercons);
			} else if (*status & UART_LSR_PE)
				port->icount.parity++;
			else if (*status & UART_LSR_FE)
				port->icount.frame++;
			if (*status & UART_LSR_OE)
				port->icount.overrun++;

			/*
			 * Mask off conditions which should be ingored.
			 */
			*status &= port->read_status_mask;

#ifdef CONFIG_SERIAL_CONSOLE
			if (port->line == sercons.index) {
				/* Recover the break flag from console xmit */
				*status |= lsr_break_flag;
				lsr_break_flag = 0;
			}
#endif
			if (*status & UART_LSR_BI) {
#ifdef SERIAL_DEBUG_INTR
				printk("handling break....");
#endif
				*tty->flip.flag_buf_ptr = TTY_BREAK;
			} else if (*status & UART_LSR_PE)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (*status & UART_LSR_FE)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}
		if (uart_handle_sysrq_char(info, ch, regs))
			goto ignore_char;
		if ((*status & port->ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((*status & UART_LSR_OE) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			*tty->flip.flag_buf_ptr = TTY_OVERRUN;
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
	ignore_char:
		*status = serial_inp(port, UART_LSR);
	} while ((*status & UART_LSR_DR) && (max_count-- > 0));
	tty_flip_buffer_push(tty);
}

static _INLINE_ void transmit_chars(struct uart_info *info, int *intr_done)
{
	struct uart_port *port = info->port;
	int count;

	if (port->x_char) {
		serial_outp(port, UART_TX, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		if (intr_done)
			*intr_done = 0;
		return;
	}
	if (info->xmit.head == info->xmit.tail
	    || info->tty->stopped
	    || info->tty->hw_stopped) {
		serial8250_stop_tx(port, 0);
		return;
	}

	count = port->fifosize;
	do {
		serial_out(port, UART_TX, info->xmit.buf[info->xmit.tail]);
		info->xmit.tail = (info->xmit.tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (info->xmit.head == info->xmit.tail)
			break;
	} while (--count > 0);

	if (CIRC_CNT(info->xmit.head, info->xmit.tail, UART_XMIT_SIZE) <
			WAKEUP_CHARS)
		uart_event(info, EVT_WRITE_WAKEUP);

#ifdef SERIAL_DEBUG_INTR
	printk("THRE...");
#endif
	if (intr_done)
		*intr_done = 0;

	if (info->xmit.head == info->xmit.tail)
		serial8250_stop_tx(info->port, 0);
}

static _INLINE_ void check_modem_status(struct uart_info *info)
{
	struct uart_port *port = info->port;
	int status;

	status = serial_in(port, UART_MSR);

	if (status & UART_MSR_ANY_DELTA) {
		if (status & UART_MSR_TERI)
			port->icount.rng++;
		if (status & UART_MSR_DDSR)
			port->icount.dsr++;
		if (status & UART_MSR_DDCD)
			uart_handle_dcd_change(info, status & UART_MSR_DCD);
		if (status & UART_MSR_DCTS)
			uart_handle_cts_change(info, status & UART_MSR_CTS);

		wake_up_interruptible(&info->delta_msr_wait);
	}
}

/*
 * This handles the interrupt from one port.
 */
static inline void
serial8250_handle_port(struct uart_info *info, struct pt_regs *regs)
{
	int status = serial_inp(info->port, UART_LSR);
#ifdef SERIAL_DEBUG_INTR
	printk("status = %x...", status);
#endif
	if (status & UART_LSR_DR)
		receive_chars(info, &status, regs);
	check_modem_status(info);
	if (status & UART_LSR_THRE)
		transmit_chars(info, 0);
}

#ifdef CONFIG_SERIAL_SHARE_IRQ
/*
 * This is the serial driver's generic interrupt routine
 */
static void rs_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info, *end_mark = NULL;
	int pass_counter = 0;
#ifdef CONFIG_SERIAL_MULTIPORT
	int first_multi = 0;
	unsigned long port_monitor = rs_multiport[irq].port_monitor;
#endif

#ifdef SERIAL_DEBUG_INTR
	printk("rs_interrupt(%d)...", irq);
#endif

	info = *(struct uart_info **)dev_id;
	if (!info)
		return;

#ifdef CONFIG_SERIAL_MULTIPORT
	if (port_monitor)
		first_multi = inb(port_monitor);
#endif

	do {
		if (!info->tty ||
		    (serial_in(info->port, UART_IIR) & UART_IIR_NO_INT)) {
		    	if (!end_mark)
		    		end_mark = info;
			goto next;
		}
#ifdef SERIAL_DEBUG_INTR
		printk("IIR = %x...", serial_in(info, UART_IIR));
#endif
		end_mark = NULL;

		serial8250_handle_port(info, regs);

	next:
		info = info->next_info;
		if (info)
			continue;
		info = *(struct uart_info **)dev_id;
		if (pass_counter++ > RS_ISR_PASS_LIMIT) {
#if 0
			printk("rs loop break\n");
#endif
			break; /* Prevent infinite loops */
		}
	} while (end_mark != info);
#ifdef CONFIG_SERIAL_MULTIPORT
	if (port_monitor)
		printk("rs port monitor (normal) irq %d: 0x%x, 0x%x\n",
			info->port->irq, first_multi, inb(port_monitor));
#endif
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}
#endif

/*
 * This is the serial driver's interrupt routine for a single port
 */
static void rs_interrupt_single(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info;
	int pass_counter = 0;
#ifdef CONFIG_SERIAL_MULTIPORT
	int first_multi = 0;
	unsigned long port_monitor = rs_multiport[irq].port_monitor;
#endif

#ifdef SERIAL_DEBUG_INTR
	printk("rs_interrupt_single(%d)...", irq);
#endif

	info = *(struct uart_info **)dev_id;
	if (!info || !info->tty)
		return;

#ifdef CONFIG_SERIAL_MULTIPORT
	if (port_monitor)
		first_multi = inb(port_monitor);
#endif

	do {
		serial8250_handle_port(info, regs);
		if (pass_counter++ > RS_ISR_PASS_LIMIT) {
#if 0
			printk("rs_single loop break.\n");
#endif
			break;
		}
#ifdef SERIAL_DEBUG_INTR
		printk("IIR = %x...", serial_in(info->port, UART_IIR));
#endif
	} while (!(serial_in(info->port, UART_IIR) & UART_IIR_NO_INT));
#ifdef CONFIG_SERIAL_MULTIPORT
	if (port_monitor)
		printk("rs port monitor (single) irq %d: 0x%x, 0x%x\n",
			info->port->irq, first_multi, inb(port_monitor));
#endif
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}

#ifdef CONFIG_SERIAL_MULTIPORT
/*
 * This is the serial driver's interrupt routine for multiport boards
 */
static void rs_interrupt_multi(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info;
	int pass_counter = 0;
	struct rs_multiport_struct *multi = &rs_multiport[irq];
	int first_multi = 0;

#ifdef SERIAL_DEBUG_INTR
	printk("rs_interrupt_multi(%d)...", irq);
#endif

	info = *(struct uart_info **)dev_id;
	if (!info)
		return;

	if (!multi->port1) {
		/* should never happen */
		printk("rs_interrupt_multi: port1 NULL!\n");
		return;
	}
	if (multi->port_monitor)
		first_multi = inb(multi->port_monitor);

	while (1) {
		if (!info->tty ||
		    (serial_in(info->port, UART_IIR) & UART_IIR_NO_INT))
			goto next;

		serial8250_handle_port(info, regs);

	next:
		info = info->next;
		if (info)
			continue;
		info = *(struct uart_info **)dev_id;

		/*
		 * The user was a bonehead, and misconfigured their
		 * multiport info.  Rather than lock up the kernel
		 * in an infinite loop, if we loop too many times,
		 * print a message and break out of the loop.
		 */
		if (pass_counter++ > RS_ISR_PASS_LIMIT) {
			printk("Misconfigured multiport serial info "
			       "for irq %d.  Breaking out irq loop\n", irq);
			break;
		}
		if (multi->port_monitor)
			printk("rs port monitor irq %d: 0x%x, 0x%x\n",
				info->port->irq, first_multi,
				inb(multi->port_monitor));
		if ((inb(multi->port1) & multi->mask1) != multi->match1)
			continue;
		if (!multi->port2)
			break;
		if ((inb(multi->port2) & multi->mask2) != multi->match2)
			continue;
		if (!multi->port3)
			break;
		if ((inb(multi->port3) & multi->mask3) != multi->match3)
			continue;
		if (!multi->port4)
			break;
		if ((inb(multi->port4) & multi->mask4) != multi->match4)
			continue;
		break;
	}
#ifdef SERIAL_DEBUG_INTR
	printk("end.\n");
#endif
}
#endif

static u_int serial8250_tx_empty(struct uart_port *port)
{
	return serial_in(port, UART_LSR) & UART_LSR_TEMT ? TIOCSER_TEMT : 0;
}

static int serial8250_get_mctrl(struct uart_port *port)
{
	unsigned long flags;
	unsigned char status;
	int ret;

	save_flags(flags); cli();
	status = serial_in(port, UART_MSR);
	restore_flags(flags);

	ret = 0;
	if (status & UART_MSR_DCD)
		ret |= TIOCM_CAR;
	if (status & UART_MSR_RI)
		ret |= TIOCM_RNG;
	if (status & UART_MSR_DSR)
		ret |= TIOCM_DSR;
	if (status & UART_MSR_CTS)
		ret |= TIOCM_CTS;
	return ret;
}

static void serial8250_set_mctrl(struct uart_port *port, u_int mctrl)
{
	unsigned char mcr = ALPHA_KLUDGE_MCR;

#ifdef CONFIG_SERIAL_MANY_PORTS
	if (port->flags & ASYNC_FOURPORT) {
		if (port->irq == 0)
			mcr |= UART_MCR_OUT1;
	} else
#endif
	if (port->irq != 0)
		mcr |= UART_MCR_OUT2;

	if (mctrl & TIOCM_RTS)
		mcr |= UART_MCR_RTS;
	if (mctrl & TIOCM_DTR)
		mcr |= UART_MCR_DTR;
	if (mctrl & TIOCM_OUT1)
		mcr |= UART_MCR_OUT1;
	if (mctrl & TIOCM_OUT2)
		mcr |= UART_MCR_OUT2;
	if (mctrl & TIOCM_LOOP)
		mcr |= UART_MCR_LOOP;

	serial_out(port, UART_MCR, mcr);
}

static void serial8250_break_ctl(struct uart_port *port, int break_state)
{
	if (break_state == -1)
		port->port_lcr |= UART_LCR_SBC;
	else
		port->port_lcr &= ~UART_LCR_SBC;
	serial_out(port, UART_LCR, port->port_lcr);
}

static int serial8250_startup(struct uart_port *port, struct uart_info *info)
{
	void (*handler)(int, void *, struct pt_regs *);
	int retval;

	if (port->type == PORT_16C950) {
		/* Wake up and initialize UART */
		port->port_acr = 0;
		serial_outp(port, UART_LCR, 0xBF);
		serial_outp(port, UART_EFR, UART_EFR_ECB);
		serial_outp(port, UART_IER, 0);
		serial_outp(port, UART_LCR, 0);
		serial_icr_write(port, UART_CSR, 0); /* Reset the UART */
		serial_outp(port, UART_LCR, 0xBF);
		serial_outp(port, UART_EFR, UART_EFR_ECB);
		serial_outp(port, UART_LCR, 0);
	}

#ifdef CONFIG_SERIAL_RSA
	/*
	 * If this is an RSA port, see if we can kick it up to the
	 * higher speed clock.
	 */
	if (port->type == PORT_RSA) {
		if (port->uartclk != SERIAL_RSA_BAUD_BASE * 16 &&
		    enable_rsa(port))
			port->uartclk = SERIAL_RSA_BAUD_BASE * 16;
		if (port->uartclk == SERIAL_RSA_BAUD_BASE * 16)
			serial_outp(port, UART_RSA_FRR, 0);
	}
#endif

	/*
	 * Clear the FIFO buffers and disable them.
	 * (they will be reeanbled in change_speed())
	 */
	if (uart_config[port->type].flags & UART_CLEAR_FIFO) {
		serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO);
		serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO |
				UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
		serial_outp(port, UART_FCR, 0);
	}

	/*
	 * Clear the interrupt registers.
	 */
	(void) serial_inp(port, UART_LSR);
	(void) serial_inp(port, UART_RX);
	(void) serial_inp(port, UART_IIR);
	(void) serial_inp(port, UART_MSR);

	/*
	 * At this point, there's no way the LSR could still be 0xff;
	 * if it is, then bail out, because there's likely no UART
	 * here.
	 */
	if (!(port->flags & ASYNC_BUGGY_UART) &&
	    (serial_inp(port, UART_LSR) == 0xff)) {
		printk("ttyS%d: LSR safety check engaged!\n", port->line);
		return -ENODEV;
	}

	/*
	 * Allocate the IRQ if necessary
	 */
	if (port->irq && (!IRQ_ports[port->irq] ||
			  !IRQ_ports[port->irq]->next_info)) {
		handler = rs_interrupt_single;
		if (IRQ_ports[port->irq]) {
#ifdef CONFIG_SERIAL_SHARE_IRQ
			handler = rs_interrupt;
			free_irq(port->irq, &IRQ_ports[port->irq]);
#ifdef CONFIG_SERIAL_MULTIPORT
			if (rs_multiport[port->irq].port1)
				handler = serial8250_interrupt_multi;
#endif
#else
			return -EBUSY;
#endif /* CONFIG_SERIAL_SHARE_IRQ */
		}

		retval = request_irq(port->irq, handler, SA_SHIRQ,
				     "serial", &IRQ_ports[port->irq]);
		if (retval)
			return retval;
	}

	/*
	 * Insert serial port into IRQ chain.
	 */
	info->next_info = IRQ_ports[port->irq];
	IRQ_ports[port->irq] = info;

	/*
	 * Now, initialize the UART
	 */
	serial_outp(port, UART_LCR, UART_LCR_WLEN8);

	/* we do MCR initialisation later -- rmk */

	/*
	 * Finally, enable interrupts.  Note: Modem status interrupts
	 * are set via change_speed(), which will be occuring imminently
	 * anyway, so we don't enable them here.
	 */
	port->port_ier = UART_IER_RLSI | UART_IER_RDI;
	serial_outp(port, UART_IER, port->port_ier);

#ifdef CONFIG_SERIAL_MANY_PORTS
	if (port->flags & ASYNC_FOURPORT) {
		unsigned int ICP;
		/*
		 * Enable interrupts on the AST Fourport board
		 */
		ICP = (port->iobase & 0xfe0) | 0x01f;
		outb_p(0x80, ICP);
		(void) inb_p(ICP);
	}
#endif

	/*
	 * And clear the interrupt registers again for luck.
	 */
	(void) serial_inp(port, UART_LSR);
	(void) serial_inp(port, UART_RX);
	(void) serial_inp(port, UART_IIR);
	(void) serial_inp(port, UART_MSR);

	return 0;
}

static void serial8250_shutdown(struct uart_port *port, struct uart_info *info)
{
	struct uart_info **infop;
	int retval;

	/*
	 * First unlink the serial port from the IRQ chain...
	 */
	for (infop = &IRQ_ports[port->irq]; *infop; infop = &(*infop)->next_info)
		if (*infop == info)
			break;

	if (*infop == info)
		*infop = info->next_info;

	/*
	 * Free the IRQ, if necessary
	 */
	if (port->irq && (!IRQ_ports[port->irq] ||
			  !IRQ_ports[port->irq]->next_info)) {
		free_irq(port->irq, &IRQ_ports[port->irq]);
		if (IRQ_ports[port->irq]) {
			retval = request_irq(port->irq, rs_interrupt_single,
					     SA_SHIRQ, "serial", &IRQ_ports[port->irq]);
			if (retval)
				printk("serial shutdown: request_irq: error %d"
				       " couldn't reacquire IRQ.\n", retval);
		}
	}

	/*
	 * Disable all intrs
	 */
	port->port_ier = 0;
	serial_outp(port, UART_IER, 0);

	/*
	 * Disable break condition and FIFOs
	 */
	serial_out(port, UART_LCR, serial_inp(port, UART_LCR) & ~UART_LCR_SBC);
	serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO |
				    UART_FCR_CLEAR_RCVR |
				    UART_FCR_CLEAR_XMIT);
	serial_outp(port, UART_FCR, 0);

#ifdef CONFIG_SERIAL_RSA
	/*
	 * Reset the RSA board back to 115kbps compat mode.
	 */
	if (port->type == PORT_RSA &&
	    port->uartclk == SERIAL_RSA_BAUD_BASE * 16 &&
	    disable_rsa(port))
		port->uartclk = SERIAL_RSA_BAUD_BASE_LO * 16;
#endif

	/*
	 * Read data port to reset things
	 */
	(void) serial_in(port, UART_RX);
}

static void serial8250_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	unsigned char cval, fcr = 0;
	unsigned long flags;

	switch (cflag & CSIZE) {
	case CS5:	cval = 0x00;	break;
	case CS6:	cval = 0x01;	break;
	case CS7:	cval = 0x02;	break;
	default:
	case CS8:	cval = 0x03;	break;
	}

	if (cflag & CSTOPB)
		cval |= 0x04;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
#ifdef CMSPAR
	if (cflag & CMSPAR)
		cval |= UART_LCR_SPAR;
#endif

	/*
	 * Work around a bug in the Oxford Semiconductor 952 rev B
	 * chip which causes it to seriously miscalculate baud rates
	 * when DLL is 0.
	 */
	if ((quot & 0xff) == 0 && port->type == PORT_16C950 &&
	    port->port_rev == 0x5201)
		quot ++;

	if (uart_config[port->type].flags & UART_USE_FIFO) {
		if ((port->uartclk / quot) < (2400 * 16))
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_1;
#ifdef CONFIG_SERIAL_RSA
		else if (port->type == PORT_RSA)
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_14;
#endif
		else
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_TRIGGER_8;
	}
	if (port->type == PORT_16750)
		fcr |= UART_FCR7_64BYTE;

	port->read_status_mask = UART_LSR_OE | UART_LSR_THRE | UART_LSR_DR;
	if (iflag & IGNPAR)
		port->read_status_mask |= UART_LSR_FE | UART_LSR_PE;
	if (iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UART_LSR_BI;

	/*
	 * Characteres to ignore
	 */
	port->ignore_status_mask = 0;
	if (iflag & IGNPAR)
		port->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
	if (iflag & IGNBRK) {
		port->ignore_status_mask |= UART_LSR_BI;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns too (for real raw support).
		 */
		if (iflag & IGNPAR)
			port->ignore_status_mask |= UART_LSR_OE;
	}

	/*
	 * ignore all characters if CREAD is not set
	 */
	if ((cflag & CREAD) == 0)
		port->ignore_status_mask |= UART_LSR_DR;

	/*
	 * CTS flow control flag and modem status interrupts
	 */
	port->port_ier &= ~UART_IER_MSI;
	if (port->flags & ASYNC_HARDPPS_CD || cflag & CRTSCTS ||
	    !(cflag & CLOCAL))
		port->port_ier |= UART_IER_MSI;

	/*
	 * Ok, we're now changing the port state.  Do it with
	 * interrupts disabled.
	 */
	save_flags(flags); cli();
	serial_out(port, UART_IER, port->port_ier);

	if (uart_config[port->type].flags & UART_STARTECH) {
		serial_outp(port, UART_LCR, 0xBF);
		serial_outp(port, UART_EFR, cflag & CRTSCTS ? UART_EFR_CTS :0);
	}
	serial_outp(port, UART_LCR, cval | UART_LCR_DLAB);	/* set DLAB */
	serial_outp(port, UART_DLL, quot & 0xff);	/* LS of divisor */
	serial_outp(port, UART_DLM, quot >> 8);		/* MS of divisor */
	if (port->type == PORT_16750)
		serial_outp(port, UART_FCR, fcr);	/* set fcr */
	serial_outp(port, UART_LCR, cval);		/* reset DLAB */
	port->port_lcr = cval;				/* Save LCR */
	if (port->type != PORT_16750) {
		if (fcr & UART_FCR_ENABLE_FIFO) {
			/* emulated UARTs (Lucent Venus 167x) need two steps */
			serial_outp(port, UART_FCR, UART_FCR_ENABLE_FIFO);
		}
		serial_outp(port, UART_FCR, fcr);	/* set fcr */
	}
	restore_flags(flags);
}

static void serial8250_pm(struct uart_port *port, u_int state, u_int oldstate)
{
	if (state) {
		/* sleep */
		if (uart_config[port->type].flags & UART_STARTECH) {
			/* Arrange to enter sleep mode */
			serial_outp(port, UART_LCR, 0xBF);
			serial_outp(port, UART_EFR, UART_EFR_ECB);
			serial_outp(port, UART_LCR, 0);
			serial_outp(port, UART_IER, UART_IERX_SLEEP);
			serial_outp(port, UART_LCR, 0xBF);
			serial_outp(port, UART_EFR, 0);
			serial_outp(port, UART_LCR, 0);
		}
		if (port->type == PORT_16750) {
			/* Arrange to enter sleep mode */
			serial_outp(port, UART_IER, UART_IERX_SLEEP);
		}
	} else {
		/* wake */
		if (uart_config[port->type].flags & UART_STARTECH) {
			/* Wake up UART */
			serial_outp(port, UART_LCR, 0xBF);
			serial_outp(port, UART_EFR, UART_EFR_ECB);
			/*
			 * Turn off LCR == 0xBF so we actually set the IER
			 * register on the XR16C850
			 */
			serial_outp(port, UART_LCR, 0);
			serial_outp(port, UART_IER, 0);
			/*
			 * Now reset LCR so we can turn off the ECB bit
			 */
			serial_outp(port, UART_LCR, 0xBF);
			serial_outp(port, UART_EFR, 0);
			/*
			 * For a XR16C850, we need to set the trigger levels
			 */
			if (port->type == PORT_16850) {
				serial_outp(port, UART_FCTR, UART_FCTR_TRGD |
						UART_FCTR_RX);
				serial_outp(port, UART_TRG, UART_TRG_96);
				serial_outp(port, UART_FCTR, UART_FCTR_TRGD |
						UART_FCTR_TX);
				serial_outp(port, UART_TRG, UART_TRG_96);
			}
			serial_outp(port, UART_LCR, 0);
		}

		if (port->type == PORT_16750) {
			/* Wake up UART */
			serial_outp(port, UART_IER, 0);
		}
	}
}

/*
 * Resource handling.  This is complicated by the fact that resources
 * depend on the port type.  Maybe we should be claiming the standard
 * 8250 ports, and then trying to get other resources as necessary?
 */
static struct resource *
serial8250_request_standard_resource(struct uart_port *port)
{
	struct resource *root;
	unsigned long start;

	if (port->iotype == SERIAL_IO_MEM) {
		start = port->mapbase;
		root  = &iomem_resource;
	} else {
		start = port->iobase;
		root  = &ioport_resource;
	}

	return __request_region(root, start, 8 << port->regshift,
				"serial");
}

static struct resource *
serial8250_request_rsa_resource(struct uart_port *port)
{
	struct resource *root;
	unsigned long start;

	if (port->iotype == SERIAL_IO_MEM) {
		start = port->mapbase;
		root  = &iomem_resource;
	} else {
		start = port->iobase;
		root  = &ioport_resource;
	}

	start += UART_RSA_BASE << port->regshift;

	return __request_region(root, start, 8 << port->regshift,
				"serial-rsa");
}

static void serial8250_release_port(struct uart_port *port)
{
	struct resource *root;
	unsigned long start, offset = 0, size = 0;

	if (port->iotype == SERIAL_IO_MEM) {
		start = port->mapbase;
		root = &iomem_resource;
	} else {
		start = port->iobase;
		root = &ioport_resource;
	}

	if (port->type == PORT_RSA) {
		offset = UART_RSA_BASE;
		size = 8;
	}

	offset <<= port->regshift;
	size <<= port->regshift;

	/*
	 * Unmap the area.
	 */
	if (port->iotype == SERIAL_IO_MEM && port->mapbase) {
		iounmap(port->membase + offset);
		port->membase = NULL;
	}

	/*
	 * Release any extra region we may have first.
	 */
	if (offset)
		__release_region(root, start + offset, size);

	/*
	 * Release the standard resource.
	 */
	__release_region(root, start, 8 << port->regshift);
}

static int serial8250_request_port(struct uart_port *port)
{
	struct resource *res = NULL, *res_rsa = NULL;
	int ret = -EBUSY;

	if (port->type == PORT_RSA) {
		res_rsa = serial8250_request_rsa_resource(port);
		if (!res_rsa)
			return -EBUSY;
	}

	res = serial8250_request_standard_resource(port);

	/*
	 * If we have a mapbase, then request that as well.
	 */
	if (res != NULL && port->iotype == SERIAL_IO_MEM &&
	    port->mapbase) {
		int size = res->end - res->start + 1;

		port->membase = ioremap(port->mapbase, size);
		if (!port->membase)
			ret = -ENOMEM;
	}

	if (ret) {
		if (res_rsa)
			release_resource(res_rsa);
		if (res)
			release_resource(res);
	}
	return ret;
}

static void serial8250_config_port(struct uart_port *port, int flags)
{
	struct resource *res_std, *res_rsa;
	int probeflags = PROBE_ANY;

	/*
	 * Find the region that we can probe for.  This in turn
	 * tells us whether we can probe for the type of port.
	 */
	res_std = serial8250_request_standard_resource(port);
	if (!res_std)
		return;

	res_rsa = serial8250_request_rsa_resource(port);
	if (!res_rsa)
		probeflags &= ~PROBE_RSA;
	
	if (flags & UART_CONFIG_TYPE)
		autoconfig(port, probeflags);
	if (port->type != PORT_UNKNOWN && flags & UART_CONFIG_IRQ)
		autoconfig_irq(port);

	/*
	 * If the port wasn't an RSA port, release the resource.
	 */
	if (port->type != PORT_RSA && res_rsa)
		release_resource(res_rsa);

	if (port->type == PORT_UNKNOWN)
		release_resource(res_std);
}

static int
serial8250_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	if (ser->irq >= NR_IRQS || ser->irq < 0 ||
	    ser->baud_base < 9600 || ser->type < PORT_UNKNOWN ||
	    ser->type > PORT_MAX_8250 || ser->type == PORT_CIRRUS ||
	    ser->type == PORT_STARTECH)
		return -EINVAL;
	return 0;
}

static struct uart_ops serial8250_pops = {
	tx_empty:	serial8250_tx_empty,
	set_mctrl:	serial8250_set_mctrl,
	get_mctrl:	serial8250_get_mctrl,
	stop_tx:	serial8250_stop_tx,
	start_tx:	serial8250_start_tx,
	stop_rx:	serial8250_stop_rx,
	enable_ms:	serial8250_enable_ms,
	break_ctl:	serial8250_break_ctl,
	startup:	serial8250_startup,
	shutdown:	serial8250_shutdown,
	change_speed:	serial8250_change_speed,
	pm:		serial8250_pm,
	release_port:	serial8250_release_port,
	request_port:	serial8250_request_port,
	config_port:	serial8250_config_port,
	verify_port:	serial8250_verify_port,
};

static struct uart_port serial8250_ports[UART_NR];

static void __init serial8250_isa_init_ports(void)
{
	static int first = 1;
	int i;

	if (!first)
		return;
	first = 0;

	for (i = 0; i < ARRAY_SIZE(old_serial_port); i++) {
		serial8250_ports[i].iobase  = old_serial_port[i].port;
		serial8250_ports[i].irq     = old_serial_port[i].irq;
		serial8250_ports[i].uartclk = old_serial_port[i].base_baud * 16;
		serial8250_ports[i].flags   = old_serial_port[i].flags;
		serial8250_ports[i].ops     = &serial8250_pops;
	}
}

#ifdef CONFIG_SERIAL_SERIAL8250_CONSOLE
#ifdef used_and_not_const_char_pointer
static int serial8250_console_read(struct uart_port *port, char *s, u_int count)
{
}
#endif

/*
 *	Wait for transmitter & holding register to empty
 */
static inline void wait_for_xmitr(struct uart_port *port)
{
	unsigned int status, tmout = 1000000;

	do {
		status = serial_in(port, UART_LSR);

		if (status & UART_LSR_BI)
			lsr_break_flag = UART_LSR_BI;

		if (--tmout == 0)
			break;
	} while ((status & BOTH_EMPTY) != BOTH_EMPTY);

	/* Wait for flow control if necessary */
	if (port->flags & ASYNC_CONS_FLOW) {
		tmout = 1000000;
		while (--tmout &&
		       ((serial_in(port, UART_MSR) & UART_MSR_CTS) == 0));
	}
}

/*
 *	Print a string to the serial port trying not to disturb
 *	any possible real use of the port...
 *
 *	The console_lock must be held when we get here.
 */
static void serial8250_console_write(struct console *co, const char *s, u_int count)
{
	struct uart_port *port = serial8250_ports + co->index;
	unsigned int ier;
	int i;

	/*
	 *	First save the UER then disable the interrupts
	 */
	ier = serial_in(port, UART_IER);
	serial_out(port, UART_IER, 0);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		wait_for_xmitr(port);

		/*
		 *	Send the character out.
		 *	If a LF, also do CR...
		 */
		serial_out(port, UART_TX, *s);
		if (*s == 10) {
			wait_for_xmitr(port);
			serial_out(port, UART_TX, 13);
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IER
	 */
	wait_for_xmitr(port);
	serial_out(port, UART_IER, ier);
}

static kdev_t serial8250_console_device(struct console *co)
{
	return MKDEV(TTY_MAJOR, 64 + co->index);
}

static int serial8250_console_wait_key(struct console *co)
{
	struct uart_port *port = serial8250_ports + co->index;
	int ier, c;

	/*
	 *	First save the IER then disable the interrupts so
	 *	that the real driver for the port does not get the
	 *	character.
	 */
	ier = serial_in(port, UART_IER);
	serial_out(port, UART_IER, 0);

	while ((serial_in(port, UART_LSR) & UART_LSR_DR) == 0);
	c = serial_in(port, UART_RX);

	/*
	 *	Restore the interrupts
	 */
	serial_out(port, UART_IER, ier);

	return c;
}

static int __init serial8250_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(serial8250_ports, UART_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console serial8250_console = {
	name:		"ttyS",
	write:		serial8250_console_write,
#ifdef used_and_not_const_char_pointer
	read:		serial8250_console_read,
#endif
	device:		serial8250_console_device,
	wait_key:	serial8250_console_wait_key,
	setup:		serial8250_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};

void __init serial8250_console_init(void)
{
	serial8250_isa_init_ports();
	register_console(&serial8250_console);
}

#define SERIAL8250_CONSOLE	&serial8250_console
#else
#define SERIAL8250_CONSOLE	NULL
#endif

static struct uart_driver serial8250_reg = {
	owner:			THIS_MODULE,
#ifdef CONFIG_DEVFS_FS
	normal_name:		"tts/%d",
	callout_name:		"cua/%d",
#else
	normal_name:		"ttyS",
	callout_name:		"cua",
#endif
	normal_major:		TTY_MAJOR,
	callout_major:		TTYAUX_MAJOR,
	normal_driver:		&normal,
	callout_driver:		&callout,
	table:			serial8250_table,
	termios:		serial8250_termios,
	termios_locked:		serial8250_termios_locked,
	minor:			64,
	nr:			ARRAY_SIZE(old_serial_port),
	port:			serial8250_ports,
	cons:			SERIAL8250_CONSOLE,
};

/*
 * register_serial and unregister_serial allows for 16x50 serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
 
/**
 *	register_serial - configure a 16x50 serial port at runtime
 *	@req: request structure
 *
 *	Configure the serial port specified by the request. If the
 *	port exists and is in use an error is returned. If the port
 *	is not currently in the table it is added.
 *
 *	The port is then probed and if neccessary the IRQ is autodetected
 *	If this fails an error is returned.
 *
 *	On success the port is ready to use and the line number is returned.
 */
int register_serial(struct serial_struct *req)
{
	struct uart_port port;

	port.iobase   = req->port;
	port.membase  = req->iomem_base;
	port.irq      = req->irq;
	port.uartclk  = req->baud_base * 16;
	port.fifosize = req->xmit_fifo_size;
	port.regshift = req->iomem_reg_shift;
	port.iotype   = req->io_type;
	port.flags    = req->flags | ASYNC_BOOT_AUTOCONF;

	if (HIGH_BITS_OFFSET)
		port.iobase |= req->port_high << HIGH_BITS_OFFSET;

	/*
	 * If a clock rate wasn't specified by the low level
	 * driver, then default to the standard clock rate.
	 */
	if (port.uartclk == 0)
		port.uartclk = BASE_BAUD * 16;

	return uart_register_port(&serial8250_reg, &port);
}

void unregister_serial(int line)
{
	uart_unregister_port(&serial8250_reg, line);
}

static int __init serial8250_init(void)
{
	serial8250_isa_init_ports();
	return uart_register_driver(&serial8250_reg);
}

static void __exit serial8250_exit(void)
{
	uart_unregister_driver(&serial8250_reg);
}

module_init(serial8250_init);
module_exit(serial8250_exit);

EXPORT_SYMBOL(register_serial);
EXPORT_SYMBOL(unregister_serial);
