/*
   cerf_keyb.c: This is the end. Daniel is writing a device driver!!!
*/
#include <linux/config.h>

#include <linux/spinlock.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/mm.h>
#include <linux/signal.h>
#include <linux/init.h>
#include <linux/kbd_ll.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/kbd_kern.h>
#include <linux/smp_lock.h>
#include <linux/timer.h>

#include <asm/keyboard.h>
#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/irq.h>
#include <asm/system.h>

#include <asm/io.h>

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

#define KBD_KEYUP           (0x80)
#define KBD_MODESCAN        (0x7f)
#define KBD_CAPSSCAN        (0x3a)
#define KBD_SHIFTSCAN       (0x2a)
#define KBD_NUMCURSCAN      (0x7c)
#define KBD_CTRLSCAN        (0x1d)
#define KBD_ALTSCAN         (0x38)

#define KBD_UP_OFF          (0)
#define KBD_UP_ON           (1)
#define KBD_DOWN            (2)
#define KBD_DOWN_HOLD       (3)



static unsigned char handle_kbd_event(void);
static unsigned char kbd_read_input(void);
static void column_set(unsigned int column);
static int scancodes(unsigned char codeval[KBD_ROWS][KBD_COLUMNS]);

static spinlock_t kbd_controller_lock = SPIN_LOCK_UNLOCKED;
static struct timer_list kbd_timer;

static short mode_ena = 0;
static short numcur_ena = 0;
static short shift_ena = 0;

#define E0_KPENTER 96
#define E0_RCTRL   97
#define E0_KPSLASH 98
#define E0_PRSCR   99
#define E0_RALT    100
#define E0_BREAK   101  /* (control-pause) */
#define E0_HOME    102
#define E0_UP      103
#define E0_PGUP    104
#define E0_LEFT    105
#define E0_RIGHT   106
#define E0_END     107
#define E0_DOWN    108
#define E0_PGDN    109
#define E0_INS     110
#define E0_DEL     111
#define E1_PAUSE   119
#define E0_MACRO   112
#define E0_F13     113
#define E0_F14     114
#define E0_HELP    115
#define E0_DO      116
#define E0_F17     117
#define E0_KPMINPLUS 118
#define E0_OK      124
#define E0_MSLW    125
#define E0_MSRW    126
#define E0_MSTM    127

static unsigned char e0_keys[128] = {
  0, 0, 0, 0, 0, 0, 0, 0,                             /* 0x00-0x07 */
  0, 0, 0, 0, 0, 0, 0, 0,                             /* 0x08-0x0f */
  0, 0, 0, 0, 0, 0, 0, 0,                             /* 0x10-0x17 */
  0, 0, 0, 0, E0_KPENTER, E0_RCTRL, 0, 0,             /* 0x18-0x1f */
  0, 0, 0, 0, 0, 0, 0, 0,                       	/* 0x20-0x27 */
  0, 0, 0, 0, 0, 0, 0, 0,                             /* 0x28-0x2f */
  0, 0, 0, 0, 0, E0_KPSLASH, 0, E0_PRSCR,             /* 0x30-0x37 */
  E0_RALT, 0, 0, 0, 0, E0_F13, E0_F14, E0_HELP,       /* 0x38-0x3f */
  E0_DO, E0_F17, 0, 0, 0, 0, E0_BREAK, E0_HOME,       /* 0x40-0x47 */
  E0_UP, E0_PGUP, 0, E0_LEFT, E0_OK, E0_RIGHT, E0_KPMINPLUS, E0_END,/* 0x48-0x4f */
  E0_DOWN, E0_PGDN, E0_INS, E0_DEL, 0, 0, 0, 0,       /* 0x50-0x57 */
  0, 0, 0, E0_MSLW, E0_MSRW, E0_MSTM, 0, 0,           /* 0x58-0x5f */
  0, 0, 0, 0, 0, 0, 0, 0,                             /* 0x60-0x67 */
  0, 0, 0, 0, 0, 0, 0, E0_MACRO,                      /* 0x68-0x6f */
  0, 0, 0, 0, 0, 0, 0, 0,                             /* 0x70-0x77 */
  0, 0, 0, 0, 0, 0, 0, 0                              /* 0x78-0x7f */
};

static unsigned char cerf_normal_map[KBD_ROWS][KBD_COLUMNS] = {
   {KBD_ALTSCAN, KBD_MODESCAN, 0x1e, 0x30, 0x2e, 0x20, 0x00, 0x00},
   {0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x00},
   {0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x00},
   {0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x00},
   {0x2c, KBD_SHIFTSCAN, KBD_CTRLSCAN, 0x39, KBD_NUMCURSCAN, 0x2b, 0x1c, 0x00}
};

