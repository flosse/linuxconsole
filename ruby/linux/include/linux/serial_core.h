/*
 *  linux/drivers/char/serial_core.h
 *
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
 */

/*
 * The type definitions.  These are from Ted Ts'o's serial.h
 */
#define PORT_UNKNOWN	0
#define PORT_8250	1
#define PORT_16450	2
#define PORT_16550	3
#define PORT_16550A	4
#define PORT_CIRRUS	5
#define PORT_16650	6
#define PORT_16650V2	7
#define PORT_16750	8
#define PORT_STARTECH	9
#define PORT_16C950	10
#define PORT_16654	11
#define PORT_16850	12
#define PORT_RSA	13
#define PORT_MAX_8250	13	/* max port ID */

/*
 * ARM specific type numbers.  These are not currently guaranteed
 * to be implemented, and will change in the future.
 */
#define PORT_AMBA	32
#define PORT_CLPS711X	33
#define PORT_SA1100	34
#define PORT_UART00    35

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/interrupt.h>
#include <linux/circ_buf.h>
#include <linux/serial.h>
#include <linux/spinlock.h>

struct uart_port;
struct uart_info;

/*
 * This structure describes all the operations that can be
 * done on the physical hardware.
 */
struct uart_ops {
	u_int	(*tx_empty)(struct uart_port *);
	void	(*set_mctrl)(struct uart_port *, u_int mctrl);
	int	(*get_mctrl)(struct uart_port *);
	void	(*stop_tx)(struct uart_port *, u_int from_tty);
	void	(*start_tx)(struct uart_port *, u_int nonempty, u_int from_tty);
	void	(*stop_rx)(struct uart_port *);
	void	(*enable_ms)(struct uart_port *);
	void	(*break_ctl)(struct uart_port *, int ctl);
	int	(*startup)(struct uart_port *, struct uart_info *);
	void	(*shutdown)(struct uart_port *, struct uart_info *);
	void	(*change_speed)(struct uart_port *, u_int cflag, u_int iflag, u_int quot);
	void	(*pm)(struct uart_port *, u_int state, u_int oldstate);

	/*
	 * Release IO and memory resources used by the port.
	 * This includes iounmap if necessary.
	 */
	void	(*release_port)(struct uart_port *);

	/*
	 * Request IO and memory resources used by the port.
	 * This includes iomapping the port if necessary.
	 */
	int	(*request_port)(struct uart_port *);
	void	(*config_port)(struct uart_port *, int);
	int	(*verify_port)(struct uart_port *, struct serial_struct *);
	int	(*ioctl)(struct uart_port *, u_int, u_long);
};

#define UART_CONFIG_TYPE	(1 << 0)
#define UART_CONFIG_IRQ		(1 << 1)

struct uart_icount {
	__u32	cts;
	__u32	dsr;
	__u32	rng;
	__u32	dcd;
	__u32	rx;
	__u32	tx;
	__u32	frame;
	__u32	overrun;
	__u32	parity;
	__u32	brk;
	__u32	buf_overrun;
};

struct uart_port {
	u_int			iobase;			/* in/out[bwl] */
	void			*membase;		/* read/write[bwl] */
	u_int			irq;
	u_int			uartclk;
	u_char			fifosize;		/* tx fifo size */
	u_char			x_char;
	u_char			regshift;		/* reg offset shift */
	u_char			iotype;			/* io access style */
	u_char			hub6;
	u_char			unused[7];
	u_int			read_status_mask;
	u_int			ignore_status_mask;
	u_int			flags;
	u_int			type;			/* port type */
	struct uart_ops		*ops;
	struct uart_icount	icount;
	u_int			line;
	u_long			mapbase;		/* for ioremap */
};

/*
 * This is the state information which is persistent across opens.
 * The low level driver must not to touch any elements contained
 * within.
 */
struct uart_state {
	u_int			close_delay;
	u_int			closing_wait;
	u_int			custom_divisor;
	struct termios		normal_termios;
	struct termios		callout_termios;

	int			count;
	struct uart_info	*info;
	struct uart_port	*port;

	struct semaphore	count_sem;	/* this protects 'count' */
#ifdef CONFIG_PM
	struct pm_dev		*pm;
	struct console		*cons;		/* need this to handle cons */
#endif
};

#define UART_XMIT_SIZE 1024
/*
 * This is the state information which is only valid when the port
 * is open; it may be freed by the core driver once the device has
 * been closed.  Either the low level driver or the core can modify
 * stuff here.
 */
struct uart_info {
	spinlock_t		lock;
	struct uart_port	*port;
	struct uart_ops		*ops;
	struct uart_state	*state;
	struct tty_struct	*tty;
	struct circ_buf		xmit;
	u_int			flags;

	u_int			event;
	u_int			timeout;
	u_int			mctrl;
	u_int			driver_priv;
	int			blocked_open;
	pid_t			session;
	pid_t			pgrp;

