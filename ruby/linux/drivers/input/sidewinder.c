/*
 *  sidewinder.c.c  Version 1.2
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux input driver, supporting
 * Microsoft SideWinder digital joystick family.
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/input.h>
#include "gameport.h"

/*
 * These are really magic values. Changing them can make a problem go away,
 * as well as break everything.
 */

#undef SW_DEBUG

#define SW_START	400	/* The time we wait for the first bit [400 us] */
#define SW_STROBE	45	/* Max time per bit [45 us] */
#define SW_TIMEOUT	4000	/* Wait for everything to settle [4 ms] */
#define SW_KICK		45	/* Wait after A0 fall till kick [45 us] */
#define SW_END		8	/* Number of bits before end of packet to kick */
#define SW_FAIL		16	/* Number of packet read errors to fail and reinitialize */
#define SW_BAD		2	/* Number of packet read errors to switch off 3d Pro optimization */
#define SW_OK		64	/* Number of packet read successes to switch optimization back on */
#define SW_LENGTH	512	/* Max number of bits in a packet */

/*
 * SideWinder joystick types ...
 */

#define SW_ID_3DP	0
#define SW_ID_GP	1
#define SW_ID_PP	2
#define SW_ID_FFP	3
#define SW_ID_FSP	4
#define SW_ID_FFW	5

/*
 * Names, buttons, axes ...
 */

static char *sw_names[] = {	"SideWinder 3D Pro", "SideWinder GamePad", "SideWinder Precision Pro",
				"SideWinder Force Feedback Pro", "SideWinder FreeStyle Pro",
				"SideWinder Force Feedback Wheel" };

static char sw_3dp_abs[] =	{ ABS_X, ABS_Y, ABS_RZ, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y };
static char sw_gp_abs[] = 	{ ABS_X, ABS_Y };
static char sw_pp_abs[] =	{ ABS_X, ABS_Y, ABS_RZ, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y };
static char sw_ffp_abs[] = 	{ ABS_X, ABS_Y, ABS_RZ, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y };
static char sw_fsp_abs[] = 	{ ABS_X, ABS_Y,         ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y };

3dp_btn = { BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_THUMB2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4 };
gp_btn =  { BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_START, BTN_MODE };
pp_btn = { BTN_TRIGGER, BTN_TOP, BTN_THUMB, BTN_THUMB2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4 };

char axes[] = { 0, 6, 6, 2, 6, 6, 5, 3 };
char buttons[] = { 0, 9, 9, 10, 9, 9, 10, 8 };

static struct {
	int x;
	int y;
} sw_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

struct sw {
	int io;
	int length;
	int speed;
	unsigned char type;
	unsigned char bits;
	unsigned char number;
	unsigned char fail;
	unsigned char ok;
};

/*
 * Gameport speed.
 */

unsigned int sw_io_speed = 0;

/*
 * sw_measure_speed() measures the gameport i/o speed.
 */

static int __init sw_measure_speed(int io)
{
#ifdef __i386__

#define GET_TIME(x)     do { outb(0, 0x43); x = inb(0x40); x |= inb(0x40) << 8; } while (0)
#define DELTA(x,y)      ((y)-(x)+((y)<(x)?1193180L/HZ:0))

	unsigned int i, t, t1, t2, t3, tx;
	unsigned long flags;

	tx = 1 << 30;

	for(i = 0; i < 50; i++) {
		save_flags(flags);	/* Yes, all CPUs */
		cli();
		GET_TIME(t1);
		for(t = 0; t < 50; t++) inb(io);
		GET_TIME(t2);
		GET_TIME(t3);
		restore_flags(flags);
		udelay(i * 10);
		if ((t = DELTA(t2,t1) - DELTA(t3,t2)) < tx) tx = t;
	}

	return 59659 / t;

#else

	unsigned int j, t = 0;

	j = jiffies; while (j == jiffies);
	j = jiffies; while (j == jiffies) { t++; inb(0x201); }

	return t * HZ / 1000;

#endif
}

/*
 * sw_read_packet() is a function which reads either a data packet, or an
 * identification packet from a SideWinder joystick. Better don't try to
 * understand this, since all the ugliness of the Microsoft Digital
 * Overdrive protocol is concentrated in this function. If you really want
 * to know how this works, first go watch a couple horror movies, so that
 * you are well prepared, read US patent #5628686 and then e-mail me,
 * and I'll send you an explanation.
 *					Vojtech <vojtech@suse.cz>
 */

