/*
 *  joy-bttv.c  Version 1.0
 *
 *  Copyright (c) 2000 Ryan Gammon <rggammon@uwaterloo.ca>
 *
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * the tv remote control hooked up to tv cards based on the bt848 chip.
 *
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
 */

/*
  I've received the following documentation from Avermedia. (It's not 100% 
  correct for bt848-based cards. There have been reports that it works with
  later bt878 boards)

  The following is the define of our remote function for the GPIO pins:

  Remote code(8 bits):
        GPIO (MSB)23 22 21 20 19 17 16 15(LSB)
  Handshaking:(3 bits):
        GPIO 14:        0       Get the code of remote
                        1       Read the code of remote
        GPIO 23:        0       The code of remote not ready
                        1       The code of remote is ready
        GPIO 22:        0
                        1       Repeat

  Algorithm:
       If (bit 23 is 1)
               Set (bit 14 to 1)
               Get remote code(8 bits)
               Set (bit 14 to 0)
       Endif

*/

#include <asm/io.h>
#include <asm/param.h>
#include <asm/system.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/joystick.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/init.h>

#if 0
#include "../bttv.h"
#else
int bttv_read_gpio(unsigned int card, unsigned long *data);
int bttv_write_gpio(unsigned int card, unsigned long mask, unsigned long data);
#endif

static struct js_port* js_bttv_port __initdata = NULL;

#define MODEL_AVERMEDIA_BT848 0 /* Old TV Phone/TV98 (pre 1998-ish) */
#define MODEL_AVERMEDIA_BT878 1 /* Newer avermedia cards            */

MODULE_AUTHOR("Ryan Gammon <rggammon@uwaterloo.ca>");
MODULE_DESCRIPTION("joy-bttv - joystick driver for tv remote controls hooked up via bttv chips");
MODULE_PARM(card, "1-4i");
MODULE_PARM(model, "0-1i");

static unsigned int card = 1;
static unsigned int model = 0;

/*
 * js_avermedia_bt848_read_code() reads remote control data.
 */

static int js_avermedia_bt848_read_code(int *button, int *repeat)
{
    unsigned long gpio;
    int result;
    
    result = bttv_read_gpio(card - 1, &gpio);
    if(result < 0)
        return result;

    *button = (int)(((gpio & 0x00F00000) >> 20) + ((gpio & 0x00080000) >> 15));
    *repeat =        (gpio & 0x00010000)? 1: 0;

    return 0;
}

/*
 * js_avermedia_bt878_read_code() reads remote control data.
 */

static int js_avermedia_bt878_read_code(int *button, int *repeat)
{
    unsigned long gpio;
    int result;
    
    result = bttv_read_gpio(card - 1, &gpio);
    if(result < 0)
        return result;

    if(!(gpio & 0x00800000)) /* remote is not ready */
        return -1;

    gpio |= 0x00008000; /* bit 15 (14?) is flow control */
    result = bttv_write_gpio(card - 1, 0x00008000, gpio);
    if(result < 0)
        return result;
    
    bttv_read_gpio(card - 1, &gpio);
    
    *button = (int)((gpio & 0x003F0000) >> 16);  
    *repeat =       (gpio & 0x00400000)? 1: 0;
    
    gpio &= ~0x00008000; 
    bttv_write_gpio(card - 1, 0x00008000, gpio);
    if(result < 0)
        return result;
    
    return 0;
}

/*
 * js_bttv_read() reads remote control data.
 */

static int js_bttv_read(void *unused, int **axes, int **buttons)
{
    unsigned int b, r;
    int result = -1;
    
    switch(model)
    {
    case MODEL_AVERMEDIA_BT848:
        result = js_avermedia_bt848_read_code(&b, &r);
        break;
    case MODEL_AVERMEDIA_BT878:
        result = js_avermedia_bt878_read_code(&b, &r);
        break;
    default:
        return -1;
    }

    if((result < 0) || (b >= 32))
        return -1;
    
    if(r)
        buttons[0][0] |= 1 << b;
    else
        buttons[0][0] &= ~(1 << b);

	return 0;
}

/*
 * js_bttv_open() is a callback from the file open routine.
 */

static int js_bttv_open(struct js_dev *jd)
{
	MOD_INC_USE_COUNT;
    return 0;
}

/*
 * js_bttv_close() is a callback from the file release routine.
 */

static int js_bttv_close(struct js_dev *jd)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
int init_module(void)
#else
    int __init js_bttv_init(void)
#endif
{
    int dev;
    char *names[] = { "Avermedia bt848 remote", "Avermedia bt878 remote" };
  
    js_bttv_port = js_register_port(js_bttv_port,
                                    NULL,          /* no private data */
                                    1,             /* 1 remote/card   */
                                    0,             /* sizeof(info)    */
                                    js_bttv_read);

    if(!js_bttv_port)
    {
        printk(KERN_INFO "js_bttv_init: Error registering port\n");
        return -ENODEV;
    }

    dev = js_register_device(js_bttv_port,
                             0,                        /* remote 0   */
                             0,                        /* 0 axes     */
                             32,                       /* 32 buttons */
                             names[model],             /* name       */
                             js_bttv_open,             /* open       */
                             js_bttv_close);           /* close      */

    if(dev < 0)
    {
        printk(KERN_INFO "js_bttv_init: Error registering device\n");
        return -ENODEV;
    }

    printk(KERN_INFO "js%d: Avermedia bt848 remote\n", dev);
    
	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
    js_unregister_device(js_bttv_port->devs[0]);
    js_bttv_port = js_unregister_port(js_bttv_port);
}
#endif
