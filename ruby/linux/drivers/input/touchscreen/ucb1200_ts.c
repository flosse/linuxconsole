/*
 * SA11x0 + UCB1x00 Touch Screen Driver Version 0.2
 * Put together by Tak-Shing Chan <tchan.rd@idthk.com>
 *
 * 90% derived from the Touch screen driver for Tifon
 * Copyright 1999 Peter Danielsson
 *
 * Codec routines derived from Itsy's Touchscreen driver
 * Copyright (c) Compaq Computer Corporation, 1998, 1999
 * Author: Larry Brakmo.
 *
 * Sample filtering derived from Diagman (ucb1x00Touch.c)
 * Copyright (C) 1999 Intel Corp.
 *
 * IOCTL's and jitter elimination derived from ADS Touchscreen driver
 * Copyright (c) 2000 Century Software, Inc.
 *
 * iPAQ emulation derived from Driver for the h3600 Touch Screen
 * Copyright 2000 Compaq Computer Corporation.
 * Author: Charles Flynn.
 *
 * Add Some function to let it more flexible
 * Allen_cheng@xlinux.com
 * chester@linux.org.tw
 *
 * Add Freebird UCB1300 Touch-Panel/Button driver support
 * Eric Peng <ercipeng@coventive.com>
 * Tony Liu < tonyliu@coventive.com>
 *
 * Added support for ADCx (0-3) inputs on UCB1200
 * Brad Parker <brad@heeltoe.com>
 *
 * Added support for Flexanet machine.
 * On sa1100_ts_init() error, resouces are freed.
 * Jordi Colomer <jco@ict.es>
 *
 * Todo:
 * support other button driver controlled by UCB-1300
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/string.h>

#include <asm/uaccess.h>
#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/ucb1200.h>

/*
 * UCB1200 register 9: Touchscreen control register
 */
#define TSMX_POW (1 << 0)
#define TSPX_POW (1 << 1)
#define TSMY_POW (1 << 2)
#define TSPY_POW (1 << 3)
#define TSMX_GND (1 << 4)
#define TSPX_GND (1 << 5)
#define TSMY_GND (1 << 6)
#define TSPY_GND (1 << 7)
#define TSC_MODE_MASK (3 << 8)
#define TSC_MODE_INT (0 << 8)
#define TSC_MODE_PRESSURE (1 << 8)
#define TSC_MODE_POSITION (1 << 9)
#define TSC_BIAS_ENA (1 << 11)
#define TSPX_LOW (1 << 12)
#define TSMX_LOW (1 << 13)


static int raw_max_x, raw_max_y, res_x, res_y, raw_min_x, raw_min_y, xyswap;

static int cal_ok, x_rev, y_rev;

static char *dev_id = "ucb1200-ts";

static DECLARE_WAIT_QUEUE_HEAD(queue);
static struct timer_list timer;

/* state machine states for touch screen */
#define PRESSED  0
#define P_DONE   1
#define X_DONE   2
#define Y_DONE   3
#define RELEASED 4

/* state machine states for adc */
#define ADCX_IDLE   0
#define ADCX_SAMPLE 1

/* who owns the adc h/w (touch screen or adcx) */
#define ADC_OWNER_TS   1
#define ADC_OWNER_ADCX 2
static spinlock_t owner_lock = SPIN_LOCK_UNLOCKED;

#define BUFSIZE 128
#define XLIMIT 160
#define YLIMIT 160
static volatile int ts_state, adcx_state, adc_owner;
static int head, tail, sample;
static TS_EVENT cur_data, samples[3], buf[BUFSIZE];
static struct fasync_struct *fasync;
static unsigned long in_timehandle = 0;
static int adcx_channel, adcx_data[4];
/* Allen Add */
static void ts_clear(void);
static void print_par(void);


extern void ucb1200_stop_adc(void);
extern void ucb1200_start_adc(u16 input);
extern u16 ucb1200_read_adc(void);


