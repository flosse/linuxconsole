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
#include <linux/kdev_t.h>
#include <linux/spinlock.h>

/*
 *	Array of consoles built from command line options (console=)
 */
struct console_cmdline
{
	char	name[8];			/* Name of the driver	    */
	int	index;				/* Minor dev. to use	    */
	char	*options;			/* Options for the driver   */
};
#define MAX_CMDLINECONSOLES 8
extern struct console_cmdline console_list[MAX_CMDLINECONSOLES];

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
	int	(*read)(struct console *, const char *, unsigned);
	kdev_t	(*device)(struct console *);
	int	(*wait_key)(struct console *);
	int	(*setup)(struct console *, char *);
	short	flags;
	short	index;
	int	cflag;
	struct	 console *next;
};

extern void acquire_console_sem(kdev_t device);
extern void release_console_sem(kdev_t device);
extern void register_console(struct console *);
extern int unregister_console(struct console *);
extern struct console *console_drivers;

/* VESA Blanking Levels */
#define VESA_NO_BLANKING        0
#define VESA_VSYNC_SUSPEND      1
#define VESA_HSYNC_SUSPEND      2
#define VESA_POWERDOWN          3

#endif /* _LINUX_CONSOLE_H */
