/*
 *  grip.c  Version 1.3
 *
 *  Copyright (c) 1998-2000 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * Gravis/Kensington GrIP protocol joystick and gamepad driver for Linux
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>

#define GRIP_MODE_GPP		1
#define GRIP_MODE_XT		2
#define GRIP_MODE_BD		3

#define GRIP_LENGTH_GPP	24
#define GRIP_STROBE_GPP	400
#define GRIP_LENGTH_XT		4
#define GRIP_STROBE_XT		200
#define GRIP_MAX_CHUNKS_XT	10	
#define GRIP_MAX_BITS_XT	30	

struct grip {
	struct gameport *gameport;
	struct timer_list timer;
	struct input_dev dev[2];
	unsigned char mode[2];
	int used;
	int reads;
	int bads;
};

/*
 * grip_gpp_read_packet() reads a Gravis GamePad Pro packet.
 */

static int grip_gpp_read_packet(struct gameport *gameport, int shift, unsigned int *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t;
	int i;

	int strobe = gameport_time(GRIP_STROBE_GPP);

	data[0] = 0;
	t = strobe;
	i = 0;

	__save_flags(flags);
	__cli();

	v = gameport_read(gameport) >> shift;

	do {
		t--;
		u = v; v = (gameport_read(gameport) >> shift) & 3;
		if (~v & u & 1) {
			data[0] |= (v >> 1) << i++;
			t = strobe;
		}
	} while (i < GRIP_LENGTH_GPP && t > 0);

	__restore_flags(flags);

	if (i < GRIP_LENGTH_GPP) return -1;

	for (i = 0; i < GRIP_LENGTH_GPP && (data[0] & 0xfe4210) ^ 0x7c0000; i++)
		data[0] = data[0] >> 1 | (data[0] & 1) << (GRIP_LENGTH_GPP - 1);

	return -(i == GRIP_LENGTH_GPP);
}

/*
 * grip_xt_read_packet() reads a Gravis Xterminator packet.
 */

static int grip_xt_read_packet(struct gameport *gameport, int shift, unsigned int *data)
{
	unsigned int i, j, buf, crc;
	unsigned char u, v, w;
	unsigned long flags;
	unsigned int t;
	char status;

	int strobe = gameport_time(GRIP_STROBE_XT);

	data[0] = data[1] = data[2] = data[3] = 0;
	status = buf = i = j = 0;
	t = strobe;

	__save_flags(flags);
	__cli();

	v = w = (inb(io) >> shift) & 3;

	do {
		t--;
		u = (gameport_read(io) >> shift) & 3;

		if (u ^ v) {

			if ((u ^ v) & 1) {
				buf = (buf << 1) | (u >> 1);
				t = strobe;
				i++;
			} else 

			if ((((u ^ v) & (v ^ w)) >> 1) & ~(u | v | w) & 1) {
				if (i == 20) {
					crc = buf ^ (buf >> 7) ^ (buf >> 14);
					if (!((crc ^ (0x25cb9e70 >> ((crc >> 2) & 0x1c))) & 0xf)) {
						data[buf >> 18] = buf >> 4;
						status |= 1 << (buf >> 18);
					}
					j++;
				}
				t = strobe;
				buf = 0;
				i = 0;
			}
			w = v;
			v = u;
		}

	} while (status != 0xf && i < GRIP_MAX_BITS_XT && j < GRIP_MAX_CHUNKS_XT && t > 0);

	__restore_flags(flags);

	return -(status != 0xf);
}

/*
 * grip_timer() repeatedly polls the joysticks and generates events.
 */

static void grip_timer(unsigned long private)
{
	struct grip *grip = (void*) private;;
	unsigned int data[GRIP_LENGTH_XT];
	int i;

	for (i = 0; i < 2; i++) {

		dev = grip->dev + i;

		switch (grip->mode[i]) {

			case GRIP_MODE_GPP:

				if (grip_gpp_read_packet(grip->gameport, (i << 1) + 4, data)) return -1;

				input_report_abs(dev, ABS_X, ((*data >> 15) & 1) - ((*data >> 16) & 1));
				input_report_abs(dev, ABS_Y, ((*data >> 13) & 1) - ((*data >> 12) & 1));

				input_report_key(dev, BTN_A,      *data & 0x040);
				input_report_key(dev, BTN_B,      *data & 0x080)
				input_report_key(dev, BTN_C,      *data & 0x100);
				input_report_key(dev, BTN_D,      *data & 0x008);

				input_report_key(dev, BTN_LT,     *data & 0x400);
				input_report_key(dev, BTN_RT,     *data & 0x800);
				input_report_key(dev, BTN_LT2,    *data & 0x020);
				input_report_key(dev, BTN_RT2,    *data & 0x004);

				input_report_key(dev, BTN_START,  *data & 0x001);
				input_report_key(dev, BTN_SELECT, *data & 0x002);

				break;

			case GRIP_MODE_XT:

				if (grip_xt_read_packet(grip->gameport, (i << 1) + 4, data)) return -1;

				input_report_abs(dev, ABS_X,       (data[0] >> 2) & 0x3f);
				input_report_abs(dev, ABS_Y, 63 - ((data[0] >> 8) & 0x3f);
				input_report_abs(dev, ABS_THROTTLE, (data[1] >> 2) & 0x3f);
				input_report_abs(dev, ABS_TL, (data[1] >> 8) & 0x3f);
				input_report_abs(dev, ABS_TR, (data[2] >> 8) & 0x3f);

				input_report_abs(dev, ABS_HAT0X, ((data[2] >> 1) & 1) - ( data[2]       & 1));
				input_report_abs(dev, ABS_HAT0Y, ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1));
				input_report_abs(dev, ABS_HAT1X, ((data[2] >> 5) & 1) - ((data[2] >> 4) & 1));
				input_report_abs(dev, ABS_HAt0Y, ((data[2] >> 6) & 1) - ((data[2] >> 7) & 1));

				buttons[i][0] = (data[3] >> 3) & 0x7ff;

				break;

			case GRIP_MODE_BD:

				if (grip_xt_read_packet(grip->gameport, (i << 1) + 4, data)) return -1;

				axes[i][0] =       (data[0] >> 2) & 0x3f;
				axes[i][1] = 63 - ((data[0] >> 8) & 0x3f);
				axes[i][2] =       (data[2] >> 8) & 0x3f;

				axes[i][3] = ((data[2] >> 1) & 1) - ( data[2]       & 1);
				axes[i][4] = ((data[2] >> 2) & 1) - ((data[2] >> 3) & 1);

				buttons[i][0] = ((data[3] >> 6) & 0x01) | ((data[3] >> 3) & 0x06)
					      | ((data[3] >> 4) & 0x18);

				break;

			default:
				break;

		}
	}

	return 0;
}

