/* $Id$
 * serial.c: Serial port driver infrastructure for the Sparc.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/oplib.h>

#include "sunserial.h"

int serial_console;
int stop_a_enabled = 1;

int __init con_is_present(void)
{
	return serial_console ? 0 : 1;
}

static void __init nop_rs_kgdb_hook(int channel)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static void nop_rs_change_mouse_baud(int baud)
{
	printk("Oops: %s called\n", __FUNCTION__);
}

static int nop_rs_read_proc(char *page, char **start, off_t off, int count,
			    int *eof, void *data)
{
	printk("Oops: %s called\n", __FUNCTION__);
	return 0;
}

struct sunserial_operations rs_ops = {
	0,
	nop_rs_kgdb_hook,
	nop_rs_change_mouse_baud,
	nop_rs_read_proc
};

void rs_init(void)
{
	static int invoked = 0;

	if (!invoked) {
		struct initfunc *init;

		invoked = 1;

		init = rs_ops.rs_init;
		while (init) {
			(void) init->init();
			init = init->next;
		}
	}
}

void __init rs_kgdb_hook(int channel)
{
	rs_ops.rs_kgdb_hook(channel);
}

void __init serial_console_init(void)
{
	return;
}

void rs_change_mouse_baud(int baud)
{
	rs_ops.rs_change_mouse_baud(baud);
}

int rs_read_proc(char *page, char **start, off_t off, int count,
		 int *eof, void *data)
{
	return rs_ops.rs_read_proc(page, start, off, count, eof, data);
}

int register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
}

void * __init sunserial_alloc_bootmem(unsigned long size)
{
	void *ret;

	ret = __alloc_bootmem(size, SMP_CACHE_BYTES, 0UL);
	if (ret != NULL)
		memset(ret, 0, size);

	return ret;
}

void
sunserial_setinitfunc(int (*init) (void))
{
	struct initfunc *rs_init;

	rs_init = sunserial_alloc_bootmem(sizeof(struct initfunc));
	if (rs_init == NULL) {
		prom_printf("sunserial_setinitfunc: Cannot alloc initfunc.\n");
		prom_halt();
	}

	rs_init->init = init;
	rs_init->next = rs_ops.rs_init;
	rs_ops.rs_init = rs_init;
}

void
sunserial_console_termios(struct console *con)
{
	char mode[16], buf[16], *s;
	char *mode_prop = "ttyX-mode";
	char *cd_prop = "ttyX-ignore-cd";
	char *dtr_prop = "ttyX-rts-dtr-off";
	int baud, bits, stop, cflag;
	char parity;
	int carrier = 0;
	int rtsdtr = 1;
	int topnd, nd;

	if (!serial_console)
		return;

	if (serial_console == 1) {
		mode_prop[3] = 'a';
		cd_prop[3] = 'a';
		dtr_prop[3] = 'a';
	} else {
		mode_prop[3] = 'b';
		cd_prop[3] = 'b';
		dtr_prop[3] = 'b';
	}

	topnd = prom_getchild(prom_root_node);
	nd = prom_searchsiblings(topnd, "options");
	if (!nd) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	if (!prom_node_has_property(nd, mode_prop)) {
		strcpy(mode, "9600,8,n,1,-");
		goto no_options;
	}

	memset(mode, 0, sizeof(mode));
	prom_getstring(nd, mode_prop, mode, sizeof(mode));

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			carrier = 1;

		/* XXX: this is unused below. */
	}

	if (prom_node_has_property(nd, cd_prop)) {
		memset(buf, 0, sizeof(buf));
		prom_getstring(nd, cd_prop, buf, sizeof(buf));
		if (!strcmp(buf, "false"))
			rtsdtr = 0;

		/* XXX: this is unused below. */
	}

no_options:
	cflag = CREAD | HUPCL | CLOCAL;

	s = mode;
	baud = simple_strtoul(s, 0, 0);
	s = strchr(s, ',');
	bits = simple_strtoul(++s, 0, 0);
	s = strchr(s, ',');
	parity = *(++s);
	s = strchr(s, ',');
	stop = simple_strtoul(++s, 0, 0);
	s = strchr(s, ',');
	/* XXX handshake is not handled here. */

	switch (baud) {
		case 150: cflag |= B150; break;
		case 300: cflag |= B300; break;
		case 600: cflag |= B600; break;
		case 1200: cflag |= B1200; break;
		case 2400: cflag |= B2400; break;
		case 4800: cflag |= B4800; break;
		case 9600: cflag |= B9600; break;
		case 19200: cflag |= B19200; break;
		case 38400: cflag |= B38400; break;
		default: baud = 9600; cflag |= B9600; break;
	}

	switch (bits) {
		case 5: cflag |= CS5; break;
		case 6: cflag |= CS6; break;
		case 7: cflag |= CS7; break;
		case 8: cflag |= CS8; break;
		default: cflag |= CS8; break;
	}

	switch (parity) {
		case 'o': cflag |= (PARENB | PARODD); break;
		case 'e': cflag |= PARENB; break;
		case 'n': default: break;
	}

	switch (stop) {
		case 2: cflag |= CSTOPB; break;
		case 1: default: break;
	}

	con->cflag = cflag;
}

extern int su_probe(void);
extern int zs_probe(void);
#ifdef CONFIG_SAB82532
extern int sab82532_probe(void);
#endif

void __init sun_serial_setup(void)
{
	int ret = 1;
	
#if defined(CONFIG_PCI) && !defined(__sparc_v9__)
	/*
	 * Probing sequence on sparc differs from sparc64.
	 * Keyboard is probed ahead of su because we want su function
	 * when keyboard is active. su is probed ahead of zs in order to
	 * get console on MrCoffee with fine but disconnected zs.
	 */
	if (su_probe() == 0)
		return;
#endif

	if (zs_probe() == 0)
		return;
		
#ifdef CONFIG_SAB82532
	ret = sab82532_probe();
#endif

#if defined(CONFIG_PCI) && defined(__sparc_v9__)
	/*
	 * Keyboard serial devices.
	 *
	 * Well done, Sun, prom_devopen("/pci@1f,4000/ebus@1/su@14,3083f8")
	 * hangs the machine if no keyboard is connected to the device...
	 * All PCI PROMs seem to do this, I have seen this on the Ultra 450
	 * with version 3.5 PROM, and on the Ultra/AX with 3.1.5 PROM.
	 *
	 * So be very careful not to probe for keyboards if we are on a
	 * serial console.
	 */
	if (su_probe() == 0)
		return;
#endif

	if (!ret)
		return;
		
#ifdef __sparc_v9__
	{	extern int this_is_starfire;
		/* Hello, Starfire. Pleased to meet you :) */
		if(this_is_starfire != 0)
			return;
	}
#endif

	prom_printf("No serial devices found, bailing out.\n");
	prom_halt();
}
