/*
 * $Id$
 *
 *  Copyright (c) 2001 James Simmons <jsimmons@users.sf.net> 
 *
 *  Based on the work of:
 *  		Peter De Schrijver <p2@mind.be>
 */

/*
 * Apollo input keyboard driver for Linux/m68k
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
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <jsimmons@user.sf.net>.
 */

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/keyboard.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/apollohw.h>
#include <asm/uaccess.h>

#define DNKEY_CAPS 0x7e
#define BREAK_FLAG 0x80
#define DNKEY_REPEAT_DELAY 50
#define DNKEY_CTRL 0x43
#define DNKEY_LSHIFT 0x5e
#define DNKEY_RSHIFT 0x6a
#define DNKEY_REPT 0x5d
#define DNKEY_REPEAT 0x7f
#define DNKEY_LALT 0x75
#define DNKEY_RALT 0x77

#define APOLLO_KEYB_CMD_ENTRIES 16
#define APOLLO_KBD_MODE_KEYB   0x01
#define APOLLO_KBD_MODE_MOUSE   0x02
#define APOLLO_KBD_MODE_CHANGE 0xff

static u_char keyb_cmds[APOLLO_KEYB_CMD_ENTRIES];
static short keyb_cmd_read=0, keyb_cmd_write=0;
static int keyb_cmd_transmit=0;
static int msedev;

static unsigned int kbd_mode=APOLLO_KBD_MODE_KEYB;

static void dn_keyb_process_key_event(unsigned char scancode) {

	static unsigned char lastscancode;
	unsigned char prev_scancode=lastscancode;
	static unsigned int lastkeypress;
	
	lastscancode=scancode;

	/*  printk("scan: %02x, lastscan: %02X, prev_scancode: %02X\n",scancode,lastscancode,prev_scancode); */

	if(prev_scancode==APOLLO_KBD_MODE_CHANGE) {
		kbd_mode=scancode;
/*		printk("modechange: %d\n",scancode); */
	}
	else if((scancode & (~BREAK_FLAG)) == DNKEY_CAPS) {
    	/* printk("handle_scancode: %02x\n",DNKEY_CAPS); */
		handle_scancode(DNKEY_CAPS, 1);
		/*    printk("handle_scancode: %02x\n",BREAK_FLAG | DNKEY_CAPS); */
		handle_scancode(DNKEY_CAPS, 0);
	}
	else if( (scancode == DNKEY_REPEAT) && (prev_scancode < 0x7e) &&
   			!(prev_scancode==DNKEY_CTRL || prev_scancode==DNKEY_LSHIFT ||
       	   	prev_scancode==DNKEY_RSHIFT || prev_scancode==DNKEY_REPT ||
       	  	prev_scancode==DNKEY_LALT || prev_scancode==DNKEY_RALT)) {
			if (time_after(jiffies, lastkeypress + DNKEY_REPEAT_DELAY)) {
			/*    	printk("handle_scancode: %02x\n",prev_scancode); */
           			handle_scancode(prev_scancode, 1);
			  	}
	   			lastscancode=prev_scancode;
  			}
  	else {
	/*    	printk("handle_scancode: %02x\n",scancode);  */
   			handle_scancode(scancode & ~BREAK_FLAG, !(scancode & BREAK_FLAG));
   			lastkeypress=jiffies;
  	}
}

static void dn_keyb_process_mouse_event(unsigned char mouse_data) {

	static short mouse_byte_count=0;
	static u_char mouse_packet[3];
	short mouse_buttons;	

	mouse_packet[mouse_byte_count++]=mouse_data;

	if(mouse_byte_count==3) {
		if(mouse_packet[0]==APOLLO_KBD_MODE_CHANGE) {
			kbd_mode=mouse_packet[1];
			mouse_byte_count=0;
/*			printk("modechange: %d\n",mouse_packet[1]); */
			if(kbd_mode==APOLLO_KBD_MODE_KEYB)
				dn_keyb_process_key_event(mouse_packet[2]);
		}				
		if((mouse_packet[0] & 0x8f) == 0x80) {
			if(mouse_update_allowed) {
				mouse_ready=1;
				mouse_buttons=(mouse_packet[0] >> 4) & 0x7;
				mouse_dx+=mouse_packet[1] == 0xff ? 0 : (signed char)mouse_packet[1];
				mouse_dy+=mouse_packet[2] == 0xff ? 0 : (signed char)mouse_packet[2];
				wake_up_interruptible(&mouse_wait);
				if (mouse_dx < -2048)
              		mouse_dx = -2048;
          		else
          		if (mouse_dx >  2048)
              		mouse_dx =  2048;
          		if (mouse_dy < -2048)
              		mouse_dy = -2048;
          		else
          		if (mouse_dy >  2048)
              		mouse_dy =  2048;
				if (mouse_fasyncptr)
              		kill_fasync(mouse_fasyncptr, SIGIO, POLL_IN);
			}
			mouse_byte_count=0;
/*			printk("mouse: %d, %d, %x\n",mouse_x,mouse_y,buttons); */
		}
	}
}