static int sw_read_packet(int io, int speed, unsigned char *buf, int length, int id)
{
	unsigned long flags;
	int timeout, bitout, sched, i, kick, start, strobe;
	unsigned char pending, u, v;

	i = -id;						/* Don't care about data, only want ID */
	timeout = id ? (SW_TIMEOUT * speed) >> 10 : 0;	/* Set up global timeout for ID packet */
	kick = id ? (SW_KICK * speed) >> 10 : 0;		/* Set up kick timeout for ID packet */
	start = (SW_START * speed) >> 10;
	strobe = (SW_STROBE * speed) >> 10;
	bitout = start;
	pending = 0;
	sched = 0;

        __save_flags(flags);					/* Quiet, please */
        __cli();

	outb(0xff, io);						/* Trigger */
	v = inb(io);

	do {
		bitout--;
		u = v;
		v = inb(io);
	} while (!(~v & u & 0x10) && (bitout > 0));		/* Wait for first falling edge on clock */

	if (bitout > 0) bitout = strobe;			/* Extend time if not timed out */

	while ((timeout > 0 || bitout > 0) && (i < length)) {

		timeout--;
		bitout--;					/* Decrement timers */
		sched--;

		u = v;
		v = inb(io);

		if ((~u & v & 0x10) && (bitout > 0)) {		/* Rising edge on clock - data bit */
			if (i >= 0)				/* Want this data */
				buf[i] = v >> 5;		/* Store it */
			i++;					/* Advance index */
			bitout = strobe;			/* Extend timeout for next bit */
		} 

		if (kick && (~v & u & 0x01)) {			/* Falling edge on axis 0 */
			sched = kick;				/* Schedule second trigger */
			kick = 0;				/* Don't schedule next time on falling edge */
			pending = 1;				/* Mark schedule */
		} 

		if (pending && sched < 0 && (i > -SW_END)) {	/* Second trigger time */
			outb(0xff, io);				/* Trigger */
			bitout = start;				/* Long bit timeout */
			pending = 0;				/* Unmark schedule */
			timeout = 0;				/* Switch from global to bit timeouts */ 
		}
	}

	__restore_flags(flags);					/* Done - relax */

#ifdef SW_DEBUG
	{
		int j;
		dbg("Read %d triplets. [", i);
		for (j = 0; j < i; j++) printk("%d", buf[j]);
		printk("]\n");
	}
#endif

	return i;
}

/*
 * sw_get_bits() and GB() compose bits from the triplet buffer into a __u64.
 * Parameter 'pos' is bit number inside packet where to start at, 'num' is number
 * of bits to be read, 'shift' is offset in the resulting __u64 to start at, bits
 * is number of bits per triplet.
 */

#define GB(pos,num,shift) sw_get_bits(buf, pos, num, shift, sw->bits)

static __u64 sw_get_bits(unsigned char *buf, int pos, int num, char shift, char bits)
{
	__u64 data = 0;
	int tri = pos % bits;						/* Start position */
	int i   = pos / bits;
	int bit = shift;

	while (num--) {
		data |= (__u64)((buf[i] >> tri++) & 1) << bit++;	/* Transfer bit */
		if (tri == bits) {
			i++;						/* Next triplet */
			tri = 0;
		}
	}

	return data;
}

/*
 * sw_init_digital() initializes a SideWinder 3D Pro joystick
 * into digital mode.
 */

static void sw_init_digital(int io, int speed)
{
	int seq[] = { 140, 140+725, 140+300, 0 };
	unsigned long flags;
	int i, t;

        __save_flags(flags);
        __cli();

	i = 0;
        do {
                outb(0xff, io);					/* Trigger */
		t = (SW_TIMEOUT * speed) >> 10;
		while ((inb(io) & 1) && t) t--;			/* Wait for axis to fall back to 0 */
                udelay(seq[i]);					/* Delay magic time */
        } while (seq[++i]);

	outb(0xff, io);						/* Last trigger */

	__restore_flags(flags);
}

/*
 * sw_parity() computes parity of __u64
 */

static int sw_parity(__u64 t)
{
	int x = t ^ (t >> 32);
	x ^= x >> 16;
	x ^= x >> 8;
	x ^= x >> 4;
	x ^= x >> 2;
	x ^= x >> 1;
	return x & 1;
}