static inline void set_read_x_pos(void)
{
	/* See Philips' AN809 for an explanation of the pressure mode switch */
	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPX_POW | TSMX_GND | TSC_MODE_PRESSURE | TSC_BIAS_ENA);

	/* generate a SIB frame */
	ucb1200_stop_adc();

	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPX_POW | TSMX_GND | TSC_MODE_POSITION | TSC_BIAS_ENA);

	ucb1200_start_adc(ADC_INPUT_TSPY);
}

static inline void set_read_y_pos(void)
{
	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPY_POW | TSMY_GND | TSC_MODE_PRESSURE | TSC_BIAS_ENA);

	/* generate a SIB frame */
	ucb1200_stop_adc();

	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPY_POW | TSMY_GND | TSC_MODE_POSITION | TSC_BIAS_ENA);

	ucb1200_start_adc(ADC_INPUT_TSPX);
}

static inline void set_read_pressure(void)
{
	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPX_POW | TSMX_POW | TSPY_GND | TSMY_GND | TSC_MODE_PRESSURE | TSC_BIAS_ENA);

	ucb1200_start_adc(ADC_INPUT_TSPX);
}

static void ts_clear(void)
{
   int i;

   for (i=0; i < BUFSIZE; i++)
   {
       buf[i].pressure=(short)NULL;
       buf[i].x=(int)NULL;
       buf[i].y=(int)NULL;
       buf[i].millisecs=(int)NULL;
   }

   head = 0;
   tail = 0;

}

/* Allen */
static inline int pen_up(void)
{
	ucb1200_stop_adc();
	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPX_POW | TSMX_POW | TSPY_GND | TSMY_GND);

	return ucb1200_read_reg(UCB1200_REG_TS_CTL) & TSPX_LOW;
}

static void new_data(void)
{
   static TS_EVENT last_data = { 0, 0, 0, 0 };
   int diff0, diff1, diff2, diff3;

   if (cur_data.pressure) {
      if (sample < 3) {
	 samples[sample].x = cur_data.x;
	 samples[sample++].y = cur_data.y;
	 return;
      }
      sample = 0;

/* Check the variance between X samples (discard if not similar), then choose the closest pair */
      diff0 = abs(samples[0].x - samples[1].x);
      diff1 = abs(samples[1].x - samples[2].x);
      diff2 = abs(samples[2].x - cur_data.x);
      diff3 = abs(cur_data.x - samples[1].x);

      if (diff0 > XLIMIT || diff1 > XLIMIT || diff2 > XLIMIT || diff3 > XLIMIT)
	 return;

      if (diff1 < diff2)
      {
	 if (diff1 < diff3)
	    cur_data.x = (samples[1].x + samples[2].x) / 2;
	 else
	    cur_data.x = (cur_data.x + samples[1].x) / 2;
      }
      else
      {
	 if (diff2 < diff3)
	    cur_data.x = (samples[2].x + cur_data.x) / 2;
	 else
	    cur_data.x = (cur_data.x + samples[1].x) / 2;
      }

/* Do the same for Y */
      diff0 = abs(samples[0].y - samples[1].y);
      diff1 = abs(samples[1].y - samples[2].y);
      diff2 = abs(samples[2].y - cur_data.y);
      diff3 = abs(cur_data.y - samples[1].y);

      if (diff0 > YLIMIT || diff1 > YLIMIT || diff2 > YLIMIT || diff3 > YLIMIT)
	 return;

      if (diff1 < diff2)
      {
	 if (diff1 < diff3)
	    cur_data.y = (samples[1].y + samples[2].y) / 2;
	 else
	    cur_data.y = (cur_data.y + samples[1].y) / 2;
      }
      else
      {
	 if (diff2 < diff3)
	    cur_data.y = (samples[2].y + cur_data.y) / 2;
	 else
	    cur_data.y = (cur_data.y + samples[1].y) / 2;
      }
   }
   else
   {
/* Reset jitter detection on pen release */
      last_data.x = 0;
      last_data.y = 0;
   }

/* Jitter elimination */
/* Allen Mask */
//   if ((last_data.x || last_data.y) && abs(last_data.x - cur_data.x) <= 3 && abs(last_data.y - cur_data.y) <= 3)
//	return;
/* Allen */

   cur_data.millisecs = jiffies;
   last_data = cur_data;

   if (head != tail)
   {
      int last = head--;
      if (last < 0)
	 last = BUFSIZE - 1;
   }
   buf[head] = cur_data;

   if (++head == BUFSIZE)
      head = 0;
   if (head == tail && tail++ == BUFSIZE)
      tail = 0;

   if (fasync)
      kill_fasync(&fasync, SIGIO, POLL_IN);
   wake_up_interruptible(&queue);
}

