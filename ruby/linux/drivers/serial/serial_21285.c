/*
 * linux/drivers/char/serial_21285.c
 *
 * Driver for the serial port on the 21285 StrongArm-110 core logic chip.
 *
 * Based on drivers/char/serial.c
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
#include <linux/serial.h>
#include <linux/major.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/serial_core.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/hardware/dec21285.h>
#include <asm/hardware.h>

#define BAUD_BASE		(mem_fclk_21285/64)

#define SERIAL_21285_NAME	"ttyFB"
#define SERIAL_21285_MAJOR	204
#define SERIAL_21285_MINOR	4

#define SERIAL_21285_AUXNAME	"cuafb"
#define SERIAL_21285_AUXMAJOR	205
#define SERIAL_21285_AUXMINOR	4

#define RXSTAT_DUMMY_READ	0x80000000
#define RXSTAT_FRAME		(1 << 0)
#define RXSTAT_PARITY		(1 << 1)
#define RXSTAT_OVERRUN		(1 << 2)
#define RXSTAT_ANYERR		(RXSTAT_FRAME|RXSTAT_PARITY|RXSTAT_OVERRUN)

#define H_UBRLCR_BREAK		(1 << 0)
#define H_UBRLCR_PARENB		(1 << 1)
#define H_UBRLCR_PAREVN		(1 << 2)
#define H_UBRLCR_STOPB		(1 << 3)
#define H_UBRLCR_FIFO		(1 << 4)

static struct tty_driver normal, callout;
static struct tty_struct *serial21285_table[1];
static struct termios *serial21285_termios[1];
static struct termios *serial21285_termios_locked[1];
static const char serial21285_name[] = "Footbridge UART";

/*
 * The documented expression for selecting the divisor is:
 *  BAUD_BASE / baud - 1
 * However, typically BAUD_BASE is not divisible by baud, so
 * we want to select the divisor that gives us the minimum
 * error.  Therefore, we want:
 *  int(BAUD_BASE / baud - 0.5) ->
 *  int(BAUD_BASE / baud - (baud >> 1) / baud) ->
 *  int((BAUD_BASE - (baud >> 1)) / baud)
 */

static void serial21285_stop_tx(struct uart_port *port, u_int from_tty)
{
	disable_irq(IRQ_CONTX);
}

static void serial21285_start_tx(struct uart_port *port, u_int nonempty, u_int from_tty)
{
	if (nonempty)
		enable_irq(IRQ_CONTX);
}

static void serial21285_stop_rx(struct uart_port *port)
{
	disable_irq(IRQ_CONRX);
}

static void serial21285_enable_ms(struct uart_port *port)
{
}

static void serial21285_rx_chars(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	struct uart_port *port = info->port;
	struct tty_struct *tty = info->tty;
	unsigned int status, ch, rxs, max_count = 256;

	status = *CSR_UARTFLG;
	while (status & 0x10 && max_count--) {
		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			tty->flip.tqueue.routine((void *)tty);
			if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
				printk(KERN_WARNING "TTY_DONT_FLIP set\n");
				return;
			}
		}

		ch = *CSR_UARTDR;

		*tty->flip.char_buf_ptr = ch;
		*tty->flip.flag_buf_ptr = TTY_NORMAL;
		port->icount.rx++;

		rxs = *CSR_RXSTAT | RXSTAT_DUMMY_READ;
		if (rxs & RXSTAT_ANYERR) {
			if (rxs & RXSTAT_PARITY)
				port->icount.parity++;
			else if (rxs & RXSTAT_FRAME)
				port->icount.frame++;
			if (rxs & RXSTAT_OVERRUN)
				port->icount.overrun++;

			rxs &= port->read_status_mask;

			if (rxs & RXSTAT_PARITY)
				*tty->flip.flag_buf_ptr = TTY_PARITY;
			else if (rxs & RXSTAT_FRAME)
				*tty->flip.flag_buf_ptr = TTY_FRAME;
		}

		if ((rxs & port->ignore_status_mask) == 0) {
			tty->flip.flag_buf_ptr++;
			tty->flip.char_buf_ptr++;
			tty->flip.count++;
		}
		if ((rxs & RXSTAT_OVERRUN) &&
		    tty->flip.count < TTY_FLIPBUF_SIZE) {
			/*
			 * Overrun is special, since it's reported
			 * immediately, and doesn't affect the current
			 * character.
			 */
			*tty->flip.char_buf_ptr++ = 0;
			*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
			tty->flip.count++;
		}
		status = *CSR_UARTFLG;
	}
	tty_flip_buffer_push(tty);
}

