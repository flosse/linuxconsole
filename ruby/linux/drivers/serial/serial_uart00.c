/*
 *  linux/drivers/char/serial_uart00.c
 *
 *  Driver for UART00 serial ports
 *
 *  Based on drivers/char/serial_amba.c, by ARM Limited & 
 *                                          Deep Blue Solutions Ltd.
 *  Copyright 2001 Altera Corporation
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
 * This is a generic driver for ARM UART00-type serial ports.  They
 * have a lot of 16550-like features, but are not register compatable.
 * Note that although they do have CTS, DCD and DSR inputs, they do
 * not have an RI input, nor do they have DTR or RTS outputs.  If
 * required, these have to be supplied via some other means (eg, GPIO)
 * and hooked into this driver.
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

#if defined(CONFIG_SERIAL_UART00_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/serial_core.h>
#include <asm/arch/excalibur.h>
#define UART00_TYPE (volatile unsigned int*)
#include <asm/arch/uart00.h>
#include <asm/arch/int_ctrl00.h>

#define UART_NR		2

#define SERIAL_UART00_NAME	"ttyUA"
#define SERIAL_UART00_MAJOR	204
#define SERIAL_UART00_MINOR	16      /* Temporary - will change in future */
#define SERIAL_UART00_NR	UART_NR
#define UART_PORT_SIZE 0x50

#define CALLOUT_UART00_NAME	"cuaua"
#define CALLOUT_UART00_MAJOR	205
#define CALLOUT_UART00_MINOR	16      /* Temporary - will change in future */
#define CALLOUT_UART00_NR	UART_NR



static struct tty_driver normal, callout;
static struct tty_struct *uart00_table[UART_NR];
static struct termios *uart00_termios[UART_NR], *uart00_termios_locked[UART_NR];
//static struct uart_state uart00_state[UART_NR];
static struct console uart00_console;

#define UART00_ISR_PASS_LIMIT	256

/*
 * Access macros for the UART00 UARTs
 */
#define UART_GET_INT_STATUS(p)	inl(UART_ISR((p)->membase))
#define UART_PUT_IES(p, c)      outl(c,UART_IES((p)->membase))
#define UART_GET_IES(p)         inl(UART_IES((p)->membase))
#define UART_PUT_IEC(p, c)      outl(c,UART_IEC((p)->membase))
#define UART_GET_IEC(p)         inl(UART_IEC((p)->membase))
#define UART_PUT_CHAR(p, c)     outl(c,UART_TD((p)->membase))
#define UART_GET_CHAR(p)        inl(UART_RD((p)->membase))
#define UART_GET_RSR(p)         inl(UART_RSR((p)->membase))
#define UART_GET_RDS(p)         inl(UART_RDS((p)->membase))
#define UART_GET_MSR(p)         inl(UART_MSR((p)->membase))
#define UART_GET_MCR(p)         inl(UART_MCR((p)->membase))
#define UART_PUT_MCR(p, c)      outl(c,UART_MCR((p)->membase))
#define UART_GET_MC(p)          inl(UART_MC((p)->membase))
#define UART_PUT_MC(p, c)       outl(c,UART_MC((p)->membase))
#define UART_GET_TSR(p)         inl(UART_TSR((p)->membase))
#define UART_GET_DIV_HI(p)	inl(UART_DIV_HI((p)->membase))
#define UART_PUT_DIV_HI(p,c)	outl(c,UART_DIV_HI((p)->membase))
#define UART_GET_DIV_LO(p)	inl(UART_DIV_LO((p)->membase))
#define UART_PUT_DIV_LO(p,c)	outl(c,UART_DIV_LO((p)->membase))
#define UART_RX_DATA(s)		((s) & UART_RSR_RX_LEVEL_MSK)
#define UART_TX_READY(s)	(((s) & UART_TSR_TX_LEVEL_MSK) < 15)
//#define UART_TX_EMPTY(p)	((UART_GET_FR(p) & UART00_UARTFR_TMSK) == 0)

static void uart00_stop_tx(struct uart_port *port, u_int from_tty)
{

	UART_PUT_IEC(port, UART_IEC_TIE_MSK);
}

static void uart00_stop_rx(struct uart_port *port)
{

	UART_PUT_IEC(port, UART_IEC_RE_MSK);
}

static void uart00_enable_ms(struct uart_port *port)
{

	UART_PUT_IES(port, UART_IES_ME_MSK);
}