static TS_EVENT get_data(void)
{
   int last = tail;

   if (++tail == BUFSIZE)
      tail = 0;
   return buf[last];
}

static void adcx_take_ownership(void);

static void wait_for_action(void)
{
	adc_owner = ADC_OWNER_TS;
	ts_state = PRESSED;
	sample = 0;

	ucb1200_disable_irq(IRQ_UCB1200_ADC);
	ucb1200_set_irq_edge(TSPX_INT, GPIO_FALLING_EDGE);
	ucb1200_enable_irq(IRQ_UCB1200_TSPX);

	ucb1200_stop_adc();
	ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPX_POW | TSMX_POW | TSPY_GND | TSMY_GND | TSC_MODE_INT);

	/* if adc is waiting, start it */
	if (adcx_state == ADCX_SAMPLE) {
		adcx_take_ownership();
	}
}

static void
ts_take_ownership(void)
{
	/* put back in ts int mode */
	ucb1200_write_reg(UCB1200_REG_TS_CTL,
			  TSPX_POW | TSMX_POW | TSPY_GND | TSMY_GND |
			  TSC_MODE_INT);

	ucb1200_enable_irq(IRQ_UCB1200_ADC);
	set_read_pressure();
}

static void start_chain(void)
{
	unsigned long flags;

	ts_state = P_DONE;

	/* if adcx is idle, grab adc, else wait for ts state check at end */
	spin_lock_irqsave(&owner_lock, flags);
	if (adcx_state == ADCX_IDLE || adc_owner == ADC_OWNER_TS) {
		ts_take_ownership();
	}
	spin_unlock_irqrestore(&owner_lock, flags);
}

/* Forward declaration */
static void ucb1200_ts_timer(unsigned long);

static int ucb1200_ts_starttimer(void)
{
   in_timehandle++;
   init_timer(&timer);
   timer.function = ucb1200_ts_timer;
   timer.expires = jiffies + HZ / 100;
   add_timer(&timer);
   return 0;
}

static void ucb1200_ts_timer(unsigned long data)
{
   in_timehandle--;
   if (pen_up())
   {
      cur_data.pressure = 0;
      new_data();
      wait_for_action();
   }
   else
      start_chain();
}

static int ucb1200_ts_open(struct inode *inode, struct file *filp)
{
/* Allen Add */
	ts_clear();
/* Allen */

	MOD_INC_USE_COUNT;
	return 0;
}

static int ucb1200_ts_release(struct inode *inode, struct file *filp)
{
/* Allen Add */
	ts_clear();
/* Allen */

	ucb1200_ts_fasync(-1, filp, 0);
	MOD_DEC_USE_COUNT;
	return 0;
}