/*
 * sw_ccheck() checks synchronization bits and computes checksum of nibbles.
 */

static int sw_check(__u64 t)
{
	char sum = 0;

	if ((t & 0x8080808080808080ULL) ^ 0x80)			/* Sync */
		return -1;

	while (t) {						/* Sum */
		sum += t & 0xf;
		t >>= 4;
	}

	return sum & 0xf;
}

/*
 * sw_parse() analyzes SideWinder joystick data, and writes the results into
 * the axes and buttons arrays.
 */

static int sw_parse(unsigned char *buf, struct sw *sw, int **axes, int **buttons)
{
	int hat, i;

	switch (sw->type) {

		case SW_ID_3DP:

			if (sw_check(GB(0,64,0)) || (hat = GB(6,1,3) | GB(60,3,0))  > 8) return -1;

			axes[0][0] = GB( 3,3,7) | GB(16,7,0);
			axes[0][1] = GB( 0,3,7) | GB(24,7,0);
			axes[0][2] = GB(35,2,7) | GB(40,7,0);
			axes[0][3] = GB(32,3,7) | GB(48,7,0);
			axes[0][4] = sw_hat_to_axis[hat].x;
			axes[0][5] = sw_hat_to_axis[hat].y;
			buttons[0][0] = ~(GB(37,1,8) | GB(38,1,7) | GB(8,7,0));

			return 0;

		case SW_ID_GP:

			for (i = 0; i < sw->number * 15; i += 15) {

				if (sw_parity(GB(i,15,0))) return -1;

				axes[i][0] = GB(i+3,1,0) - GB(i+2,1,0);
				axes[i][1] = GB(i+0,1,0) - GB(i+1,1,0);
				buttons[i][0] = ~GB(i+4,10,0);

			}

			return 0;

		case SW_ID_PP:
		case SW_ID_FFP:

			if (!sw_parity(GB(0,48,0)) || (hat = GB(42,4,0)) > 8) return -1;

			axes[0][0] = GB( 9,10,0);
			axes[0][1] = GB(19,10,0);
			axes[0][2] = GB(36, 6,0);
			axes[0][3] = GB(29, 7,0);
			axes[0][4] = sw_hat_to_axis[hat].x;
			axes[0][5] = sw_hat_to_axis[hat].y;
			buttons[0][0] = ~GB(0,9,0);

			return 0;

		case SW_ID_FSP:

			if (!sw_parity(GB(0,43,0)) || (hat = GB(28,4,0)) > 8) return -1;

			axes[0][0] = GB( 0,10,0);
			axes[0][1] = GB(16,10,0);
			axes[0][2] = GB(32, 6,0);
			axes[0][3] = sw_hat_to_axis[hat].x;
			axes[0][4] = sw_hat_to_axis[hat].y;
			buttons[0][0] = ~(GB(10,6,0) | GB(26,2,6) | GB(38,2,8));

			return 0;

		case SW_ID_FFW:

			if (!sw_parity(GB(0,33,0))) return -1;

			axes[0][0] = GB( 0,10,0);
			axes[0][1] = GB(10, 6,0);
			axes[0][2] = GB(16, 6,0);
			buttons[0][0] = ~GB(22,8,0);

			return 0;
	}

	return -1;
}

/*
 * sw_read() reads SideWinder joystick data, and reinitializes
 * the joystick in case of persistent problems. This is the function that is
 * called from the generic code to poll the joystick.
 */

