/*
 *  Copyright (c) 2001 James Simmons 
 */

/*
 * CerPDA input keyboard Driver for Linux
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
 * e-mail - mail your message to <jsimmons@users.sf.net>
 */

#include <asm/bitops.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/input.h>

MODULE_AUTHOR("James Simmons <jsimmons@users.sf.net>");
MODULE_DESCRIPTION("CerfPDA keyboard input driver");
MODULE_LICENSE("GPL");

/*			Keyboard Layout
	F1    F2    1/A       2/B             3/C     4/D     Pwr 
	5/E   6/F   7/G       8/H             9/I     0/J     @/K
	+/L   -/M   ^8/N      / O             =/P     up/Q    "/R     
	Esc/S ,/T   ./U       ?/V             left/W  :/X     right/Y
	Tab/Z Shift Caps/Ctrl Backspace/Space Num/Cur down/\  Enter

	The way this device works is that we need two keymaps. If the
	user presses Num/Cur we switch which set of events we get. 
*/

#define KBD_REPORT_UNKN

#define KBD_REPORT_ERR			/* Report keyboard errors */
#define KBD_REPORT_UNKN                 /* Report unknown scan codes */
#define KBD_REPORT_TIMEOUTS		/* Report keyboard timeouts */
#define KBD_NO_DATA         (-1)        /* No data */
#define KBD_REPEAT_START    (0x20)
#define KBD_REPEAT_CONTINUE (0x05)
#define KBD_KEY_DOWN_MAX    (0x10)
#define UINT_LEN            (20)
#define SC_LIM              (69)
#define KBD_ROWS            (5)
#define KBD_COLUMNS         (8)

#define KBD_UP_OFF          (0)
#define KBD_UP_ON           (1)
#define KBD_DOWN            (2)
#define KBD_DOWN_HOLD       (3)

unsigned keynum[KBD_ROWS*KBD_COLUMNS];
unsigned keycur[KBD_ROWS*KBD_COLUMNS];

struct cerfPDA {
	struct timer_list kbd_timer;
	struct input_dev kbdev;
	spinlock_t kbd_controller_lock = SPIN_LOCK_UNLOCKED;
	unsigned char keynum[KBD_ROWS*KBD_COLUMNS];
	unsigned char keycur[KBD_ROWS*KBD_COLUMNS];
	short which_keymap = 0;
};

static void column_set(unsigned int column)
{
	if (column < 0) {
		CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_A, 0xFF, 0xFF);
		CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_B, 0xFF, 0xFF);
   	} else {
      		if (column < 4) {
			CERF_PDA_CPLD_Set(CERF_PDA_CPLD_KEYPAD_A, 1 << (column % 4), 0xFF);
			CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_B, 0xFF, 0xFF);
      		} else { 
         		CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_A, 0xFF, 0xFF);
         		CERF_PDA_CPLD_Set(CERF_PDA_CPLD_KEYPAD_B, 1 << (column % 4), 0xFF);
      		}
   	}
}

static int kbd_read_input(void)
{
	int value, i, j;

	for(i = 0; i < KBD_COLUMNS; i++) {
		column_set(i);
		udelay(50);
		for(j = 0; j < KBD_ROWS; j++)  {
			if (pda->which_keymap)
				value = (GPLR & (1 << (20 + j)));
		}
   	}
   	column_set(-1);
   	return value;
}

/* Handle the automatic interrupts handled by the timer */
static void cerfkbd_timer(unsigned long private)
{
	struct cerfPDA *pda = (void *) private;
		
   	spin_lock_irq(&pda->kbd_controller_lock);
   	scancode = kbd_read_input();
   	spin_unlock_irq(&pda->kbd_controller_lock);
   	mod_timer(&pda->kbd_timer, jiffies + 8);
}

static int cerfkbd_open(struct input_dev *dev)
{
	init_timer(&cerfPDA->timer);
        cerfPDA->timer.data = (long) cerfPDA;
	cerfPDA->timer.expires = jiffies + 50;	
	cerfPDA->timer.function = cerkbd_timer;		
}

static void cerfkbd_close(struct input_dev *dev)
{
	del_timer(&kbd_timer);
}

static struct input_dev logibm_dev = {
	evbit:		{ BIT(EV_KEY) | BIT(EV_REL) },
	keybit:		{ [LONG(BTN_LEFT)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | B
	relbit:		{ BIT(REL_X) | BIT(REL_Y) },
	open:		cerfkbd_open,
	close:		cerfkbd_close,
	name:		"Cerf PDA keyboard",
	phys:		"CerfPDA/input0",
	idbus:		BUS_ISA,
	idvendor:	0x0003,
	idproduct:	0x0001,
	idversion:	0x0100,
};

static int __init cerfkbd_init(void)
{
	printk("Starting Cerf PDA Keyboard Driver... ");

	GPDR &= ~(GPIO_GPIO(20) | GPIO_GPIO(21) | GPIO_GPIO(22) | GPIO_GPIO(23) | GPIO_GPIO(24));
	
	input_register_device(&cerfkbd_dev);
	printk(KERN_INFO "input: Cerf PDA keyboard\n");
}

static void __exit cerfkbd_int(void)
{
	input_unregister_device(&cerfkbd_dev);
}

module_init(cerfkbd_init);
module_exit(cerfkbd_exit);