static void
uart00_rx_chars(struct uart_info *info, struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	unsigned int status, ch, rds, flg, ignored = 0;
	struct uart_port *port = info->port;
	

	status = UART_GET_RSR(port);
	while (UART_RX_DATA(status)) {

		/* 
		 * We need to read rds before reading the 
		 * character from the fifo
		 */
		rds = UART_GET_RDS(port);
		ch = UART_GET_CHAR(port);
		port->icount.rx++;

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			goto ignore_char;

		flg = TTY_NORMAL;

		/*
		 * Note that the error handling code is
		 * out of the main execution path
		 */
		if (rds & (UART_RDS_BI_MSK |UART_RDS_FE_MSK|
			   UART_RDS_PE_MSK |UART_RDS_PE_MSK))
			goto handle_error;

		if (uart_handle_sysrq_char(info, ch, regs))
			goto ignore_char;

	error_return:
		*tty->flip.flag_buf_ptr++ = flg;
		*tty->flip.char_buf_ptr++ = ch;
		tty->flip.count++;
	ignore_char:
		status = UART_GET_RSR(port);
	}
out:
	tty_flip_buffer_push(tty);
	return;

handle_error:
	if (rds & UART_RDS_BI_MSK) {
		status &= ~(UART_RDS_FE_MSK | UART_RDS_PE_MSK);
		port->icount.brk++;

#ifdef SUPPORT_SYSRQ
		if (uart_handle_break(info, &uart00_console))
			goto ignore_char;
#endif
	} else if (rds & UART_RDS_PE_MSK)
		port->icount.parity++;
	else if (rds & UART_RDS_PE_MSK)
		port->icount.frame++;
	if (rds & UART_RDS_OE_MSK)
		port->icount.overrun++;

	if (rds & port->ignore_status_mask) {
		if (++ignored > 100)
			goto out;
		goto ignore_char;
	}
	rds &= port->read_status_mask;

	if (rds & UART_RDS_BI_MSK)
		flg = TTY_BREAK;
	else if (rds & UART_RDS_PE_MSK)
		flg = TTY_PARITY;
	else if (rds & UART_RDS_FE_MSK)
		flg = TTY_FRAME;

	if (status & UART_RDS_OE_MSK) {
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

static void uart00_tx_chars(struct uart_info *info)
{
	int count;
	struct uart_port *port=info->port;

	if (port->x_char) {
		while((UART_GET_TSR(port)& UART_TSR_TX_LEVEL_MSK)==15);
		UART_PUT_CHAR(port, port->x_char);
		port->icount.tx++;
		port->x_char = 0;
		
		return;
	}
	if (info->xmit.head == info->xmit.tail
	    || info->tty->stopped
	    || info->tty->hw_stopped) {
		uart00_stop_tx(info->port, 0);
		return;
	}

	count = port->fifosize >> 1;
	do {
		while((UART_GET_TSR(port)& UART_TSR_TX_LEVEL_MSK)==15);
		UART_PUT_CHAR(port, info->xmit.buf[info->xmit.tail]);
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
		uart00_stop_tx(info->port, 0);
}

static void uart00_start_tx(struct uart_port *port, u_int nonempty, u_int from_tty)
{
	struct uart_info *info=(struct uart_info*)(port->iobase);

	if (nonempty) {
		UART_PUT_IES(port,UART_IES_TIE_MSK );		
		uart00_tx_chars(info);
		
	}
}

static void uart00_modem_status(struct uart_info *info)
{
	unsigned int status;
	struct uart_icount *icount = &info->port->icount;


	status = UART_GET_MSR(info->port);

	if (!status & (UART_MSR_DCTS_MSK | UART_MSR_DDSR_MSK | 
		       UART_MSR_TERI_MSK | UART_MSR_DDCD_MSK))
		return;

	if (status & UART_MSR_DDCD_MSK) {
		icount->dcd++;
#ifdef CONFIG_HARD_PPS
		if ((info->flags & ASYNC_HARDPPS_CD) &&
		    (status & UART_MSR_DCD_MSK))
			hardpps();
#endif
		if (info->flags & ASYNC_CHECK_CD) {
			if (status & UART_MSR_DCD_MSK)
				wake_up_interruptible(&info->open_wait);
			else if (!((info->flags & ASYNC_CALLOUT_ACTIVE) &&
				   (info->flags & ASYNC_CALLOUT_NOHUP))) {
				if (info->tty)
					tty_hangup(info->tty);
			}
		}
	}

	if (status & UART_MSR_DDSR_MSK)
		icount->dsr++;

	if (status & UART_MSR_DCTS_MSK) {
		icount->cts++;

		if (info->flags & ASYNC_CTS_FLOW) {
			status &= UART_MSR_CTS_MSK;

			if (info->tty->hw_stopped) {
				if (status) {
					info->tty->hw_stopped = 0;
					info->ops->start_tx(info->port, 1, 0);
					uart_event(info, EVT_WRITE_WAKEUP);
				}
			} else {
				if (!status) {
					info->tty->hw_stopped = 1;
					info->ops->stop_tx(info->port, 0);
				}
			}
		}
	}
	wake_up_interruptible(&info->delta_msr_wait);

}

static void uart00_int(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	unsigned int status, pass_counter = 0;

	status = UART_GET_INT_STATUS(info->port);
	do {

		if (status & UART_ISR_RI_MSK)
			uart00_rx_chars(info, regs);
		if (status & (UART_ISR_TI_MSK | UART_ISR_TII_MSK))
			uart00_tx_chars(info);
		if (status & UART_ISR_MI_MSK)
			uart00_modem_status(info);
		if (pass_counter++ > UART00_ISR_PASS_LIMIT)
			break;

		status = UART_GET_INT_STATUS(info->port);
	} while (status);
}

static u_int uart00_tx_empty(struct uart_port *port)
{
	return UART_GET_TSR(port) & UART_TSR_TX_LEVEL_MSK? 0 : TIOCSER_TEMT;
}

static u_int uart00_get_mctrl(struct uart_port *port)
{
	unsigned int result = 0;
	unsigned int status;

	status = UART_GET_MSR(port);
	if (status & UART_MSR_DCD_MSK)
		result |= TIOCM_CAR;
	if (status & UART_MSR_DSR_MSK)
		result |= TIOCM_DSR;
	if (status & UART_MSR_CTS_MSK)
		result |= TIOCM_CTS;
	if (status & UART_MSR_RI_MSK)
		result |= TIOCM_RI;

	return result;
}

static void uart00_set_mctrl_null(struct uart_port *port, u_int mctrl)
{
}

static void uart00_break_ctl(struct uart_port *port, int break_state)
{
	unsigned int mcr;

	mcr = UART_GET_MCR(port);
	if (break_state == -1)
		mcr |= UART_MCR_BR_MSK;
	else
		mcr &= ~UART_MCR_BR_MSK;
	UART_PUT_MCR(port, mcr);
}

static inline u_int uart_calculate_quot(struct uart_info *info, u_int baud)
{
	u_int quot;

	/* Special case: B0 rate */
	if (!baud)
		baud = 9600;

	quot = (info->port->uartclk / (16 * baud)-1)  ;

	return quot;
}
static void uart00_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	u_int uart_mc=0, old_ies;
	unsigned long flags;

#if DEBUG
	printk("uart00_set_cflag(0x%x) called\n", cflag);
#endif
	/* byte size and parity */
	switch (cflag & CSIZE) {
	case CS5: uart_mc = UART_MC_CLS_CHARLEN_5; break;
	case CS6: uart_mc = UART_MC_CLS_CHARLEN_6; break;
	case CS7: uart_mc = UART_MC_CLS_CHARLEN_7; break;
	default:  uart_mc = UART_MC_CLS_CHARLEN_8; break; // CS8
	}
	if (cflag & CSTOPB)
		uart_mc|= UART_MC_ST_TWO;
	if (cflag & PARENB) {
		uart_mc |= UART_MC_PE_MSK;
		if (!(cflag & PARODD))
			uart_mc |= UART_MC_EP_MSK;
	}

	port->read_status_mask = UART_RDS_OE_MSK;
	if (iflag & INPCK)
		port->read_status_mask |= UART_RDS_FE_MSK | UART_RDS_PE_MSK;
	if (iflag & (BRKINT | PARMRK))
		port->read_status_mask |= UART_RDS_BI_MSK;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (iflag & IGNPAR)
		port->ignore_status_mask |= UART_RDS_FE_MSK | UART_RDS_PE_MSK;
	if (iflag & IGNBRK) {
		port->ignore_status_mask |= UART_RDS_BI_MSK;
		/*
		 * If we're ignoring parity and break indicators,
		 * ignore overruns to (for real raw support).
		 */
		if (iflag & IGNPAR)
			port->ignore_status_mask |= UART_RDS_OE_MSK;
	}

	/* first, disable everything */
	save_flags(flags); cli();
	old_ies = UART_GET_IES(port); 

	if ((port->flags & ASYNC_HARDPPS_CD) ||
	    (cflag & CRTSCTS) || !(cflag & CLOCAL))
		old_ies |= UART_IES_ME_MSK;


	/* Set baud rate */
	quot+=1; /* Correction for generic quotient calculation */
	UART_PUT_DIV_LO(port, (quot & 0xff));
	UART_PUT_DIV_HI(port, ((quot & 0xf00) >> 8));
   

	UART_PUT_MC(port, uart_mc);
	UART_PUT_IES(port, old_ies);

	restore_flags(flags);
}

static int uart00_startup(struct uart_port *port, struct uart_info *info)
{
	int retval;
	int quot;

	/*
	 * Allocate the IRQ
	 */
	retval = request_irq(port->irq, uart00_int, 0, "uart00", info);
	if (retval)
		return retval;

	port->ops->set_mctrl(port, info->mctrl);
	
	quot=uart_calculate_quot(info,38400);
	uart00_change_speed(port,CS8,IGNPAR,quot);

	/* 
	 * Use iobase to store a pointer to info. We need this to start a 
	 * transmission as the tranmittr interrupt is only generated on
	 * the transition to the idle state 
	 */

	port->iobase=(u_int)info;
	
	/*
	 * Finally, enable interrupts. Use the TII interrupt to minimise 
	 * the number of interrupts generated. If higher performance is 
	 * needed, consider using the TI interrupt with a suitable FIFO
	 * threshold
	 */
	UART_PUT_IES(port, UART_IES_RE_MSK | UART_IES_TIE_MSK);


	

	return 0;
}

static void uart00_shutdown(struct uart_port *port, struct uart_info *info)
{
	/*
	 * Free the interrupt
	 */
	free_irq(port->irq, info);

	/*
	 * disable all interrupts, disable the port
	 */
	UART_PUT_IEC(port, 0xff);

	/* disable break condition and fifos */
	UART_PUT_MCR(port, UART_GET_MCR(port) &~UART_MCR_BR_MSK);
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void uart00_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, UART_PORT_SIZE);
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int uart00_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, UART_PORT_SIZE, "serial_uart00")
			!= NULL ? 0 : -EBUSY;
}