static int sw_read(void *xsw, int **axes, int **buttons)
{
	struct sw *sw = xsw;
	unsigned char buf[SW_LENGTH];
	int i;

	i = sw_read_packet(sw->io, sw->speed, buf, sw->length, 0);

	if (sw->type == SW_ID_3DP && sw->length == 66 && i != 66) {		/* Broken packet, try to fix */

		if (i == 64 && !sw_check(sw_get_bits(buf,0,64,0,1))) {		/* Last init failed, 1 bit mode */
			printk(KERN_WARNING "sidewinder.c: Joystick in wrong mode on %#x"
				" - going to reinitialize.\n", sw->io);
			sw->fail = SW_FAIL;					/* Reinitialize */
			i = 128;						/* Bogus value */
		}

		if (i < 66 && GB(0,64,0) == GB(i*3-66,64,0))			/* 1 == 3 */
			i = 66;							/* Everything is fine */

		if (i < 66 && GB(0,64,0) == GB(66,64,0))			/* 1 == 2 */
			i = 66;							/* Everything is fine */

		if (i < 66 && GB(i*3-132,64,0) == GB(i*3-66,64,0)) {		/* 2 == 3 */
			memmove(buf, buf + i - 22, 22);				/* Move data */
			i = 66;							/* Carry on */
		}
	}

	if (i == sw->length && !sw_parse(buf, sw, axes, buttons)) {		/* Parse data */

		sw->fail = 0;
		sw->ok++;

		if (sw->type == SW_ID_3DP && sw->length == 66			/* Many packets OK */
			&& sw->ok > SW_OK) {

			printk(KERN_INFO "sidewinder.c: No more trouble on %#x"
				" - enabling optimization again.\n", sw->io);
			sw->length = 22;
		}

		return 0;
	}

	sw->ok = 0;
	sw->fail++;

	if (sw->type == SW_ID_3DP && sw->length == 22 && sw->fail > SW_BAD) {	/* Consecutive bad packets */

		printk(KERN_INFO "sidewinder.c: Many bit errors on %#x"
			" - disabling optimization.\n", sw->io);
		sw->length = 66;
	}

	if (sw->fail < SW_FAIL) return -1;					/* Not enough, don't reinitialize yet */

	printk(KERN_WARNING "sidewinder.c: Too many bit errors on %#x"
		" - reinitializing joystick.\n", sw->io);

	if (!i && sw->type == SW_ID_3DP) {					/* 3D Pro can be in analog mode */
		udelay(3 * SW_TIMEOUT);
		sw_init_digital(sw->io, sw->speed);
	}

	udelay(SW_TIMEOUT);
	i = sw_read_packet(sw->io, sw->speed, buf, SW_LENGTH, 0);		/* Read normal data packet */
	udelay(SW_TIMEOUT);
	sw_read_packet(sw->io, sw->speed, buf, SW_LENGTH, i);			/* Read ID packet, this initializes the stick */

	sw->fail = SW_FAIL;
	
	return -1;
}

/*
 * sw_open() is a callback from the file open routine.
 */

static int sw_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * sw_close() is a callback from the file release routine.
 */

static int sw_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * sw_init_corr() initializes the correction values for
 * SideWinders.
 */

static void __init sw_init_corr(int num_axes, int type, int number, struct js_corr **corr)
{
	int i, j;

	for (i = 0; i < number; i++) {

		for (j = 0; j < num_axes; j++) {
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 8;
			corr[i][j].coef[0] = 511 - 32;
			corr[i][j].coef[1] = 512 + 32;
			corr[i][j].coef[2] = (1 << 29) / (511 - 32);
			corr[i][j].coef[3] = (1 << 29) / (511 - 32);
		}

		switch (type) {

			case SW_ID_3DP:

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 4;
				corr[i][2].coef[0] = 255 - 16;
				corr[i][2].coef[1] = 256 + 16;
				corr[i][2].coef[2] = (1 << 29) / (255 - 16);
				corr[i][2].coef[3] = (1 << 29) / (255 - 16);

				j = 4;

			break;

			case SW_ID_PP:
			case SW_ID_FFP:

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 0;
				corr[i][2].coef[0] = 31 - 2;
				corr[i][2].coef[1] = 32 + 2;
				corr[i][2].coef[2] = (1 << 29) / (31 - 2);
				corr[i][2].coef[3] = (1 << 29) / (31 - 2);

				corr[i][3].type = JS_CORR_BROKEN;
				corr[i][3].prec = 1;
				corr[i][3].coef[0] = 63 - 4;
				corr[i][3].coef[1] = 64 + 4;
				corr[i][3].coef[2] = (1 << 29) / (63 - 4);
				corr[i][3].coef[3] = (1 << 29) / (63 - 4);

				j = 4;

			break;

			case SW_ID_FFW:

				corr[i][0].type = JS_CORR_BROKEN;
				corr[i][0].prec = 2;
				corr[i][0].coef[0] = 511 - 8;
				corr[i][0].coef[1] = 512 + 8;
				corr[i][0].coef[2] = (1 << 29) / (511 - 8);
				corr[i][0].coef[3] = (1 << 29) / (511 - 8);

				corr[i][1].type = JS_CORR_BROKEN;
				corr[i][1].prec = 1;
				corr[i][1].coef[0] = 63;
				corr[i][1].coef[1] = 63;
				corr[i][1].coef[2] = (1 << 29) / -63;
				corr[i][1].coef[3] = (1 << 29) / -63;

				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 1;
				corr[i][2].coef[0] = 63;
				corr[i][2].coef[1] = 63;
				corr[i][2].coef[2] = (1 << 29) / -63;
				corr[i][2].coef[3] = (1 << 29) / -63;

				j = 3;

			break;

			case SW_ID_FSP:
				
				corr[i][2].type = JS_CORR_BROKEN;
				corr[i][2].prec = 0;
				corr[i][2].coef[0] = 31 - 2;
				corr[i][2].coef[1] = 32 + 2;
				corr[i][2].coef[2] = (1 << 29) / (31 - 2);
				corr[i][2].coef[3] = (1 << 29) / (31 - 2);

				j = 3;

			break;

			default:

				j = 0;
		}

		for (; j < num_axes; j++) {				/* Hats & other binary axes */
			corr[i][j].type = JS_CORR_BROKEN;
			corr[i][j].prec = 0;
			corr[i][j].coef[0] = 0;
			corr[i][j].coef[1] = 0;
			corr[i][j].coef[2] = (1 << 29);
			corr[i][j].coef[3] = (1 << 29);
		}
	}
}