static void ucb1200_ts_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	if (in_timehandle > 0)
		return;

	switch (ts_state) {
	case PRESSED:
		start_chain();
		break;
	case P_DONE:
		cur_data.pressure = ucb1200_read_adc();
		ucb1200_enable_irq(IRQ_UCB1200_ADC);
		ucb1200_disable_irq(IRQ_UCB1200_TSPX);
		set_read_x_pos();
		ts_state++;
		break;
	case X_DONE:
		cur_data.x = ucb1200_read_adc();
		ucb1200_enable_irq(IRQ_UCB1200_ADC);
		set_read_y_pos();
		ts_state++;
		break;
	case Y_DONE:
		cur_data.y = ucb1200_read_adc();
		ucb1200_set_irq_edge(TSPX_INT, GPIO_RISING_EDGE);
		ucb1200_enable_irq(IRQ_UCB1200_TSPX);
		ucb1200_stop_adc();
		ucb1200_write_reg(UCB1200_REG_TS_CTL, TSPX_POW | TSMX_POW | TSPY_GND | TSMY_GND | TSC_MODE_INT);
		ts_state++;
		new_data();
		ucb1200_ts_starttimer();
		break;
	case RELEASED:
		cur_data.pressure = 0;
		new_data();
		wait_for_action();
	}
}

static void ucb1200_adcx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	adcx_data[adcx_channel] = ucb1200_read_adc();
	adcx_state = ADCX_IDLE;
	adc_owner = ADC_OWNER_TS;

	ucb1200_stop_adc();

	/* if ts is waiting, start it */
	if (ts_state == P_DONE) {
		ts_take_ownership();
	}
}

static void ucb1200_adc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	//printk("ucb1200_adc_interrupt() adc_owner %d\n", adc_owner);

	switch (adc_owner) {
	case ADC_OWNER_TS:
		ucb1200_ts_interrupt(irq, dev_id, regs);
		break;
	case ADC_OWNER_ADCX:
		ucb1200_adcx_interrupt(irq, dev_id, regs);
		break;
	}
}

static void
adcx_take_ownership(void)
{
	u16 inp = 0;

	adc_owner = ADC_OWNER_ADCX;

	/* take out of ts int mode */
	ucb1200_write_reg(UCB1200_REG_TS_CTL,
			  TSPX_POW | TSMX_POW | TSPY_GND | TSMY_GND);

	ucb1200_enable_irq(IRQ_UCB1200_ADC);

	switch (adcx_channel) {
	case 0: inp = ADC_INPUT_AD0; break;
	case 1: inp = ADC_INPUT_AD1; break;
	case 2: inp = ADC_INPUT_AD2; break;
	case 3: inp = ADC_INPUT_AD3; break;
	}

	ucb1200_start_adc(inp);
}

int ucb1200_adc_start(int channel)
{
	unsigned long flags;

#if 0
	printk("ucb1200_adc_start(channel=%d) adcx_state %d, adc_owner %d\n",
	       channel, adcx_state, adc_owner);
#endif

	if (adcx_state != ADCX_IDLE || adc_owner != ADC_OWNER_TS)
		return -EINVAL;

	adcx_state = ADCX_SAMPLE;
	adcx_channel = channel;

	/* if ts is idle, grab adc, else wait for adc state check at end */
	spin_lock_irqsave(&owner_lock, flags);
	if (ts_state == PRESSED) {
		adcx_take_ownership();
	}
	spin_unlock_irqrestore(&owner_lock, flags);

	return 0;
}

int ucb1200_adc_done(void)
{
	return adcx_state == ADCX_IDLE ? 1 : 0;
}

int ucb1200_adc_value(int channel)
{
	return adcx_data[channel];
}

static struct file_operations ucb1200_ts_fops = {
	read:	ucb1200_ts_read,
	poll:	ucb1200_ts_poll,
	ioctl:	ucb1200_ts_ioctl,
	fasync:	ucb1200_ts_fasync,
	open:	ucb1200_ts_open,
	release:ucb1200_ts_release,
};

