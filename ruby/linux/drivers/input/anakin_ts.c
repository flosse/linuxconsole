/*
 *  linux/drivers/input/anakin_ts.c
 *
 *  Copyright (c) 2001 "Crazy" James Simmons jsimmons@transvirtual.com
 *
 *  Sponsored by SuSE, Transvirtual Technology.
 *
 *  Based on work by :	Tak-Shing Chan <chan@aleph1.co.uk> 	
 *
 *  Copyright (C) 2001 Aleph One Ltd. for Acunia N.V.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *  Changelog:
 *   18-Apr-2001 TTC	Created
 *   24-Jun-2001 JAS	Ported to new input api
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/input.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/irq.h>

/*
 * TSBUF_SIZE must be a power of two
 */
#define TSBUF_SIZE	256
#define ANAKIN_TS_ON	1	
#define ANAKIN_TS_OFF	0	
#define NEXT(index)	(((index) + 1) & (TSBUF_SIZE - 1))

static struct input_dev anakin_ts_dev;
static unsigned short buffer[TSBUF_SIZE][4];
static int head, tail, xscale, xtrans, yscale, ytrans;

static char *anakin_ts_name = "Anakin TouchScreen";
static char *anakin_ts_phys[32];

/*
 * Interrupt handler
 */
static void
anakin_ts_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned int status = __raw_readl(IO_BASE + IO_CONTROLLER + 0x24);

/*
	if (NEXT(head) != tail) {
		 *
		 * iPAQ format (u16 pressure, x, y, millisecs)
		 *
		switch (status >> 20 & 3) {
		case 0:
			return;
		case 2:
			buffer[head][0] = 0;
			break;
		default:
			buffer[head][0] = 0x7f;
		}
		buffer[head][1] = status >> 2 & 0xff;
		buffer[head][2] = status >> 12 & 0xff;
		buffer[head][3] = jiffies;
	
		while (head != tail && count >= sizeof data) {
                	data[0] = buffer[tail][0];
                	data[1] = (xscale * buffer[tail][1] >> 8) + xtrans;
                	data[2] = (yscale * buffer[tail][2] >> 8) + ytrans;
                	data[3] = buffer[tail][3];
                	__copy_to_user(buf, data, sizeof data);
                	tail = NEXT(tail);
                	count -= sizeof data;
                	buf += sizeof data;
                	written += sizeof data;
       		}

		input_report_key(&anakin_ts_dev, BTN_TOUCH, value);
        	input_report_abs(&anakin_ts_dev, ABS_X, value);
	        input_report_abs(&anakin_ts_dev, ABS_Y, value);
		head = NEXT(head);
	}
*/
}

static int anakin_ts_open(struct input_dev *dev)
{
	__raw_writel(ANAKIN_TS_ON, IO_BASE + IO_CONTROLLER + 8);        
        return 0;
}

static void anakin_ts_close(struct input_dev *dev)
{
	__raw_writel(ANAKIN_TS_OFF, IO_BASE + IO_CONTROLLER + 8);
}

/*
 * Initialization and exit routines
 */
static int __init anakin_ts_init(void)
{
	int retval;

	if ((retval = request_irq(IRQ_TOUCHSCREEN, anakin_ts_handler,
			SA_INTERRUPT, "anakints", 0))) {
		printk("anakints: failed to get IRQ\n");
		return retval;
	}
	__raw_writel(ANAKIN_TS_ON, IO_BASE + IO_CONTROLLER + 8);
	printk("Anakin touchscreen driver initialized\n");
	head = tail = 0;

	/*
	 * TODO: get rid of these hardcoded values
	 */
	xscale = -439;		/* 256 * Xsize / (Xout - Xoff) */
	xtrans = 415;		/* -Xoff * Xsize / (Xout - Xoff) */
	yscale = -283;		/* 256 * Ysize / (Yout - Yoff) */
	ytrans = 257;		/* -Yoff * Ysize / (Yout - Yoff) */

        anakin_ts_dev.evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
        anakin_ts_dev.absbit[0] = BIT(ABS_X) | BIT(ABS_Y);
        anakin_ts_dev.keybit[LONG(BTN_TOUCH)] = BIT(BTN_TOUCH);

        anakin_ts_dev.absmax[ABS_X] = 0x1ff;
        anakin_ts_dev.absmax[ABS_Y] = 0x0ff;

        anakin_ts_dev.open = anakin_ts_open;
        anakin_ts_dev.close = anakin_ts_close;

	sprintf(anakin_ts_phys, "isa%04x/input0", IO_BASE);

        anakin_ts_dev.name = anakin_ts_name;
	anakin_ts_dev.phys = anakin_ts_phys;
        anakin_ts_dev.idbus = BUS_ISA;
        anakin_ts_dev.idvendor = 0x0003;
        anakin_ts_dev.idproduct = 0x0001;
        anakin_ts_dev.idversion = 0x0100;

	input_register_device(&anakin_ts_dev);

        printk(KERN_INFO "input: %s at %#x irq %d\n",
		anakin_ts_name, IO_BASE, IRQ_TOUCHSCREEN);
        return 0;
}

static void __exit anakin_ts_exit(void)
{
	input_unregister_device(&anakin_ts_dev);
	__raw_writel(ANAKIN_TS_OFF, IO_BASE + IO_CONTROLLER + 8);
	free_irq(IRQ_TOUCHSCREEN, 0);
}

module_init(anakin_ts_init);
module_exit(anakin_ts_exit);

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION("Anakin touchscreen driver");
MODULE_SUPPORTED_DEVICE("touchscreen/anakin");
