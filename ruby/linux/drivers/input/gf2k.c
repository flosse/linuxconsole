/*
 *  gf2k.c  Version 1.2
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * Genius Flight 2000 joystick driver for Linux
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

#define GF2K_DEBUG

#define GF2K_START		400	/* The time we wait for the first bit [400 us] */
#define GF2K_TIMEOUT		4	/* Wait for everything to settle [4 ms] */
#define GF2K_LENGTH		80	/* Max number of triplets in a packet */

/*
 * Genius joystick ids ...
 */

#define GF2K_ID_G09		1
#define GF2K_ID_F30D		2
#define GF2K_ID_F30		3
#define GF2K_ID_F31D		4
#define GF2K_ID_F305		5
#define GF2K_ID_F23P		6
#define GF2K_ID_F31		7
#define GF2K_ID_MAX		7

static struct {
	int x;
	int y;
} gf2k_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static char gf2k_length[] = { 40, 40, 40, 40, 40, 40, 40, 40 };

int seq_reset[] = { 240, 340, 0 };
int seq_digital[] = { 590, 320, 860, 0 };

struct gf2k {
	int io;
	unsigned char id;
};

/*
 * gf2k_read_packet() reads an Assgesin 3D packet.
 */

static int gf2k_read_packet(int io, int length, char *data)
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
	t = p = GF2K_START;

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
 * gf2k_init_digital() initializes a Genius 3D Pro joystick
 * into digital mode.
 */

static void gf2k_trigger_seq(int io, int *seq)
{

	unsigned long flags;
	int i, t;

        __save_flags(flags);
        __cli();

	i = 0;
        do {
                outb(0xff, io);
		t = GF2K_TIMEOUT * 4000;
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

#define GB(p,n,s)	gf2k_get_bits(data, p, n, s)

static int gf2k_get_bits(unsigned char *buf, int pos, int num, int shift)
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
 * gf2k_print_packet() prints the contents of a Genius packet.
 */

static void gf2k_print_packet(char *name, int length, unsigned char *buf)
{
	int i;

	printk("joy-genius: %s packet, %d bits. [", name, length);
	for (i = (((length + 3) >> 2) - 1); i >= 0; i--)
		printk("%x", gf2k_get_bits(buf, i << 2, 4, 0));
	printk("]\n");
}

/*
 * gf2k_read() reads and analyzes Genius joystick data,
 * and writes the results into the axes and buttons arrays.
 */

static int gf2k_read(void *xgf2k, int **axes, int **buttons)
{
	struct gf2k *gf2k = xgf2k;
	unsigned char data[GF2K_LENGTH];
	int hat;

	if (gf2k_read_packet(gf2k->io, gf2k_length[gf2k->id], data) < gf2k_length[gf2k->id])
		return -1;

	switch (gf2k->id) {

		case GF2K_ID_F23P:

			if ((hat = GB(40,4,0)) > 8) return -1;
		
			axes[0][0] = GB( 0,8,0) | GB(46,1,8) | GB(50,1,9);
			axes[0][1] = GB( 8,8,0) | GB(47,1,8) | GB(51,1,9);
			axes[0][2] = GB(16,8,0) | GB(48,1,8) | GB(52,1,9);
			axes[0][3] = GB(24,8,0) | GB(49,1,8) | GB(53,1,9);
			
			axes[0][4] = gf2k_hat_to_axis[hat].x;
			axes[0][5] = gf2k_hat_to_axis[hat].y;

			buttons[0][0] = GB(44,2,0) | GB(32,8,2) | GB(78,2,10);


			return 0;
	}

	return -1;
}

/*
 * gf2k_open() is a callback from the file open routine.
 */

static int gf2k_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * gf2k_close() is a callback from the file release routine.
 */

static int gf2k_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * gf2k_init_corr() initializes the correction values for
 * Geniuss.
 */

static void __init gf2k_init_corr(int naxes, int hats, struct js_corr **corr)
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
 * gf2k_probe() probes for Genius id joysticks.
 */

static struct js_port __init *gf2k_probe(int io, struct js_port *port)
{
	struct gf2k gf2k;
	char *names[] = {"", "Genius G09", "Genius F30D", "Genius F30", "Genius F31D",
				"Genius F30-5", "Genius F23 Pro", "Genius F31"};
	char hats[] =    { 0, 0, 0, 0, 0, 0, 1, 0 };
	char axes[] =    { 0, 0, 0, 0, 0, 0, 4, 0 };
	char buttons[] = { 0, 0, 0, 0, 0, 0, 8, 0 };
	unsigned char data[GF2K_LENGTH];
	int i;

	if (check_region(io, 1)) return port;

	gf2k.io = io;

	gf2k_trigger_seq(io, seq_reset);

	wait_ms(80);

	gf2k_trigger_seq(io, seq_digital);

	wait_ms(80);

	i = gf2k_read_packet(io, GF2K_LENGTH, data);
	gf2k_print_packet("ID", i*3, data);

	gf2k.id = GB(7,2,0) | GB(3,3,2) | GB(0,3,5);
	
	printk("id:%d\n", gf2k.id);

	wait_ms(4);

	i = gf2k_read_packet(io, GF2K_LENGTH, data);
	gf2k_print_packet("Data", i*3, data);

	wait_ms(4);

	i = gf2k_read_packet(io, GF2K_LENGTH, data);
	gf2k_print_packet("Data", i*3, data);

	wait_ms(4);

	if (!gf2k.id || gf2k.id > GF2K_ID_MAX)
		return port;

	request_region(io, 1, "joystick (genius)");

	port = js_register_port(port, &gf2k, 1, sizeof(struct gf2k), gf2k_read);

	printk(KERN_INFO "js%d: Genius %s at %#x\n",
		js_register_device(port, 0, axes[gf2k.id] + 2 * hats[gf2k.id], buttons[gf2k.id],
			names[gf2k.id], gf2k_open, gf2k_close), names[gf2k.id], io);

	gf2k_init_corr(axes[gf2k.id], hats[gf2k.id], port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init gf2k_init(void)
#endif
{
	int *p;

	for (p = gf2k_port_list; *p; p++) gf2k_port = gf2k_probe(*p, gf2k_port);
	if (gf2k_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-genius: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct gf2k *gf2k;

	while (gf2k_port) {
		js_unregister_device(gf2k_port->devs[0]);
		gf2k = gf2k_port->gf2k;
		release_region(gf2k->io, 1);
		gf2k_port = js_unregister_port(gf2k_port);
	}
}
#endif
