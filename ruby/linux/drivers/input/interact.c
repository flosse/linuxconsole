/*
 *  joy-hammerheadfx.c  Version 1.0
 *
 *  Copyright (c) 2000 Toby Deshane
 *  Copyright (c) 1996-1999 Vojtech Pavlik
 *
 *  Basic source skeleton borrowed from the Amiga joystick driver.
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux joystick driver, supporting
 * the Hammerhead/FX controller from InterAct.
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

static struct js_port* js_hh_port __initdata = NULL;

MODULE_AUTHOR("Toby Deshane <hac@digivill.net>");
//MODULE_PARM(js_hh, "1-2i");

#define NUM_CLOCKS 32
#define JS_PORT 0x201

typedef struct
{
   unsigned char unk1:8;
   unsigned char b7  :1;
   unsigned char b6  :1;
   unsigned char b5  :1;
   unsigned char b4  :1;
   unsigned char b3  :1;
   unsigned char b2  :1;
   unsigned char b1  :1;
   unsigned char b8  :1;
   unsigned char x;
   unsigned char x2;
} _data1;

typedef union
{
  _data1 bits;
  unsigned long value;
} data1;

typedef struct
{
  unsigned char unk1    :8; //7
  unsigned char profile :1; //8
  unsigned char rumble  :1; //9
  unsigned char b10     :1; //A
  unsigned char b9      :1; //B
  unsigned char dright  :1; //C
  unsigned char dleft   :1; //D
  unsigned char ddown   :1; //E
  unsigned char dup     :1; //F
  unsigned char y;
  unsigned char y2;
} _data2;

typedef union
{
  _data2 bits;
  unsigned long value; // unsigned long should be 4 bytes
} data2;

//static int __initdata js_hh[] = { 0, 0 };
struct
{
  unsigned char id1, id2;
  unsigned char button[10];
  unsigned char pad_up, pad_down, pad_left, pad_right;
  unsigned char rumble, profile;
  unsigned char x,  y;
  unsigned char x2, y2;
} js_status;


/*
 * revbits() reverses the order of bits in a byte
 */

inline unsigned char revbits(unsigned char b)
{
  int x;
  unsigned char a = 0;

  for (x = 7; x >= 0; x--)
     {
       a |= b & (1 << x) ? (1<< (7-x)):0;
     }
  return a;
}

/*
 * js_hh_read() reads and Hammerhead/FX joystick data.
 */

static int js_hh_read(void *info, int **axes, int **buttons)
{
  unsigned long flags;
  unsigned char c;         // used for clock tick counting, and gen purpose
  unsigned char u;
  unsigned char data[32];
  data1 d1;
  data2 d2;

  d1.value = d2.value = 0;
  c = 0;                   // reset clock tick count

  // read data stream
  __save_flags(flags);
  __cli();                 // clear interrupts

  outb(0xFF,JS_PORT);      // strobe port

  while(c < NUM_CLOCKS)    // hammerhead sends two 32 bit streams on bits 4 and 5
    {
      do { u = inb(JS_PORT);} while(!(u & (1<<6))); // wait for 0 -> 1
      data[c] = u;                                  // clock == 1, grab bits
      do { u = inb(JS_PORT);} while(u & (1<<6));    // wait for 1 -> 0
      c++;                                          // next!
    }

  __restore_flags(flags);


  // decode data stream
  for (c = 0; c < 32; c++)
     {
       d1.value |= data[c] & (1<<4)? (1<<c): 0;
       d2.value |= data[c] & (1<<5)? (1<<c): 0;
     }

  // break it down
  //js_status.id1  = d1.bits.unk1;
  //js_status.id2  = d2.bits.unk1;

  // Digital pad
  axes[0][0] = (d2.bits.dright ? 1 : 0) - (d2.bits.dleft ? 1 : 0);
  axes[0][1] = (d2.bits.ddown  ? 1 : 0) - (d2.bits.dup   ? 1 : 0);

  // Stick 1
  axes[0][2] = revbits(d1.bits.x);;
  axes[0][3] = revbits(d2.bits.y);

  // Stick 2
  axes[0][4] = revbits(d1.bits.x2);
  axes[0][5] = revbits(d2.bits.y2);

  buttons[0][0] = (d1.bits.b1 ? 0x001 : 0)|
                  (d1.bits.b2 ? 0x002 : 0)|
                  (d1.bits.b3 ? 0x004 : 0)|
                  (d1.bits.b4 ? 0x008 : 0)|
                  (d1.bits.b5 ? 0x010 : 0)|
                  (d1.bits.b6 ? 0x020 : 0)|
                  (d1.bits.b7 ? 0x040 : 0)|
                  (d1.bits.b8 ? 0x080 : 0)|
                  (d2.bits.b9 ? 0x100 : 0)|
                  (d2.bits.b10? 0x200 : 0)|
                  (d2.bits.rumble  ? 0x400 : 0)|
                  (d2.bits.profile ? 0x800 : 0);

  return 0;
}

