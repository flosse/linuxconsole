/*
 *  joy-genius.c  Version 1.2
 *
 *  Copyright (c) 1998-1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * Microsoft Genius digital joystick family.
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <asm/system.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>

/*
 * These are really magic values. Changing them can make a problem go away,
 * as well as break everything.
 */

#define JS_GE_DEBUG

#define JS_GE_START		400	/* The time we wait for the first bit [400 us] */
#define JS_GE_TIMEOUT		4	/* Wait for everything to settle [4 ms] */
#define JS_GE_LENGTH		80	/* Max number of triplets in a packet */

/*
 * Genius joystick ids ...
 */

#define JS_GE_ID_G09		1
#define JS_GE_ID_F30D		2
#define JS_GE_ID_F30		3
#define JS_GE_ID_F31D		4
#define JS_GE_ID_F305		5
#define JS_GE_ID_F23P		6
#define JS_GE_ID_F31		7
#define JS_GE_ID_MAX		7

static int js_ge_port_list[] __initdata = {0x201, 0};
static struct js_port* js_ge_port __initdata = NULL;

static struct {
	int x;
	int y;
} js_ge_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static char js_ge_length[] = { 40, 40, 40, 40, 40, 40, 40, 40 };

int seq_reset[] = { 240, 340, 0 };
int seq_digital[] = { 590, 320, 860, 0 };

struct js_ge_info {
	int io;
	unsigned char id;
};

/*
 * js_ge_read_packet() reads an Assgesin 3D packet.
 */

static int js_ge_read_packet(int io, int length, char *data)
{
	unsigned char u, v;
	int i;
	unsigned int t, p;
	unsigned long flags;

	i = 0;

	__save_flags(flags);
	__cli();

	outb(0xff,io);
	v = inb(io);
	t = p = JS_GE_START;

	while (t > 0 && i < length) {
		t--;
		u = v; v = inb(io);
		if (v & ~u & 0x10) {
			data[i++] = v >> 5;
			p = t = (p - t) << 3;
		}
	}

	__restore_flags(flags);

	return i;
}


/*
 * js_ge_init_digital() initializes a Genius 3D Pro joystick
 * into digital mode.
 */

static void js_ge_trigger_seq(int io, int *seq)
{

	unsigned long flags;
	int i, t;

        __save_flags(flags);
        __cli();

	i = 0;
        do {
                outb(0xff, io);
		t = JS_GE_TIMEOUT * 4000;
		while ((inb(io) & 1) && t) t--;
                udelay(seq[i]);
        } while (seq[++i]);

	outb(0xff, io);

	__restore_flags(flags);
}

/*
 * js_sw_get_bits() composes bits from the triplet buffer into a __u64.
 * Parameter 'pos' is bit number inside packet where to start at, 'num' is number
 * of bits to be read, 'shift' is offset in the resulting __u64 to start at, bits
 * is number of bits per triplet.
 */

#define GB(p,n,s)	js_ge_get_bits(data, p, n, s)

static int js_ge_get_bits(unsigned char *buf, int pos, int num, int shift)
{
	__u64 data = 0;
	int i;

	for (i = 0; i < num / 3 + 2; i++)
		data |= buf[pos / 3 + i] << (i * 3);
	data >>= pos % 3;
	data &= (1 << num) - 1;
	data <<= shift;

	return data;
}

/*
 * js_ge_print_packet() prints the contents of a Genius packet.
 */

static void js_ge_print_packet(char *name, int length, unsigned char *buf)
{
	int i;

	printk("joy-genius: %s packet, %d bits. [", name, length);
	for (i = (((length + 3) >> 2) - 1); i >= 0; i--)
		printk("%x", js_ge_get_bits(buf, i << 2, 4, 0));
	printk("]\n");
}

/*
 * js_ge_read() reads and analyzes Genius joystick data,
 * and writes the results into the axes and buttons arrays.
 */