/*
 * sw_print_packet() prints the contents of a SideWinder packet.
 */

static void sw_print_packet(char *name, int length, unsigned char *buf, char bits)
{
	int i;

	printk("sidewinder.c: %s packet, %d bits. [", name, length);
	for (i = (((length + 3) >> 2) - 1); i >= 0; i--)
		printk("%x", (int)sw_get_bits(buf, i << 2, 4, 0, bits));
	printk("]\n");
}

/*
 * sw_3dp_id() translates the 3DP id into a human legible string.
 * Unfortunately I don't know how to do this for the other SW types.
 */

static void sw_3dp_id(unsigned char *buf, char *comment)
{
	int i;
	char pnp[8], rev[9];

	for (i = 0; i < 7; i++)						/* ASCII PnP ID */
		pnp[i] = sw_get_bits(buf, 24+8*i, 8, 0, 1);

	for (i = 0; i < 8; i++)						/* ASCII firmware revision */
		rev[i] = sw_get_bits(buf, 88+8*i, 8, 0, 1);

	pnp[7] = rev[8] = 0;

	sprintf(comment, " [PnP %d.%02d id %s rev %s]",
		(int) (sw_get_bits(buf, 8, 6, 6, 1) |			/* Two 6-bit values */
			sw_get_bits(buf, 16, 6, 0, 1)) / 100,
		(int) (sw_get_bits(buf, 8, 6, 6, 1) |
			sw_get_bits(buf, 16, 6, 0, 1)) % 100,
		 pnp, rev);
}

/*
 * sw_guess_mode() checks the upper two button bits for toggling -
 * indication of that the joystick is in 3-bit mode. This is documented
 * behavior for 3DP ID packet, and for example the FSP does this in
 * normal packets instead. Fun ...
 */

static int sw_guess_mode(unsigned char *buf, int len)
{
	int i;
	unsigned char xor = 0;
	for (i = 1; i < len; i++) xor |= (buf[i - 1] ^ buf[i]) & 6;
	return !!xor * 2 + 1;
}

/*
 * sw_probe() probes for SideWinder type joysticks.
 */

static struct js_port __init *sw_probe(int io, struct js_port *port)
{
	struct sw sw;
	int i, j, k, l;
	unsigned char buf[SW_LENGTH];
	unsigned char idbuf[SW_LENGTH];
	unsigned char m = 1;
	char comment[40];

	comment[0] = 0;

	if (check_region(io, 1)) return port;

	i = sw_read_packet(io, speed, buf, SW_LENGTH, 0);		/* Read normal packet */
	m |= sw_guess_mode(buf, i);					/* Data packet (1-bit) can carry mode info [FSP] */
	udelay(SW_TIMEOUT);
	dbg("Init 1: Mode %d. Length %d.", m , i);

	if (!i) {							/* No data. 3d Pro analog mode? */
		sw_init_digital(io, speed);				/* Switch to digital */
		udelay(SW_TIMEOUT);
		i = sw_read_packet(io, speed, buf, SW_LENGTH, 0);	/* Retry reading packet */
		udelay(SW_TIMEOUT);
		dbg("Init 1b: Length %d.", i);
		if (!i) return port;					/* No data -> FAIL */
	}