/*
 * Configure/autoconfigure the port.
 */
static void uart00_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE) {
		port->type = PORT_UART00;
		uart00_request_port(port);
	}
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int uart00_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_UART00)
		ret = -EINVAL;
	if (ser->irq < 0 || ser->irq >= NR_IRQS)
		ret = -EINVAL;
	if (ser->baud_base < 9600)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops uart00_pops = {
	tx_empty:	uart00_tx_empty,
	set_mctrl:	uart00_set_mctrl_null,
	get_mctrl:	uart00_get_mctrl,
	stop_tx:	uart00_stop_tx,
	start_tx:	uart00_start_tx,
	stop_rx:	uart00_stop_rx,
	enable_ms:	uart00_enable_ms,
	break_ctl:	uart00_break_ctl,
	startup:	uart00_startup,
	shutdown:	uart00_shutdown,
	change_speed:	uart00_change_speed,
	release_port:	uart00_release_port,
	request_port:	uart00_request_port,
	config_port:	uart00_config_port,
	verify_port:	uart00_verify_port,
};

static struct uart_port uart00_ports[UART_NR] = {
	{
		membase:	(void*)IO_ADDRESS(EXC_UART00_BASE),
		mapbase:        EXC_UART00_BASE,
		irq:		IRQ_UART,
		uartclk:	EXC_AHB2_CLK_FREQUENCY,
		fifosize:	16,
		ops:		&uart00_pops,
		flags:          ASYNC_BOOT_AUTOCONF,
	},
	{
		membase:	(void*)IO_ADDRESS(EXC_PLD_BLOCK0_BASE + 0x280),
		mapbase:        EXC_PLD_BLOCK0_BASE,
		irq:		0,
		uartclk:	32768000,
		fifosize:	16,
		ops:		&uart00_pops,
	},
};