static int js_ge_read(void *xinfo, int **axes, int **buttons)
{
	struct js_ge_info *info = xinfo;
	unsigned char data[JS_GE_LENGTH];
	int hat;

	if (js_ge_read_packet(info->io, js_ge_length[info->id], data) < js_ge_length[info->id])
		return -1;

	switch (info->id) {

		case JS_GE_ID_F23P:

			if ((hat = GB(40,4,0)) > 8) return -1;
		
			axes[0][0] = GB( 0,8,0) | GB(46,1,8) | GB(50,1,9);
			axes[0][1] = GB( 8,8,0) | GB(47,1,8) | GB(51,1,9);
			axes[0][2] = GB(16,8,0) | GB(48,1,8) | GB(52,1,9);
			axes[0][3] = GB(24,8,0) | GB(49,1,8) | GB(53,1,9);
			
			axes[0][4] = js_ge_hat_to_axis[hat].x;
			axes[0][5] = js_ge_hat_to_axis[hat].y;

			buttons[0][0] = GB(44,2,0) | GB(32,8,2) | GB(78,2,10);


			return 0;
	}

	return -1;
}

/*
 * js_ge_open() is a callback from the file open routine.
 */

static int js_ge_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * js_ge_close() is a callback from the file release routine.
 */

static int js_ge_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * js_ge_init_corr() initializes the correction values for
 * Geniuss.
 */

static void __init js_ge_init_corr(int naxes, int hats, struct js_corr **corr)
{
	int i;

	for (i = 0; i < naxes; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 8;
		corr[0][i].coef[0] = 511 - 32;
		corr[0][i].coef[1] = 512 + 32;
		corr[0][i].coef[2] = (1 << 29) / (511 - 32);
		corr[0][i].coef[3] = (1 << 29) / (511 - 32);
	}

	for (i = naxes; i < naxes + hats * 2; i++) {
		corr[0][i].type = JS_CORR_BROKEN;
		corr[0][i].prec = 0;
		corr[0][i].coef[0] = 0;
		corr[0][i].coef[1] = 0;
		corr[0][i].coef[2] = (1 << 29);
		corr[0][i].coef[3] = (1 << 29);
	}
}

/*
 * js_ge_probe() probes for Genius id joysticks.
 */

static struct js_port __init *js_ge_probe(int io, struct js_port *port)
{
	struct js_ge_info info;
	char *names[] = {"", "Genius G09", "Genius F30D", "Genius F30", "Genius F31D",
				"Genius F30-5", "Genius F23 Pro", "Genius F31"};
	char hats[] =    { 0, 0, 0, 0, 0, 0, 1, 0 };
	char axes[] =    { 0, 0, 0, 0, 0, 0, 4, 0 };
	char buttons[] = { 0, 0, 0, 0, 0, 0, 8, 0 };
	unsigned char data[JS_GE_LENGTH];
	int i;

	if (check_region(io, 1)) return port;

	info.io = io;

	js_ge_trigger_seq(io, seq_reset);

	wait_ms(80);

	js_ge_trigger_seq(io, seq_digital);

	wait_ms(80);

	i = js_ge_read_packet(io, JS_GE_LENGTH, data);
	js_ge_print_packet("ID", i*3, data);

	info.id = GB(7,2,0) | GB(3,3,2) | GB(0,3,5);
	
	printk("id:%d\n", info.id);

	wait_ms(4);

	i = js_ge_read_packet(io, JS_GE_LENGTH, data);
	js_ge_print_packet("Data", i*3, data);

	wait_ms(4);

	i = js_ge_read_packet(io, JS_GE_LENGTH, data);
	js_ge_print_packet("Data", i*3, data);

	wait_ms(4);

	if (!info.id || info.id > JS_GE_ID_MAX)
		return port;

	request_region(io, 1, "joystick (genius)");

	port = js_register_port(port, &info, 1, sizeof(struct js_ge_info), js_ge_read);

	printk(KERN_INFO "js%d: Genius %s at %#x\n",
		js_register_device(port, 0, axes[info.id] + 2 * hats[info.id], buttons[info.id],
			names[info.id], js_ge_open, js_ge_close), names[info.id], io);

	js_ge_init_corr(axes[info.id], hats[info.id], port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init js_ge_init(void)
#endif
{
	int *p;

	for (p = js_ge_port_list; *p; p++) js_ge_port = js_ge_probe(*p, js_ge_port);
	if (js_ge_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-genius: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct js_ge_info *info;

	while (js_ge_port) {
		js_unregister_device(js_ge_port->devs[0]);
		info = js_ge_port->info;
		release_region(info->io, 1);
		js_ge_port = js_unregister_port(js_ge_port);
	}
}
#endif
