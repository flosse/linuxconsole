/*
 *  linux/include/linux/console.h
 *
 *  Copyright (C) 1993        Hamish Macdonald
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Changed:
 * 10-Mar-94: Arno Griffioen: Conversion for vt100 emulator port from PC LINUX
 */

#ifndef _LINUX_CONSOLE_H_
#define _LINUX_CONSOLE_H_ 1

#include <linux/types.h>
#include <linux/spinlock.h>

/*
 *	The interface for a console, or any other device that
 *	wants to capture console messages (printer driver?)
 */

#define CON_PRINTBUFFER	(1)
#define CON_CONSDEV	(2) /* Last on the command line */
#define CON_ENABLED	(4)

struct console
{
	char	name[8];
	void	(*write)(struct console *, const char *, unsigned);
	int	(*read)(struct console *, char *, unsigned);
	struct tty_driver *(*device)(struct console *, int *);
	void	(*unblank)(void);
	int	(*setup)(struct console *, char *);
	short	flags;
	short	index;
	int	cflag;
	void	*data;
	struct	 console *next;
};

extern int add_preferred_console(char *name, int idx, char *options);
extern void register_console(struct console *);
extern int unregister_console(struct console *);
extern struct console *console_drivers;
extern void acquire_console_sem(void);
extern void release_console_sem(void);
extern void console_conditional_schedule(void);
extern void console_unblank(void);

/* VESA Blanking Levels */
#define VESA_NO_BLANKING	0
#define VESA_VSYNC_SUSPEND	1
#define VESA_HSYNC_SUSPEND	2
#define VESA_POWERDOWN		3

#endif /* _LINUX_CONSOLE_H */