#ifdef CONFIG_SERIAL_UART00_CONSOLE
#ifdef used_and_not_const_char_pointer
static int uart00_console_read(struct uart_port *port, char *s, u_int count)
{
	unsigned int status;
	int c;
#if DEBUG
	printk("uart00_console_read() called\n");
#endif

	c = 0;
	while (c < count) {
		status = UART_GET_RSR(port);
 		if (UART_RX_DATA(status)) {
			*s++ = UART_GET_CHAR(port);
			c++;
		} else {
			// nothing more to get, return
			return c;
		}
	}
	// return the count
	return c;
}
#endif
static void uart00_console_write(struct console *co, const char *s, unsigned count)
{
	struct uart_port *port = uart00_ports + co->index;
	unsigned int status, old_ies;
	int i;

	/*
	 *	First save the CR then disable the interrupts
	 */
	old_ies = UART_GET_IES(port);
	UART_PUT_IEC(port,0xff);

	/*
	 *	Now, do each character
	 */
	for (i = 0; i < count; i++) {
		do {
			status = UART_GET_TSR(port);
		} while (!UART_TX_READY(status));
		UART_PUT_CHAR(port, s[i]);
		if (s[i] == '\n') {
			do {
				status = UART_GET_TSR(port);
			} while (!UART_TX_READY(status));
			UART_PUT_CHAR(port, '\r');
		}
	}

	/*
	 *	Finally, wait for transmitter to become empty
	 *	and restore the IES
	 */
	do {
		status = UART_GET_TSR(port);
	} while (status & UART_TSR_TX_LEVEL_MSK);
	UART_PUT_IES(port, old_ies);
}

