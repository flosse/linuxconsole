/* $Id$
 * sunserial.h: SUN serial driver infrastructure (including keyboards).
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef _SPARC_SUNSERIAL_H
#define _SPARC_SUNSERIAL_H 1

#include <linux/config.h>
#include <linux/tty.h>
#include <linux/kd.h>
#include <linux/kbd_kern.h>
#include <linux/console.h>

struct initfunc {
	int		(*init) (void);
	struct initfunc *next;
};

struct sunserial_operations {
	struct initfunc	*rs_init;
	void		(*rs_kgdb_hook) (int);
	void		(*rs_change_mouse_baud) (int);
	int		(*rs_read_proc) (char *, char **, off_t, int, int *, void *);
};

extern struct sunserial_operations rs_ops;

extern void sunserial_setinitfunc(int (*) (void));

extern int serial_console;
extern int stop_a_enabled;
extern void sunserial_console_termios(struct console *);

#endif /* !(_SPARC_SUNSERIAL_H) */