int sa1100_ts_init(void)
{
#ifdef CONFIG_SA1100_ASSABET
	raw_max_x = 944;
	raw_max_y = 944;
	raw_min_x = 70;
	raw_min_y = 70;
	res_x = 320;
	res_y = 240;
#elif defined(CONFIG_SA1100_CERF)
	raw_max_x = 944;
	raw_max_y = 944;
	raw_min_x = 70;
	raw_min_y = 70;
#if defined(CONFIG_CERF_LCD_38_A)
	res_x = 240;
	res_y = 320;
#elif defined(CONFIG_CERF_LCD_57_A)
	res_x = 320;
	res_y = 240;
#elif defined(CONFIG_CERF_LCD_72_A)
	res_x = 640;
	res_y = 480;
#else
#warning "Cannot enable the UCB1200 Touchscreen Driver without selecting a Cerfboard screen orientation first"
#error
#endif

#elif defined(CONFIG_SA1100_FREEBIRD)
	raw_max_x = 925;
	raw_max_y = 875;
	raw_min_x = 85;
	raw_min_y = 60;
	res_x = 240;
	res_y = 320;
#elif defined(CONFIG_SA1100_YOPY)
	raw_max_x = 964;
	raw_max_y = 958;
	raw_min_x = 45;
	raw_min_y = 53;
	res_x = 240;
	res_y = 320;
#elif defined(CONFIG_SA1100_PFS168)
	raw_max_x = 944;
	raw_max_y = 944;
	raw_min_x = 70;
	raw_min_y = 70;
	res_x = 320;
	res_y = 240;
#elif defined(CONFIG_SA1100_SIMPAD)
	raw_max_x = 944;
	raw_max_y = 944;
	raw_min_x = 70;
	raw_min_y = 70;
	res_x = 800;
	res_y = 600;
#elif defined(CONFIG_SA1100_FLEXANET)
	switch (flexanet_GUI_type)
	{
	  /* set the touchscreen dimensions */
	  case FHH_GUI_TYPE_0:
	  
	    raw_max_x = 944;
	    raw_max_y = 944;
	    raw_min_x = 70;
	    raw_min_y = 70;
	    res_x = 320;
	    res_y = 240;
	    break;
	    
	  default:
	    return -ENODEV;
	}
#else
	raw_max_x = 885;
	raw_max_y = 885;
	raw_min_x = 70;
	raw_min_y = 70;
	res_x = 640;
	res_y = 480;
#endif

	xyswap = 0;
	head = 0;
	tail = 0;
/* Allen Add */
	cal_ok = 1;
	x_rev = 0;
	y_rev = 0;
/* Allen */

	init_waitqueue_head(&queue);

	/* Initialize the touchscreen controller */
	ucb1200_stop_adc();
	ucb1200_set_irq_edge(ADC_INT, GPIO_RISING_EDGE);

	wait_for_action();

	return 0;
}

int __init ucb1200_ts_init(void)
{
	int ret;

	register_chrdev(TS_MAJOR, TS_NAME, &ucb1200_ts_fops);

	if ((ret = ucb1200_request_irq(IRQ_UCB1200_ADC, ucb1200_adc_interrupt,
				       SA_INTERRUPT, TS_NAME, dev_id)))
	{
		printk("ucb1200_ts_init: failed to register ADC IRQ\n");
		return ret;
	}
	if ((ret = ucb1200_request_irq(IRQ_UCB1200_TSPX, ucb1200_ts_interrupt,
				       SA_INTERRUPT, TS_NAME, dev_id)))
	{
		printk("ucb1200_ts_init: failed to register TSPX IRQ\n");
		ucb1200_free_irq(IRQ_UCB1200_ADC, dev_id);
		return ret;
	}

	if ((ret = sa1100_ts_init()) != 0)
	{
		ucb1200_free_irq(IRQ_UCB1200_TSPX, dev_id);
		ucb1200_free_irq(IRQ_UCB1200_ADC, dev_id);
		return ret;
	}

	printk("ucb1200 touch screen driver initialized\n");

	return 0;
}

void __exit ucb1200_ts_cleanup(void)
{
	ucb1200_stop_adc();

	if (in_timehandle)
		del_timer(&timer);

	ucb1200_free_irq(IRQ_UCB1200_TSPX, dev_id);
	ucb1200_free_irq(IRQ_UCB1200_ADC, dev_id);

	unregister_chrdev(TS_MAJOR, TS_NAME);

	printk("ucb1200 touch screen driver removed\n");
}

module_init(ucb1200_ts_init);
module_exit(ucb1200_ts_cleanup);