static void serial21285_tx_chars(int irq, void *dev_id, struct pt_regs *regs)
{
	struct uart_info *info = dev_id;
	struct uart_port *port = info->port;
	int count = 256;

	if (port->x_char) {
		*CSR_UARTDR = port->x_char;
		port->icount.tx++;
		port->x_char = 0;
		return;
	}
	if (port->xmit.head == port->xmit.tail
	    || info->tty->stopped
	    || info->tty->hw_stopped) {
		serial21285_stop_tx(port, 0);
		return;
	}

	do {
		*CSR_UARTDR = port->xmit.buf[port->xmit.tail];
		port->xmit.tail = (port->xmit.tail + 1) & (UART_XMIT_SIZE - 1);
		port->icount.tx++;
		if (port->xmit.head == port->xmit.tail)
			break;
	} while (--count > 0 && !(*CSR_UARTFLG & 0x20));

	if (CIRC_CNT(port->xmit.head, port->xmit.tail, UART_XMIT_SIZE) <
			WAKEUP_CHARS)
		uart_event(info, EVT_WRITE_WAKEUP);

	if (port->xmit.head == port->xmit.tail)
		serial21285_stop_tx(port, 0);
}

static u_int serial21285_tx_empty(struct uart_port *port)
{
	return (*CSR_UARTFLG & 8) ? 0 : TIOCSER_TEMT;
}