static unsigned char cerf_mode_map[KBD_ROWS][KBD_COLUMNS] = {
   {0x00, 0x00, 0x02, 0x03, 0x04, 0x05, 0x00, 0x00},
   {0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x00, 0x00}, //
   {0x0d, 0x0c, 0x37, 0x35, 0x0d, 0x48, 0x28, 0x00},
   {0x01, 0x33, 0x34, 0x00, 0x4b, 0x27, 0x4d, 0x00}, //
   {0x0f, 0x00, KBD_CAPSSCAN, 0x0e, 0x00, 0x50, 0x00, 0x00}
};

static unsigned char cerf_numcur_map[KBD_ROWS][KBD_COLUMNS] = {
   {0x00, 0x00, 0x02, 0x03, 0x04, 0x05, 0x00, 0x00},
   {0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x00, 0x00},
   {0x0d, 0x0c, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00},
   {0x00, 0x00, 0x00, 0x00, 0x4b, 0x00, 0x4d, 0x00},
   {0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00}
};

static void column_set(unsigned int column)
{
   if (column < 0)
   {
      CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_A, 0xFF, 0xFF);
      CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_B, 0xFF, 0xFF);
   }
   else
   {
      if(column < 4)
      {
         CERF_PDA_CPLD_Set(CERF_PDA_CPLD_KEYPAD_A, 1 << (column % 4), 0xFF);
         CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_B, 0xFF, 0xFF);
      }
      else
      {
         CERF_PDA_CPLD_UnSet(CERF_PDA_CPLD_KEYPAD_A, 0xFF, 0xFF);
         CERF_PDA_CPLD_Set(CERF_PDA_CPLD_KEYPAD_B, 1 << (column % 4), 0xFF);
      }
   }
}

static int scancodes(unsigned char codeval[KBD_ROWS][KBD_COLUMNS])
{
   int i, j;

   for(i = 0; i < KBD_COLUMNS; i++)
   {
      column_set(i);
      udelay(50);
      for(j = 0; j < KBD_ROWS; j++)
      {
         if(mode_ena)
            codeval[j][i] = (GPLR & (1 << (20 + j)))?(cerf_mode_map[j][i]?cerf_mode_map[j][i]:cerf_normal_map[j][i]):0;
         else if(numcur_ena)
            codeval[j][i] = (GPLR & (1 << (20 + j)))?(cerf_numcur_map[j][i]?cerf_numcur_map[j][i]:cerf_normal_map[j][i]):0;
         else
            codeval[j][i] = (GPLR & (1 << (20 + j)))?cerf_normal_map[j][i]:0;
      }
   }
   column_set(-1);

   return 0;
}

static unsigned char kbd_read_input(void)
{
   int i, j, k, l;
   unsigned char prev;
   static unsigned char count = 0;

   static unsigned char oldcodes[KBD_ROWS][KBD_COLUMNS]={{0,0,0,0,0,0,0,0},
                                                         {0,0,0,0,0,0,0,0},
                                                         {0,0,0,0,0,0,0,0},
                                                         {0,0,0,0,0,0,0,0},
                                                         {0,0,0,0,0,0,0,0}};
   unsigned char inputcode[KBD_ROWS][KBD_COLUMNS];

   memset(inputcode, 0, sizeof(unsigned char) * (KBD_ROWS * KBD_COLUMNS));
   scancodes(inputcode);

   for(i = 0; i < KBD_COLUMNS; i++)
   {
      for(j = 0; j < KBD_ROWS; j++)
      {
//         if(oldcodes[j][i] == 0xe0)
//            oldcodes[j][i] =
         if(oldcodes[j][i] != inputcode[j][i])
         {
            // Value of the key before entering this function
            prev = oldcodes[j][i];

            // KEYUP
            if(inputcode[j][i] == 0 && oldcodes[j][i] != 0 && !(oldcodes[j][i] & KBD_KEYUP))
            {
               oldcodes[j][i] |= KBD_KEYUP;

               if(mode_ena == KBD_UP_ON)
                  mode_ena = KBD_UP_OFF;
               if(prev == KBD_MODESCAN)
                  if(mode_ena == KBD_DOWN_HOLD)
                     mode_ena = KBD_UP_OFF;
                  else if(mode_ena == KBD_DOWN)
                     mode_ena = KBD_UP_ON;
               if(mode_ena == KBD_DOWN)
                  mode_ena = KBD_DOWN_HOLD;
            }
            // RESET KEYUP
            else if(oldcodes[j][i] & KBD_KEYUP)
               oldcodes[j][i] = 0;
            // KEY DOWN
            else
            {
               oldcodes[j][i] = inputcode[j][i];

               // Parse out mode modifiers before the keyboard interpreter can touch them
  	       if(inputcode[j][i] == KBD_MODESCAN)
               {
                  if(!mode_ena)
                     mode_ena = KBD_DOWN;
                  continue;
               }
               if(inputcode[j][i] == KBD_NUMCURSCAN)
               {
                  numcur_ena = numcur_ena?0:1;
                  continue;
               }
            }
            //printk("Modified: (%#x,%#x), ipv:%#x, To: (%#.2x), From: (%#.2x), Flags:%d,%d,%d\r\n", j, i, inputcode[j][i], oldcodes[j][i], prev, mode_ena, shift_ena, numcur_ena);
            return oldcodes[j][i];
         }
      }
   }

   return (unsigned char)(KBD_NO_DATA);
}