static void apollokbd_interrupt(int irq, void *dummy, struct pt_regs *fp) 
{
	unsigned char data;
  	unsigned long flags;
  	int scn2681_ints;

	do {
		scn2681_ints=sio01.isr_imr & 3;
		if(scn2681_ints & 2) {
			data=sio01.rhra_thra;
#if 0
			if(debug_buf_count<4096) {
				debug_buf[debug_buf_count++]=data;
				debug_buffer_updated=jiffies;	
				if(!debug_timer_running) {
					debug_keyb_timer.expires=jiffies+10;
					add_timer(&debug_keyb_timer);
					debug_timer_running=1;
				}
			}
			else
				debug_buf_overrun=1;
#endif
			if(sio01.sra_csra & 0x10) {
				printk("whaa overrun !\n");
				continue;
			}

			if (kbd_mode==APOLLO_KBD_MODE_KEYB)
				dn_keyb_process_key_event(data);
			else
				dn_keyb_process_mouse_event(data);
		}
	
		if(scn2681_ints & 1) {
			save_flags(flags);
			cli();
			if(keyb_cmd_write!=keyb_cmd_read) {
				sio01.rhra_thra=keyb_cmds[keyb_cmd_read++];
				if(keyb_cmd_read==APOLLO_KEYB_CMD_ENTRIES)
					keyb_cmd_read=0;
				keyb_cmd_transmit=1;
			}
			else {
				keyb_cmd_transmit=0;
				sio01.BRGtest_cra=9;
			}
			restore_flags(flags);
		}
	} while(scn2681_ints) ;
}

void write_keyb_cmd(u_short length, u_char *cmd) {

  	unsigned long flags;

	if((keyb_cmd_write==keyb_cmd_read) && keyb_cmd_transmit)
		return;

	save_flags(flags);
	cli();
	for(;length;length--) {
		keyb_cmds[keyb_cmd_write++]=*(cmd++);
		if(keyb_cmd_write==keyb_cmd_read)
			return;
		if(keyb_cmd_write==APOLLO_KEYB_CMD_ENTRIES)
			keyb_cmd_write=0;
	}
	if(!keyb_cmd_transmit)  {
 	   sio01.BRGtest_cra=5;
	}
	restore_flags(flags);

}

static int release_mouse(struct inode * inode, struct file * file)
{
        MOD_DEC_USE_COUNT;
        return 0;
}

static int open_mouse(struct inode * inode, struct file * file)
{
        MOD_INC_USE_COUNT;
        return 0;
}

static struct busmouse apollo_mouse = {
        APOLLO_MOUSE_MINOR, "apollomouse", open_mouse, release_mouse,7
};

static int __init apollokbd_init(void)
{
        int i;

        apollokbd_dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REP);
        apollokbd_dev.keycode = apollokbd_keycode;

        for (i = 0; i < 0x78; i++)
                if (apollokbd_keycode[i])
                        set_bit(apollokbd_keycode[i], apollokbd_dev.keybit);

	/* program UpDownMode */
	while (!(sio01.sra_csra & 0x4));
	sio01.rhra_thra=0xff;

	while (!(sio01.sra_csra & 0x4));
	sio01.rhra_thra=0x1;

        request_irq(1, apollokbd_interrupt, 0, "apollokbd", NULL);

	/* enable receive int on DUART */
	sio01.isr_imr=3;

        apollokbd_dev.name = apollokbd_name;
        apollokbd_dev.phys = apollokbd_phys;
        apollokbd_dev.idbus = BUS_AMIGA;
        apollokbd_dev.idvendor = 0x0001;
        apollokbd_dev.idproduct = 0x0001;
        apollokbd_dev.idversion = 0x0100;

        input_register_device(&apollokbd_dev);

        printk(KERN_INFO "input: %s\n", apollokbd_name);

        return 0;
}

static void __exit apollokbd_exit(void)
{
        input_unregister_device(&apollokbd_dev);
        free_irq(1, apollokbd_interrupt);
}

module_init(apollokbd_init);
module_exit(apollokbd_exit);