/* no modem control lines */
static u_int serial21285_get_mctrl(struct uart_port *port)
{
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

static void serial21285_set_mctrl(struct uart_port *port, u_int mctrl)
{
}

static void serial21285_break_ctl(struct uart_port *port, int break_state)
{
	u_int h_lcr;

	h_lcr = *CSR_H_UBRLCR;
	if (break_state)
		h_lcr |= H_UBRLCR_BREAK;
	else
		h_lcr &= ~H_UBRLCR_BREAK;
	*CSR_H_UBRLCR = h_lcr;
}

static int serial21285_startup(struct uart_port *port, struct uart_info *info)
{
	int ret;

	ret = request_irq(IRQ_CONRX, serial21285_rx_chars, 0,
			  serial21285_name, info);
	if (ret == 0) {
		ret = request_irq(IRQ_CONTX, serial21285_tx_chars, 0,
				  serial21285_name, info);
		if (ret)
			free_irq(IRQ_CONRX, info);
	}
	return ret;
}

static void serial21285_shutdown(struct uart_port *port, struct uart_info *info)
{
	free_irq(IRQ_CONTX, info);
	free_irq(IRQ_CONRX, info);
}

static void
serial21285_change_speed(struct uart_port *port, u_int cflag, u_int iflag, u_int quot)
{
	u_int h_lcr;

	switch (cflag & CSIZE) {
	case CS5:		h_lcr = 0x00;	break;
	case CS6:		h_lcr = 0x20;	break;
	case CS7:		h_lcr = 0x40;	break;
	default: /* CS8 */	h_lcr = 0x60;	break;
	}

	if (cflag & CSTOPB)
		h_lcr |= H_UBRLCR_STOPB;
	if (cflag & PARENB) {
		h_lcr |= H_UBRLCR_PARENB;
		if (!(cflag & PARODD))
			h_lcr |= H_UBRLCR_PAREVN;
	}

	if (port->fifosize)
		h_lcr |= H_UBRLCR_FIFO;

	port->read_status_mask = RXSTAT_OVERRUN;
	if (iflag & INPCK)
		port->read_status_mask |= RXSTAT_FRAME | RXSTAT_PARITY;

	/*
	 * Characters to ignore
	 */
	port->ignore_status_mask = 0;
	if (iflag & IGNPAR)
		port->ignore_status_mask |= RXSTAT_FRAME | RXSTAT_PARITY;
	if (iflag & IGNBRK && iflag & IGNPAR)
		port->ignore_status_mask |= RXSTAT_OVERRUN;

	/*
	 * Ignore all characters if CREAD is not set.
	 */
	if ((cflag & CREAD) == 0)
		port->ignore_status_mask |= RXSTAT_DUMMY_READ;

	*CSR_UARTCON = 0;
	*CSR_L_UBRLCR = quot & 0xff;
	*CSR_M_UBRLCR = (quot >> 8) & 0x0f;
	*CSR_H_UBRLCR = h_lcr;
	*CSR_UARTCON = 1;
}

static const char *serial21285_type(struct uart_port *port)
{
	return port->type == PORT_21285 ? "DC21285" : NULL;
}

static void serial21285_release_port(struct uart_port *port)
{
	release_mem_region(port->mapbase, 32);
}

static int serial21285_request_port(struct uart_port *port)
{
	return request_mem_region(port->mapbase, 32, serial21285_name)
			 != NULL ? 0 : -EBUSY;
}

static void serial21285_config_port(struct uart_port *port, int flags)
{
	if (flags & UART_CONFIG_TYPE && serial21285_request_port(port) == 0)
		port->type = PORT_21285;
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int serial21285_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	int ret = 0;
	if (ser->type != PORT_UNKNOWN && ser->type != PORT_21285)
		ret = -EINVAL;
	if (ser->irq != NO_IRQ)
		ret = -EINVAL;
	if (ser->baud_base != port->uartclk / 16)
		ret = -EINVAL;
	return ret;
}

static struct uart_ops serial21285_ops = {
	tx_empty:	serial21285_tx_empty,
	get_mctrl:	serial21285_get_mctrl,
	set_mctrl:	serial21285_set_mctrl,
	stop_tx:	serial21285_stop_tx,
	start_tx:	serial21285_start_tx,
	stop_rx:	serial21285_stop_rx,
	enable_ms:	serial21285_enable_ms,
	break_ctl:	serial21285_break_ctl,
	startup:	serial21285_startup,
	shutdown:	serial21285_shutdown,
	change_speed:	serial21285_change_speed,
	type:		serial21285_type,
	release_port:	serial21285_release_port,
	request_port:	serial21285_request_port,
	config_port:	serial21285_config_port,
	verify_port:	serial21285_verify_port,
};

static struct uart_port serial21285_port = {
	membase:	0,
	mapbase:	0x42000160,
	iotype:		SERIAL_IO_MEM,
	irq:		NO_IRQ,
	uartclk:	0,
	fifosize:	16,
	ops:		&serial21285_ops,
	flags:		ASYNC_BOOT_AUTOCONF,
};

static void serial21285_setup_ports(void)
{
	serial21285_port.uartclk = mem_fclk_21285 / 16;
}

#ifdef CONFIG_SERIAL_21285_CONSOLE
/************** console driver *****************/

static void serial21285_console_write(struct console *co, const char *s, u_int count)
{
	int i;

	disable_irq(IRQ_CONTX);
	for (i = 0; i < count; i++) {
		while (*CSR_UARTFLG & 0x20);
		*CSR_UARTDR = s[i];
		if (s[i] == '\n') {
			while (*CSR_UARTFLG & 0x20);
			*CSR_UARTDR = '\r';
		}
	}
	enable_irq(IRQ_CONTX);
}

static kdev_t serial21285_console_device(struct console *c)
{
	return MKDEV(SERIAL_21285_MAJOR, SERIAL_21285_MINOR);
}

static int serial21285_console_wait_key(struct console *co)
{
	int c;

	disable_irq(IRQ_CONRX);
	while (*CSR_UARTFLG & 0x10);
	c = *CSR_UARTDR;
	enable_irq(IRQ_CONRX);
	return c;
}

static void __init
serial21285_get_options(struct uart_port *port, int *baud, int *parity, int *bits)
{
}

static int __init serial21285_console_setup(struct console *co, char *options)
{
	struct uart_port *port = &serial21285_port;
	int baud = 9600;
	int bits = 8;
	int parity = 'n';
	int flow = 'n';

	if (machine_is_personal_server())
		baud = 57600;

	/*
	 * Check whether an invalid uart number has been specified, and
	 * if so, search for the first available port that does have
	 * console support.
	 */
	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);
	else
		serial21285_get_options(port, &baud, &parity, &bits);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

#ifdef CONFIG_SERIAL_21285_OLD
static struct console serial21285_old_cons =
{
	SERIAL_21285_OLD_NAME,
	serial21285_console_write,
	NULL,
	serial21285_console_device,
	serial21285_console_wait_key,
	NULL,
	serial21285_console_setup,
	CON_PRINTBUFFER,
	-1,
	0,
	NULL
};
#endif

static struct console serial21285_console =
{
	name:		SERIAL_21285_NAME,
	write:		serial21285_console_write,
	device:		serial21285_console_device,
	wait_key:	serial21285_console_wait_key,
	setup:		serial21285_console_setup,
	flags:		CON_PRINTBUFFER,
	index:		-1,
};

void __init rs285_console_init(void)
{
	serial21285_setup_ports();
	register_console(&serial21285_console);
}

#define SERIAL_21285_CONSOLE	&serial21285_console
#else
#define SERIAL_21285_CONSOLE	NULL
#endif

static struct uart_driver serial21285_reg = {
	owner:			THIS_MODULE,
	normal_major:		SERIAL_21285_MAJOR,
#ifdef CONFIG_DEVFS_FS
	normal_name:		"ttyFB%d",
	callout_name:		"cuafb%d",
#else
	normal_name:		"ttyFB",
	callout_name:		"cuafb",
#endif
	normal_driver:		&normal,
	callout_major:		SERIAL_21285_AUXMAJOR,
	callout_driver:		&callout,
	table:			serial21285_table,
	termios:		serial21285_termios,
	termios_locked:		serial21285_termios_locked,
	minor:			SERIAL_21285_MINOR,
	nr:			1,
	port:			&serial21285_port,
	cons:			SERIAL_21285_CONSOLE,
};

static int __init serial21285_init(void)
{
	serial21285_setup_ports();
	return uart_register_driver(&serial21285_reg);
}

static void __exit serial21285_exit(void)
{
	uart_unregister_driver(&serial21285_reg);
}

module_init(serial21285_init);
module_exit(serial21285_exit);

EXPORT_NO_SYMBOLS;

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Intel Footbridge (21285) serial driver");