	j = sw_read_packet(io, speed, idbuf, SW_LENGTH, i);		/* Read ID. This initializes the stick */
	m |= sw_guess_mode(idbuf, j);					/* ID packet should carry mode info [3DP] */
	dbg("Init 2: Mode %d. ID Length %d.", m , j);

	if (!j) {							/* Read ID failed. Happens in 1-bit mode on PP */
		udelay(SW_TIMEOUT);
		i = sw_read_packet(io, speed, buf, SW_LENGTH, 0);	/* Retry reading packet */
		dbg("Init 2b: Mode %d. Length %d.", m , i);
		if (!i) return port;
		udelay(SW_TIMEOUT);
		j = sw_read_packet(io, speed, idbuf, SW_LENGTH, i);	/* Retry reading ID */
		dbg("Init 2c: ID Length %d.", j);
	}

	k = SW_FAIL;							/* Try SW_FAIL times */
	l = 0;

	do {
		k--;
		udelay(SW_TIMEOUT);
		i = sw_read_packet(io, speed, buf, SW_LENGTH, 0);	/* Read data packet */
		dbg("Init 3: Length %d.", i);

		if (i > l) {						/* Longer? As we can only lose bits, it makes */
									/* no sense to try detection for a packet shorter */
			l = i;						/* than the previous one */

			sw.number = 1;
			sw.io = io;
			sw.speed = speed;
			sw.length = i;
			sw.bits = m;
			sw.fail = 0;
			sw.ok = 0;
			sw.type = 0;

			switch (i * m) {
				case 60:
					sw.number++;
				case 45:				/* Ambiguous packet length */
					if (j <= 40) {			/* ID length less or eq 40 -> FSP */	
				case 43:
						sw.type = SW_ID_FSP;
						break;
					}
					sw.number++;
				case 30:
					sw.number++;
				case 15:
					sw.type = SW_ID_GP;
					break;
				case 33:
				case 31:
					sw.type = SW_ID_FFW;
					break;
				case 48:				/* Ambiguous */
					if (j == 14) {			/* ID length 14*3 -> FFP */
						sw.type = SW_ID_FFP;
						sprintf(comment, " [AC %s]", sw_get_bits(idbuf,38,1,0,3) ? "off" : "on");
					} else
					sw.type = SW_ID_PP;
					break;
				case 198:
					sw.length = 22;
				case 64:
					sw.type = SW_ID_3DP;
					if (j == 160) sw_3dp_id(idbuf, comment);
					break;
			}
		}

	} while (k && !sw.type);

	if (!sw.type) {
		printk(KERN_WARNING "sidewinder.c: unknown joystick device detected "
			"(io=%#x), contact <vojtech@suse.cz>\n", io);
		sw_print_packet("ID", j * 3, idbuf, 3);
		sw_print_packet("Data", i * m, buf, m);
		return port;
	}

#ifdef SW_DEBUG
	sw_print_packet("ID", j * 3, idbuf, 3);
	sw_print_packet("Data", i * m, buf, m);
#endif

	k = i;

	request_region(io, 1, "joystick (sidewinder)");

	port = js_register_port(port, &sw, sw.number, sizeof(struct sw), sw_read);

	for (i = 0; i < sw.number; i++)
		printk(KERN_INFO "js%d: %s%s at %#x [%d ns res %d-bit id %d data %d]\n",
			js_register_device(port, i, axes[sw.type], buttons[sw.type],
				names[sw.type], sw_open, sw_close), names[sw.type], comment, io,
				1000000 / speed, m, j, k);

	sw_init_corr(axes[sw.type], sw.type, sw.number, port->corr);

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init sw_init(void)
#endif
{
	int *p;

	for (p = sw_port_list; *p; p++) sw_port = sw_probe(*p, sw_port);
	if (sw_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "sidewinder.c: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct sw *sw;

	while (sw_port) {
		for (i = 0; i < sw_port->ndevs; i++)
			if (sw_port->devs[i])
				js_unregister_device(sw_port->devs[i]);
		sw = sw_port->sw;
		release_region(sw->io, 1);
		sw_port = js_unregister_port(sw_port);
	}

}
#endif