	struct tasklet_struct	tlet;

	wait_queue_head_t	open_wait;
	wait_queue_head_t	close_wait;
	wait_queue_head_t	delta_msr_wait;

	/*
	 * List if uarts on the same IRQ line.
	 */
	struct uart_info	*next_info;
/*
 * This is placed at the end since it may not be present.
 * Do not place any new members after here.
 */
#if defined(CONFIG_SERIAL_CORE_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
	u_long			sysrq;		/* available for driver use */
#endif
};

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS		256

#define EVT_WRITE_WAKEUP	0

struct module;

struct uart_driver {
	struct module		*owner;
	int			 normal_major;
	const char		*normal_name;
	struct tty_driver	*normal_driver;
	int			 callout_major;
	const char		*callout_name;
	struct tty_driver	*callout_driver;
	struct tty_struct	**table;
	struct termios		**termios;
	struct termios		**termios_locked;
	int			 minor;
	int			 nr;
	struct uart_state	*state;			/* driver should pass NULL */
	struct uart_port	*port;			/* array of port information */
	struct console		*cons;
};

void uart_event(struct uart_info *info, int event);
struct uart_port *uart_get_console(struct uart_port *ports, int nr,
				   struct console *c);
void uart_parse_options(char *options, int *baud, int *parity, int *bits,
			int *flow);
int uart_set_options(struct uart_port *port, struct console *co, int baud,
		     int parity, int bits, int flow);
int uart_register_driver(struct uart_driver *uart);
void uart_unregister_driver(struct uart_driver *uart);
void uart_unregister_port(struct uart_driver *reg, int line);
int uart_register_port(struct uart_driver *reg, struct uart_port *port);

/*
 * The following are helper functions for the low level drivers.
 */
#ifdef SUPPORT_SYSRQ
static inline int
__uart_handle_sysrq_char(struct uart_info *info, unsigned int ch,
			 struct pt_regs *regs)
{
	if (info->sysrq) {
		if (ch && time_before(jiffies, info->sysrq)) {
			handle_sysrq(ch, regs, NULL, NULL);
			info->sysrq = 0;
			return 1;
		}
		info->sysrq = 0;
	}
	return 0;
}
#endif

/*
 * We do the SysRQ and SAK checking like this...
 */
static inline int __uart_handle_break(struct uart_info *info, struct console *con)
{
#ifdef SUPPORT_SYSRQ
	if (info->port->line == con->index) {
		if (!info->sysrq) {
			info->sysrq = jiffies + HZ*5;
			return 1;
		}
		info->sysrq = 0;
	}
#endif
	if (info->flags & ASYNC_SAK)
		do_SAK(info->tty);
	return 0;
}

#ifdef SUPPORT_SYSRQ
#define uart_handle_break(info,con)		__uart_handle_break(info,con)
#define uart_handle_sysrq_char(info,ch,regs)	__uart_handle_sysrq_char(info,ch,regs)
#else
#define uart_handle_break(info,con)	__uart_handle_break(info,NULL)
#define uart_handle_sysrq_char(info,ch,regs)	(0)
#endif

/**
 *	uart_handle_dcd_change - handle a change of carrier detect state
 *	@info: uart_info structure for the open port
 *	@status: new carrier detect status, nonzero if active
 */
static inline void
uart_handle_dcd_change(struct uart_info *info, unsigned int status)
{
	struct uart_port *port = info->port;

	port->icount.dcd++;

#ifdef CONFIG_HARD_PPS
	if ((info->flags & ASYNC_HARDPPS_CD) && status)
		hardpps();
#endif

	if (info->flags & ASYNC_CHECK_CD) {
		if (status)
			wake_up_interruptible(&info->open_wait);
		else if (!((info->flags & ASYNC_CALLOUT_ACTIVE) &&
			   (info->flags & ASYNC_CALLOUT_NOHUP))) {
			if (info->tty)
				tty_hangup(info->tty);
		}
	}
}

/**
 *	uart_handle_cts_change - handle a change of clear-to-send state
 *	@info: uart_info structure for the open port
 *	@status: new clear to send status, nonzero if active
 */
static inline void
uart_handle_cts_change(struct uart_info *info, unsigned int status)
{
	struct uart_port *port = info->port;
	unsigned long flags;

	port->icount.cts++;

	if (info->flags & ASYNC_CTS_FLOW) {
		spin_lock_irqsave(&info->lock, flags);
		if (info->tty->hw_stopped) {
			if (status) {
				info->tty->hw_stopped = 0;
				info->ops->start_tx(port, 1, 0);
				uart_event(info, EVT_WRITE_WAKEUP);
			}
		} else {
			if (!status) {
				info->tty->hw_stopped = 1;
				info->ops->stop_tx(port, 0);
			}
		}
		spin_unlock_irqrestore(&info->lock, flags);
	}
}

#endif