static kdev_t uart00_console_device(struct console *co)
{
	return MKDEV(SERIAL_UART00_MAJOR, SERIAL_UART00_MINOR + co->index);
}

static int uart00_console_wait_key(struct console *co)
{
	struct uart_port *port = uart00_ports + co->index;
	unsigned int status;

	do {
		status = UART_GET_RSR(port);
	} while (!UART_RX_DATA(status));
	return UART_GET_CHAR(port);
}

static void /*__init*/ uart00_console_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
	u_int uart_mc, quot;
	uart_mc= UART_GET_MC(port);

	*parity = 'n';
	if (uart_mc & UART_MC_PE_MSK) {
		if (uart_mc & UART_MC_EP_MSK)
			*parity = 'e';
		else
			*parity = 'o';
	}

	switch (uart_mc & UART_MC_CLS_MSK){

	case UART_MC_CLS_CHARLEN_5:
		*bits = 5;
		break;
	case UART_MC_CLS_CHARLEN_6:
		*bits = 6;
		break;
	case UART_MC_CLS_CHARLEN_7:
		*bits = 7;
		break;
	case UART_MC_CLS_CHARLEN_8:
		*bits = 8;
		break;
	}
	quot = UART_GET_DIV_LO(port) | (UART_GET_DIV_HI(port) << 8);
	*baud = port->uartclk / (16 *quot );
}

static int __init uart00_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 38400;
	int bits = 8;
	int parity = 'n';
	int flow= 'n';

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	port = uart_get_console(uart00_ports, UART_NR, co);

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		uart00_console_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

static struct console uart00_console = {
	name:           SERIAL_UART00_NAME,
	write:		uart00_console_write,
#ifdef used_and_not_const_char_pointer
	read:		uart00_console_read,
#endif
	device:		uart00_console_device,
	wait_key:	uart00_console_wait_key,
	setup:		uart00_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		0,
};

void __init uart00_console_init(void)
{
	register_console(&uart00_console);
}

#define UART00_CONSOLE	&uart00_console
#else
#define UART00_CONSOLE	NULL
#endif

static struct uart_driver uart00_reg = {
	owner:                  NULL,
	normal_major:		SERIAL_UART00_MAJOR,
	normal_name:		SERIAL_UART00_NAME,
	normal_driver:		&normal,
	callout_major:		CALLOUT_UART00_MAJOR,
	callout_name:		CALLOUT_UART00_NAME,
	callout_driver:		&callout,
	table:			uart00_table,
	termios:		uart00_termios,
	termios_locked:		uart00_termios_locked,
	minor:			SERIAL_UART00_MINOR,
	nr:			UART_NR,
	state:			NULL,
	port:			uart00_ports,
	cons:			UART00_CONSOLE,
};

static int __init uart00_init(void)
{
	printk(KERN_WARNING "serial_uart00:Using temporary major/minor pairs - these WILL change in the future\n");
	return uart_register_driver(&uart00_reg);
	
}


__initcall(uart00_init);