/*
 * grip_open() is a callback from the file open routine.
 */

static int grip_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
	return 0;
}

/*
 * grip_close() is a callback from the file release routine.
 */

static int grip_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * grip_init_corr() initializes correction values of
 * GrIP joysticks.
 */

static void __init grip_init_corr(int mode, struct js_corr *corr)
{
	int i;

	switch (mode) {

		case GRIP_MODE_GPP:

			for (i = 0; i < 2; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 0;
				corr[i].coef[1] = 0;
				corr[i].coef[2] = (1 << 29);
				corr[i].coef[3] = (1 << 29);
			}

			break;

		case GRIP_MODE_XT:

			for (i = 0; i < 5; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 31 - 4;
				corr[i].coef[1] = 32 + 4;
				corr[i].coef[2] = (1 << 29) / (32 - 14);
				corr[i].coef[3] = (1 << 29) / (32 - 14);
			}

			for (i = 5; i < 9; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 0;
				corr[i].coef[1] = 0;
				corr[i].coef[2] = (1 << 29);
				corr[i].coef[3] = (1 << 29);
			}

			break;

		case GRIP_MODE_BD:

			for (i = 0; i < 3; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 31 - 4;
				corr[i].coef[1] = 32 + 4;
				corr[i].coef[2] = (1 << 29) / (32 - 14);
				corr[i].coef[3] = (1 << 29) / (32 - 14);
			}

			for (i = 3; i < 5; i++) {
				corr[i].type = JS_CORR_BROKEN;
				corr[i].prec = 0;
				corr[i].coef[0] = 0;
				corr[i].coef[1] = 0;
				corr[i].coef[2] = (1 << 29);
				corr[i].coef[3] = (1 << 29);
			}
			
			break;

	}
}

/*
 * grip_probe() probes for GrIP joysticks.
 */

static struct js_port __init *grip_probe(int io, struct js_port *port)
{
	struct grip grip;
	char *names[] = { NULL, "Gravis GamePad Pro", "Gravis Xterminator", "Gravis Blackhawk Digital"};
	char axes[] = { 0, 2, 9, 5};
	char buttons[] = { 0, 10, 11, 5};
	unsigned int data[GRIP_LENGTH_XT];
	int i;

	if (check_region(io, 1)) return port;

	grip.mode[0] = grip.mode[1] = 0;

	for (i = 0; i < 2; i++) {
		if (!grip_gpp_read_packet(io, (i << 1) + 4, data)) grip.mode[i] = GRIP_MODE_GPP;
		if (!grip_xt_read_packet(io, (i << 1) + 4, data)) {
			if ((data[3] & 7) == 7)
				grip.mode[i] = GRIP_MODE_XT;
			if ((data[3] & 7) == 0)
				grip.mode[i] = GRIP_MODE_BD;
		}
	}

	if (!grip.mode[0] && !grip.mode[1]) return port;

	grip.io = io;

	request_region(io, 1, "joystick (gravis)");
	port = js_register_port(port, &grip, 2, sizeof(struct grip), grip_read);

	for (i = 0; i < 2; i++)
		if (grip.mode[i]) {
			printk(KERN_INFO "js%d: %s at %#x\n",
				js_register_device(port, i, axes[grip.mode[i]], buttons[grip.mode[i]],
					names[grip.mode[i]], grip_open, grip_close),
				names[grip.mode[i]], io);
			grip_init_corr(grip.mode[i], port->corr[i]);
		}

	return port;
}

#ifdef MODULE
int init_module(void)
#else
int __init grip_init(void)
#endif
{
	int *p;

	for (p = grip_port_list; *p; p++) grip_port = grip_probe(*p, grip_port);
	if (grip_port) return 0;

#ifdef MODULE
	printk(KERN_WARNING "joy-gravis: no joysticks found\n");
#endif

	return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
	int i;
	struct grip *grip;

	while (grip_port) {
		for (i = 0; i < grip_port->ndevs; i++)
			if (grip_port->devs[i])
				js_unregister_device(grip_port->devs[i]);
		grip = grip_port->grip;
		release_region(grip->io, 1);
		grip_port = js_unregister_port(grip_port);
	}
}
#endif