int cerf_kbd_translate(unsigned char scancode, unsigned char *keycode,
		    char raw_mode)
{
	static int prev_scancode;

	if (scancode == 0xe0 || scancode == 0xe1) {
		prev_scancode = scancode;
		return 0;
	}

	if (scancode == 0x00 || scancode == 0xff) {
		prev_scancode = 0;
		return 0;
	}

	scancode &= 0x7f;

	if (prev_scancode) {
	  if (prev_scancode != 0xe0) {
	      if (prev_scancode == 0xe1 && scancode == 0x1d) {
		  prev_scancode = 0x100;
		  return 0;
	      } else if (prev_scancode == 0x100 && scancode == 0x45) {
		  prev_scancode = 0;
	      } else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk(KERN_INFO "keyboard: unknown e1 escape sequence\n");
#endif
		  prev_scancode = 0;
		  return 0;
	      }
	  } else {
	      prev_scancode = 0;
	      if (scancode == 0x2a || scancode == 0x36)
		return 0;
	      else {
#ifdef KBD_REPORT_UNKN
		  if (!raw_mode)
		    printk(KERN_INFO "keyboard: unknown scancode e0 %02x\n",
			   scancode);
#endif
		  return 0;
	      }
	  }
 	} else
	  *keycode = scancode;
 	return 1;
}

static inline void handle_keyboard_event(unsigned char scancode)
{
   if(scancode != (unsigned char)(KBD_NO_DATA))
   {
#ifdef CONFIG_VT
      handle_scancode(scancode, !(scancode & KBD_KEYUP));
#endif
      tasklet_schedule(&keyboard_tasklet);
   }
}

static unsigned char handle_kbd_event(void)
{
   unsigned char scancode;

   scancode = kbd_read_input();
   handle_keyboard_event(scancode);

   return 0;
}

/* Handle the automatic interrupts handled by the timer */
static void keyboard_interrupt(unsigned long foo)
{
   spin_lock_irq(&kbd_controller_lock);
   handle_kbd_event();
   spin_unlock_irq(&kbd_controller_lock);

   kbd_timer.expires = 8 + jiffies;
   kbd_timer.data = 0x00000000;
   kbd_timer.function = (void(*)(unsigned long))&keyboard_interrupt;

   add_timer(&kbd_timer);
}

void cerf_leds(unsigned char leds)
{
}
char cerf_unexpected_up(unsigned char keycode)
{
return 0;
}
int cerf_getkeycode(unsigned int scancode)
{
return 0;
}
int cerf_setkeycode(unsigned int scancode, unsigned int keycode)
{
return 0;
}

void cerf_kbd_init_hw(void)
{
   printk("Starting Cerf PDA Keyboard Driver... ");

   k_setkeycode	= cerf_setkeycode;
   k_getkeycode    = cerf_getkeycode;
   k_translate     = cerf_kbd_translate;
   k_unexpected_up = cerf_unexpected_up;
   k_leds          = cerf_leds;

   GPDR &= ~(GPIO_GPIO(20) | GPIO_GPIO(21) | GPIO_GPIO(22) | GPIO_GPIO(23) | GPIO_GPIO(24));
   kbd_timer.expires = 40 + jiffies;
   kbd_timer.data = 0x00000000;
   kbd_timer.function = (void(*)(unsigned long))&keyboard_interrupt;

   add_timer(&kbd_timer);

   printk("Done\r\n");
}
