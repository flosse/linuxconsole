/*
 *  linux/drivers/char/serial_8250.h
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

struct serial8250_probe {
	struct module	*owner;
	int		(*pci_init_one)(struct pci_dev *dev);
	void		(*pci_remove_one)(struct pci_dev *dev);
	void		(*pnp_init)(void);
};

int serial8250_register_probe(struct serial8250_probe *probe);
void serial8250_unregister_probe(struct serial8250_probe *probe);

struct old_serial_port {
	unsigned int uart;
	unsigned int base_baud;
	unsigned int port;
	unsigned int irq;
	unsigned int flags;
};

#undef SERIAL_PARANOIA_CHECK
#define CONFIG_SERIAL_NOPAUSE_IO
#define SERIAL_DO_RESTART

#ifdef CONFIG_PCI
#ifndef CONFIG_SERIAL_SHARE_IRQ
#define CONFIG_SERIAL_SHARE_IRQ
#endif
#ifndef CONFIG_SERIAL_MANY_PORTS
#define CONFIG_SERIAL_MANY_PORTS
#endif
#endif

#if defined(CONFIG_ISAPNP)|| (defined(CONFIG_ISAPNP_MODULE) && defined(MODULE))
#ifndef ENABLE_SERIAL_PNP
#define ENABLE_SERIAL_PNP
#endif
#endif

/* Set of debugging defines */

#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_PCI
#undef SERIAL_DEBUG_AUTOCONF

/* Sanity checks */

#ifdef CONFIG_SERIAL_MULTIPORT
#ifndef CONFIG_SERIAL_SHARE_IRQ
#define CONFIG_SERIAL_SHARE_IRQ
#endif
#endif

#ifdef CONFIG_HUB6
#ifndef CONFIG_SERIAL_MANY_PORTS
#define CONFIG_SERIAL_MANY_PORTS
#endif
#ifndef CONFIG_SERIAL_SHARE_IRQ
#define CONFIG_SERIAL_SHARE_IRQ
#endif
#endif

#ifdef MODULE
#undef CONFIG_SERIAL_CONSOLE
#endif

#define CONFIG_SERIAL_RSA

#define RS_ISR_PASS_LIMIT 256

#if defined(__i386__) && (defined(CONFIG_M386) || defined(CONFIG_M486))
#define SERIAL_INLINE
#endif
  
#ifdef SERIAL_INLINE
#define _INLINE_ inline
#else
#define _INLINE_
#endif

#define PROBE_RSA	(1 << 0)
#define PROBE_ANY	(~0)

#define HIGH_BITS_OFFSET ((sizeof(long)-sizeof(int))*8)