/*
 * js_hh_open() is a callback from the file open routine.
 */

static int js_hh_open(struct js_dev *jd)
{
  MOD_INC_USE_COUNT;
  return 0;
}

/*
 * js_hh_close() is a callback from the file release routine.
 */

static int js_hh_close(struct js_dev *jd)
{
  MOD_DEC_USE_COUNT;
  return 0;
}

/*
 * js_hh_init_corr() initializes correction values of
 * Hammerhead/FX joysticks.
 */

static void __init js_hh_init_corr(struct js_corr **corr)
{
  int i;

  for (i = 0; i < 2; i++)
     {
       corr[0][i].type = JS_CORR_BROKEN;
       corr[0][i].prec = 0;
       corr[0][i].coef[0] = 0;
       corr[0][i].coef[1] = 0;
       corr[0][i].coef[2] = (1 << 29);
       corr[0][i].coef[3] = (1 << 29);
     }

  for (i = 2; i < 6; i++)
     {
       corr[0][i].type = JS_CORR_BROKEN;
       corr[0][i].prec = 4;
       corr[0][i].coef[0] = 255 - 16;
       corr[0][i].coef[1] = 256 + 16;
       corr[0][i].coef[2] = (1 << 29) / (255 - 16);
       corr[0][i].coef[3] = (1 << 29) / (255 - 16);
     }
}

#ifndef MODULE
/*
int __init js_hh_setup(SETUP_PARAM)
{
  int i;
  SETUP_PARSE(2);
  for (i = 0; i <= ints[0] && i < 2; i++) js_hh[i] = ints[i+1];
  return 1;
}
__setup("js_hh=", js_hh_setup);
*/
#endif

#ifdef MODULE
int init_module(void)
#else
int __init js_hh_init(void)
#endif
{
  int i;

  js_hh_port = js_register_port(js_hh_port,
                                &i,
                                1,
                                sizeof(int),
                                js_hh_read);

  if (check_region(JS_PORT, 1)) return -ENODEV;
  request_region(JS_PORT, 1, "joystick (Hammerhead/FX)");

  printk(KERN_INFO "js%d: Hammerhead/FX gamepad driver loaded\n",
                   js_register_device(js_hh_port, // port
                                      0,          // number
                                      6,          // axes
                                      12,         // buttons
                                      "Hammerhead/FX", // name
                                      js_hh_open,
                                      js_hh_close));
  js_hh_init_corr(js_hh_port->corr);
  if (js_hh_port) return 0;

#ifdef MODULE
  printk(KERN_WARNING "joy-hammerheadfx: drrrrrrrr...\n");
#endif
  return -ENODEV;
}

#ifdef MODULE
void cleanup_module(void)
{
  while (js_hh_port)
    {
      if (js_hh_port->devs[0])
        {
          js_unregister_device(js_hh_port->devs[0]);
        }
      release_region(JS_PORT, 1);
      js_hh_port = js_unregister_port(js_hh_port);
    }
  printk(KERN_INFO "Hammerhead/FX driver removed.\n");
}
#endif
