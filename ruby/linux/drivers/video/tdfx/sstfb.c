/*
 * linux/drivers/video/sttfb.c -- voodoo graphics frame buffer
 *
 * 01/2000 ghozlane Toumi  <gtoumi@messel.emse.fr>
 *
 * $Id$
 */

/*
 * The voodoo1 has the following memory mapped adress space:
 * 0x000000 - 0x3fffff : registers              (4Mb)
 * 0x400000 - 0x7fffff : linear frame buffer    (4Mb)
 * 0x800000 - 0xffffff : texture memory         (8Mb)
 */

/*
 * misc notes, TODOs, toASKs, and deep thoughts

-TODO: at one time or another test that the mode is acceptable by the monitor 
-ASK: i can choose different ordering for the color bitfields (rgba argb ...) w
ich one should i use ? is there any preferencial one ?
-ASK: later: how to cope with endianness ? the fbi chip has builtins functions 
to do byte swizling /swapping, maybe use that ...
-TODO: move the statics in adequate static strucs (sstfb_par sstfb_info and so 
on. see other drivers .
-TODO: check the returns value. i've got a bad feeling about some of them...
-ASK: ioremap ou ioremap_nocache ?
-TODO: in  set_var check the validity of timings (hsync vsync)...
-FIXME: i'm not sure i like all the functions with no parameters.. chage them t
o use a sstfb_par or sstfb_info or something like that.
-ASK: best aproach for supporting different dacs types ? array of functions ind
exec by dac-type : 1 detection, 2 pll calc, 3 video setting, 4 bpp setting ? or
 as it is now, a small switch or a couple of ifs ?
-TODO: check and recheck the use of sst_wait_idle : we dont flush the fifo via 
a nop command . so it's ok as long as the commands we pass don't go trou the fi
fo. warning: issuing a nop command seems to need pci_fifo enabled 
-TODO: get rid of the C++ style comments //
-TODO: check the error paths . if something get wrong, the error doesn't seem t
o be very well handled...if handled at all.. not good.
-ASK: the 24 bits mode is NOT packed . how do i differenciate from a packed mod
e ? set a pseudo alpha value not used ?
-ASK: how does the 32 bpp work ? should i enable the pipeline so alpha values a
re used ?
-TODO: check how the voodoo graphics cope with 24/32 bpp (engine is 16bpp only)
-ASK: do i ioremap the complete area devoted to the lfb (4Mb), or check the rea
l size, then unmap and remap to the real size of the lfb ? ...
-FIXME: in case of failure in the init sequence, be sure we return to a safe st
ate.
-FIXME: better check of the max pixclock...

 *
 */

#include <linux/string.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/fb.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include <linux/version.h>

#include <asm/io.h>
#include <asm/ioctl.h>

/*
 * debug info 
 * SST_DEBUG : enable debugging
 * SST_DEBUG_REG : debug registers
 *   0 / undef :  no debug
 *   1 : dac calls, [un]set_bits, fbiinit
 *   2 : insane debug level (log every register read/write)
 * SST_DEBUG_FUNC : functions 
 *   0 / undef : no debug
 *   1 : function call / debug ioctl
 *   2 : variables
 *   3 : flood . you don't want to do that. trust me.
 * SST_DEBUG_VAR : debug display/var structs
 *   0 / undef : no debug
 *   1 : dumps display, fb_var
 * SST_DEBUG_IOCTL : enable sstfb ioctls
 *   0 / undef : nodebug
 *   1 : enable toggle vga_pass_through, fill fb and dump display[0-5].var
 *
 */

#undef SST_DEBUG
//#define SST_DEBUG
#define SST_DEBUG_REG   0
#define SST_DEBUG_FUNC  2       
#define SST_DEBUG_VAR   1
#define SST_DEBUG_IOCTL 1

#ifdef SST_DEBUG
#  define dprintk(X...)         printk(KERN_DEBUG "sstfb: " X)
#else
#  define dprintk(X...)
#  undef SST_DEBUG_REG
#  undef SST_DEBUG_FUNC
#  undef SST_DEBUG_VAR
#  undef SST_DEBUG_IOCTL
#endif

#if (SST_DEBUG_REG > 0)
#  define r_dprintk(X...)       dprintk(X)
#else
#  define r_dprintk(X...)
#endif
#if (SST_DEBUG_REG > 1)
#  define r_ddprintk(X...)      dprintk(" " X)
#else
#  define r_ddprintk(X...)
#endif

#if (SST_DEBUG_FUNC > 0)
#  define f_dprintk(X...)       dprintk(X)
#else
#  define f_dprintk(X...)
#endif
#if (SST_DEBUG_FUNC > 1)
#  define f_ddprintk(X...)      dprintk(" " X)
#else
#  define f_ddprintk(X...)
#endif
#if (SST_DEBUG_FUNC > 2)
#  define f_dddprintk(X...)     dprintk(" " X)
#else
#  define f_dddprintk(X...)
#endif

#if (SST_DEBUG_VAR > 0)
#  define v_dprintk(X...)       dprintk(X)
#  define print_var(V, X...)    \
   {                            \
     dprintk(X);                \
     printk(" : {\n");          \
     __print_var(V);            \
   }
#else
#  define v_dprintk(X...)
#  define print_var(X,Y...)
#endif


#define eprintk(X...)   printk(KERN_ERR "sstfb: " X)
#define iprintk(X...)   printk(KERN_INFO "sstfb: " X)
#define wprintk(X...)   printk(KERN_WARNING "sstfb: " X)

#define BIT(x)          (1ul << (x))
#define PICOS2KHZ(a)    (1000000000UL/(a))

/* pci stuff */
#define PCI_INIT_ENABLE         0x40
#define PCI_EN_INIT_WR          BIT(0)
#define PCI_EN_FIFO_WR          BIT(1)
#define PCI_REMAP_DAC           BIT(2)
#define PCI_VCLK_ENABLE         0xc0    /* enable video */
#define PCI_VCLK_DISABLE        0xe0

/* register offsets from memBaseAddr */
#define STATUS                  0x0000
#define STATUS_FBI_BUSY         BIT(7)
#define FBZMODE                 0x0110
#define EN_CLIPPING             BIT(1)
#define EN_RGB_WRITE            BIT(9)
#define EN_ALPHA_WRITE          BIT(10)
#define LFBMODE                 0x0114
#define LFB_565                 0             	/* bits 3:0 .16 bits RGB */
#define LFB_888                 4             	/* 24 bits RGB */
#define LFB_8888                5		/* 32 bits ARGB */
#define CLIP_LEFT_RIGHT         0x0118
#define CLIP_LOWY_HIGHY         0x011c
#define NOPCMD                  0x0120
#define FASTFILLCMD             0x0124
#define FBIINIT4                0x0200        	/* misc controls */
#define FAST_PCI_READS          0             	/* 1 waitstate */
#define SLOW_PCI_READS          BIT(0)        	/* 2 ws */
#define LFB_READ_AHEAD          BIT(1)
#define BACKPORCH               0x0208
#define VIDEODIMENSIONS         0x020c
#define FBIINIT0                0x0210          /* misc+fifo  controls */
#define EN_VGA_PASSTHROUGH      BIT(0)
#define FBI_RESET               BIT(1)  
#define FIFO_RESET              BIT(2)
#define FBIINIT1                0x0214       	/* PCI + video controls */
#define VIDEO_MASK              0x8180010f    	/* masks video related bits */
#define FAST_PCI_WRITES         0             	/* 0 ws */
#define SLOW_PCI_WRITES         BIT(2)        	/* 1 ws */
#define EN_LFB_READ             BIT(3)
#define TILES_IN_X_SHIFT        4
#define VIDEO_RESET             BIT(8)
#define EN_BLANKING             BIT(12)
#define EN_DATA_OE              BIT(13)
#define EN_BLANK_OE             BIT(14)
#define EN_HVSYNC_OE            BIT(15)
#define EN_DCLK_OE              BIT(16)
#define SEL_INPUT_VCLK_2X       0		/* bit 17 */
#define SEL_SOURCE_VCLK_2X_DIV2 (0x01 << 20)    
#define SEL_SOURCE_VCLK_2X_SEL  (0x02 << 20)
#define EN_24BPP                BIT(22)
#define VCLK_2X_SEL_DEL_SHIFT   27            	/* vclk out delay 0,4,6,8ns */
#define VCLK_DEL_SHIFT          29            	/* vclk in delay */ 
#define FBIINIT2                0x0218          /* Dram controls */
#define EN_FAST_RAS_READ        BIT(5)
#define EN_DRAM_OE              BIT(6)
#define EN_FAST_RD_AHEAD_WR     BIT(7)
#define SWAP_DACVSYNC           0
#define SWAP_DACDATA0           (1 << 9)
#define SWAP_FIFO_STALL         (2 << 9)
#define EN_RD_AHEAD_FIFO        BIT(21)
#define EN_DRAM_REFRESH         BIT(22)
#define DRAM_REFRESH_16         (0x30 << 23)  	/* dram 16 ms */
#define DAC_READ                FBIINIT2        /* in remap mode */
#define FBIINIT3                0x021c          /* fbi controls */
#define DISABLE_TEXTURE         BIT(6)
#define HSYNC                   0x0220
#define VSYNC                   0x0224
#define DAC_DATA                0x022c
#define DAC_READ_CMD            BIT(11)  	/* set read dacreg mode */

/* Dac Registers */
#define DACREG_WMA              0x0     /* pixel write mode address */
#define DACREG_LUT              0x01    /* color value */
#define DACREG_RMR              0x02    /* pixel mask */
#define DACREG_RMA              0x03    /* pixel read mode address */
/*Dac registers in indexed mode (TI, ATT dacs) */
#define DACREG_ADDR_I           DACREG_WMA
#define DACREG_DATA_I           DACREG_RMR
#define DACREG_RMR_I            0x00
#define DACREG_CR0_I            0x01
#define DACREG_CR0_EN_INDEXED   BIT(0)        	/* enable indexec mode */
#define DACREG_CR0_8BIT         BIT(1)        	/* set dac to 8 bits/read */
#define DACREG_CR0_POWERDOWN    BIT(3)
#define DACREG_CR0_16BPP        0x30          	/* mode 3 */
#define DACREG_CR0_24BPP        0x50          	/* mode 5 */
#define DACREG_CR1_I            0x05
#define DACREG_CC_I             0x06
#define DACREG_CC_CLKA          BIT(7)        	/* clk A controled by regs */
#define DACREG_CC_CLKA_C        (2<<4)        	/* clk A uses reg C */
#define DACREG_CC_CLKB          BIT(3)        	/* clk B controled by regs */
#define DACREG_CC_CLKB_D        3             	/* clkB uses reg D */
#define DACREG_AC0_I            0x48            /* clock A reg C */
#define DACREG_AC1_I            0x49
#define DACREG_BD0_I            0x6c            /* clock B reg D */
#define DACREG_BD1_I            0x6d

/* identification constants */
#define DACREG_MIR_TI           0x97
#define DACREG_DIR_TI           0x09
#define DACREG_MIR_ATT          0x84
#define DACREG_DIR_ATT          0x09
/* ics dac specific registers*/
#define DACREG_ICS_PLLWMA       0x04    	/* PLL write mode address */
#define DACREG_ICS_PLLDATA      0x05    	/* PLL data /parameter */
#define DACREG_ICS_CMD          0x06    	/* command */
#define DACREG_ICS_CMD_16BPP    0x50  		/* ics color mode 6 */
#define DACREG_ICS_CMD_24BPP    0x70  		/* ics color mode 7 */
#define DACREG_ICS_CMD_POWERDOWN  BIT(0)
#define DACREG_ICS_PLLRMA       0x07    	/* PLL read mode address */
/*
 * pll parameter register: 
 * indexed : write addr to PLLWMA, write data in PLLDATA.
 * for reads use PLLRMA .
 * 8 freq registers (0-7) for video clock (CLK0) 
 * 2 freq registers (a-b) for graphic clock (CLK1)
 */
#define DACREG_ICS_PLL_CLK0_1_INI 0x55  /* initial pll M value for freq f1  */
#define DACREG_ICS_PLL_CLK0_7_INI 0x71  /* f7 */
#define DACREG_ICS_PLL_CLK1_B_INI 0x79  /* fb */
#define DACREG_ICS_PLL_CTRL       0x0e
#define DACREG_ICS_CLK0        	  BIT(5)
#define DACREG_ICS_CLK0_0         0
#define DACREG_ICS_CLK1_A         0     /* bit4 */

/* recognised Dacs */
#define DAC_TYPE_TI     0       /* Dac: TI TVP3409 */
#define DAC_TYPE_ATT    1       /* Dac: AT&T ATT20C409 */
#define DAC_TYPE_ICS    2       /* Dac: ICS ICS5342 */

/* used to know wich clock to set */
#define VID_CLOCK       0
#define GFX_CLOCK       1

/* misc const */
#define DAC_FREF        14318   /* DAC ref freq */
/*
 * freq of the fbi (graphic) chip (in khz) . default is 50Mhz.
 * has to stay  between 16 and 80, knowing that most card wont do more than 57
 * TODO: see if it has any influence on the lfb access .if no, lower it (less
 * power hungry)
 */
#define GFX_DEFAULT_FREQ        50000   /* 50 Mhz */

/* sst default init registers */
#define FBIINIT0_DEFAULT EN_VGA_PASSTHROUGH

#define FBIINIT1_DEFAULT        \
        (                       \
          FAST_PCI_WRITES       \
/*        SLOW_PCI_WRITES*/     \
        | VIDEO_RESET           \
        | 10 << TILES_IN_X_SHIFT\
        | SEL_SOURCE_VCLK_2X_SEL\
        | EN_LFB_READ           \
        )

#define FBIINIT2_DEFAULT        \
        (                       \
         SWAP_DACVSYNC          \
        | EN_DRAM_OE            \
        | DRAM_REFRESH_16       \
        | EN_DRAM_REFRESH       \
        | EN_FAST_RAS_READ      \
        | EN_RD_AHEAD_FIFO      \
        | EN_FAST_RD_AHEAD_WR   \
        )

#define FBIINIT3_DEFAULT        \
        ( DISABLE_TEXTURE )

#define FBIINIT4_DEFAULT        \
        (                       \
          FAST_PCI_READS        \
/*        SLOW_PCI_READS*/      \
        | LFB_READ_AHEAD        \
        )


struct pll_timing {
        u8 m;
        u8 n;
        u8 p;
};

/* XXX */
static void sstfb_test(void);

static int configured = 0;
int currcon = 0;
static char sstfb_name[16] = "Voodoo Graphics";

static u_long membase_phys; /* begining of physical adress space */
static u_long regbase_virt; /* mem mapped registers (4Mb)*/  
static u_long fbbase_virt;  /* mem mapped frame buffer (4Mb)*/
static u_long lfb_size;  /* usable size of the frame buffer */
static int dactype;
static struct pci_dev * sst_dev;
static u8 revision;

static struct { u_int red, green, blue, transp; } palette[16];
static struct fb_info fb_info;

static struct fb_var_screeninfo sstfb_default = {
#if 0
    /* 800x600@60, 16 bpp .borowed from glide/sst1/include/sst1init.h */
    800, 600, 800, 600, 0, 0, 16, 0,
    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
    0, 0, -1, -1, 0,
    25000, 86, 41, 23, 1, 127, 4,
    0, FB_VMODE_NONINTERLACED
};
#else
    /* 640x480@75, 16 bpp .borowed from glide/sst1/include/sst1init.h */
    640, 480, 640, 480, 0, 0, 16, 0,
    {11, 5, 0}, {5, 6, 0}, {0, 5, 0}, {0, 0, 0},
    0, 0, -1, -1, 0,
    31746, 118, 17, 16, 1, 63, 3,
    0, FB_VMODE_NONINTERLACED
};
#endif 

/* Interface to ze oueurld  */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
void sstfb_init(void);
void sstfb_setup(char *options, int *ints);
#else
int  sstfb_init(void);
void sstfb_setup(char *options);
#endif


/* Framebuffer API */
static int sstfb_open(struct fb_info *info, int user);
static int sstfb_release(struct fb_info *info, int user);
static int sstfb_get_fix(struct fb_fix_screeninfo *fix,
                         int con, struct fb_info *info);
static int sstfb_get_var(struct fb_var_screeninfo *var,
                         int con, struct fb_info *info);
static int sstfb_set_var(struct fb_var_screeninfo *var,
                         int con, struct fb_info *info);
static int sstfb_get_cmap(struct fb_cmap *cmap, int kspc,
                          int con, struct fb_info *info);
static int sstfb_set_cmap(struct fb_cmap *cmap, int kspc,
                          int con, struct fb_info *info);
static int sstfb_pan_display(struct fb_var_screeninfo *var,
                             int con, struct fb_info *info);
static int sstfb_ioctl(struct inode *inode, struct file *file,
                       u_int cmd, u_long arg, int con,
                       struct fb_info *info);

/* Interface to the low level console driver */
static int sstfbcon_switch(int con, struct fb_info *info);
static int sstfbcon_updatevar(int con, struct fb_info *info);
static void sstfbcon_blank(int blank, struct fb_info *info);

/* Internal routines */
static u_long sstfb_get_linelength(int bpp);
static void sstfb_install_cmap(int con, struct fb_info *info);
static int sstfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                           u_int *transp, struct fb_info *info);
static int sstfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                           u_int transp, struct fb_info *info);

/* Low level routines */
static int sst_get_memsize(u_long *memsize);
//static int sst_is_idle(void);
static int sst_wait_idle(void);
static int sst_detect_dactype(void);
static int sst_detect_att_ti(void);
static int sst_detect_ics(void);
static int sst_calc_pll(int freq, int *freq_out, struct pll_timing *t);
static int sst_set_pll(struct pll_timing *t, int clock);
static int sst_set_pll_att_ti(struct pll_timing *t, int clock);
static int sst_set_pll_ics(struct pll_timing *t, int clock);
static int sst_set_vidmod(struct fb_var_screeninfo *var);
static void sst_set_vidmod_att_ti(int bpp);
static void sst_set_vidmod_ics(int bpp);
static int sst_init(void);
#ifdef MODULE
static void sst_shutdown(void);
#endif

static struct fb_ops sstfb_ops = {
        sstfb_open,             /* open        */
        sstfb_release,          /* release     */
        sstfb_get_fix,          /* get fix     */
        sstfb_get_var,          /* get var     */
        sstfb_set_var,          /* set var     */
        sstfb_get_cmap,         /* get cmap    */
        sstfb_set_cmap,         /* set cmap    */
        sstfb_pan_display,      /* pan display */
        sstfb_ioctl,            /* ioctl       */
        NULL,                   /* mmap        */
        NULL                    /* rasterimg   */
};


#if (SST_DEBUG_VAR > 0)
/* debug info / dump a fb_var_screeninfo */
static void __print_var(struct fb_var_screeninfo *var) {
        dprintk(" %d, %d, %d, %d, %d, %d, %d, %d,\n",
                var->xres, var->yres, var->xres_virtual, var->yres_virtual,
                var->xoffset, var->yoffset,
                var->bits_per_pixel, var->grayscale);
        dprintk(" {%d, %d, %d}, {%d, %d, %d}, {%d, %d, %d}, {%d, %d, %d},\n",
                var->red.offset, var->red.length, var->red.msb_right,
                var->green.offset, var->green.length, var->green.msb_right,
                var->blue.offset, var->blue.length, var->blue.msb_right,
                var->transp.offset, var->transp.length,
                var->transp.msb_right);
        dprintk(" %d, %d, %d, %d, %d,\n",
                var->nonstd, var->activate,
                var->height, var->width, var->accel_flags);
        dprintk(" %d, %d, %d, %d, %d, %d, %d,\n",
                var->pixclock, var->left_margin, var->right_margin,
                var->upper_margin, var->lower_margin,
                var->hsync_len, var->vsync_len);
        dprintk(" %#x, %#x}\n",var->sync, var->vmode);
}
#endif

/* register acces */
static inline u32 sst_read(u32 reg)
{
        u32 ret;

        ret = readl(regbase_virt + reg);
#if (SST_DEBUG_REG > 0) /* i need the init registers :) */
        switch (reg) {
        case FBIINIT0:
        case FBIINIT1:
        case FBIINIT2:
        case FBIINIT3:
        case FBIINIT4:
                r_dprintk(" sst_read(%#x): %#x\n", reg, ret);
                break ;
        default:
                r_ddprintk("sst_read(%#x): %#x\n", reg, ret);
                break;
        }
#endif /*  (SST_DEBUG_REG > 0) */
        return ret;
}

static inline void sst_write(u32 reg, u32 val)
{
#if (SST_DEBUG_REG > 0)
        switch (reg) {
        case FBIINIT0:
        case FBIINIT1:
        case FBIINIT2:
        case FBIINIT3:
        case FBIINIT4:
                r_dprintk(" sst_write(%#x, %#x)\n", reg, val);
                break ;
        default:
                r_ddprintk("sst_write(%#x, %#x)\n", reg, val);
                break;
        }
#endif /*  (SST_DEBUG > 0) */
        writel(val, regbase_virt + reg);
}

static inline void sst_set_bits(u32 reg, u32 val)
{
        r_dprintk("sst_set_bits(%#x, %#x)\n", reg, val);
        sst_write(reg, sst_read(reg) | val);
}

static inline void sst_unset_bits(u32 reg, u32 val)
{
        r_dprintk("sst_unset_bits(%#x, %#x)\n", reg, val);
        sst_write(reg, sst_read(reg) & ~val);
}

/* dac access */
/* dac_read should be remaped to fbiinit2 (via pci reg init_enable) */
static u8 sst_dac_read(u8 reg)
{
        u8 ret;

#ifdef SST_DEBUG
        if ((reg & 0x07) != reg)
                dprintk("beeep: register adress is to high\n");
        reg &= 0x07;
#endif
        sst_write(DAC_DATA, ((u32)reg << 8) | DAC_READ_CMD );
        sst_wait_idle();
        ret=(sst_read(DAC_READ) & 0xff);
        r_dprintk("sst_dac_read(%#x): %#x\n", reg, ret);
        return (u8)ret;
}

static void sst_dac_write(u8 reg, u8 val)
{
        r_dprintk("sst_dac_write(%#x, %#x)\n", reg, val);
#ifdef SST_DEBUG
        if ((reg & 0x07) != reg)
                dprintk("beeep: register adress is to high\n");
        reg &= 0x07;
#endif
        sst_write(DAC_DATA,(((u32)reg << 8)) | (u32)val);
}

/* indexed access to ti/att dacs */
static u32 dac_i_read(u8 reg)
{
        u32 ret;

        sst_dac_write(DACREG_ADDR_I, reg);
        ret = sst_dac_read(DACREG_DATA_I);
        r_dprintk("sst_dac_read_i(%#x): %#x\n", reg, ret);
        return ret;
}

static void dac_i_write(u8 reg,u8 val)
{
        r_dprintk("sst_dac_write_i(%#x, %#x)\n", reg, val);
        sst_dac_write(DACREG_ADDR_I, reg);
        sst_dac_write(DACREG_DATA_I, val);
}

/*
  ----------------------------------------------------------------------------
*/

/*
 * Internal routines 
 */

static u_long sstfb_get_linelength(int bpp)
{       
        /*
         *   According to the specs, the linelength must be of 1024 *pixels*.
         * and the 24bpp mode is in fact a 32 bpp mode.
         */
        f_dprintk("sstfb_get_linelength(bpp: %d)\n",bpp);
        return ((bpp == 16) ? 2048 : 4096);
}


static void sstfb_install_cmap(int con, struct fb_info *info)
{
        f_dprintk("sstfb_install_cmap(con: %d)\n",con);
        f_ddprintk("currcon: %d\n", currcon);
        if (con != currcon)
                return;
        if (fb_display[con].cmap.len)
                fb_set_cmap(&fb_display[con].cmap, 1, sstfb_setcolreg, info);
        else
                fb_set_cmap(
                        fb_default_cmap(1<<fb_display[con].var.bits_per_pixel),
                        1, sstfb_setcolreg, info);
}

static int sstfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                           u_int *transp, struct fb_info *info)
{
        f_ddprintk("sstfb_getcolreg\n");
        if (regno >= 16) return 1;

        *red    = palette[regno].red;
        *green  = palette[regno].green;
        *blue   = palette[regno].blue;
        *transp = palette[regno].transp;
        f_dddprintk("%-2d rvba: %#x, %#x, %#x, %#x\n",
                 regno,*red, *green, *blue, *transp);
        return 0;
}

static int sstfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                           u_int transp, struct fb_info *info)
{
        u32 col;

        f_ddprintk("sstfb_setcolreg\n");
        f_dddprintk("%-2d rvba: %#x, %#x, %#x, %#x\n",
                 regno, red, green, blue, transp);
        if (regno >= 16) return 1;
        palette[regno].red   = red;
        palette[regno].green = green;
        palette[regno].blue  = blue;

        red    >>= (16 - disp.var.red.length);
        green  >>= (16 - disp.var.green.length);
        blue   >>= (16 - disp.var.blue.length);
        transp >>= (16 - disp.var.transp.length);
        col = (red << disp.var.red.offset)
                | (green << disp.var.green.offset)
                | (blue  << disp.var.blue.offset)
                | (transp << disp.var.transp.offset);

        switch(disp.var.bits_per_pixel) {
#ifdef FBCON_HAS_CFB16
        case 16:
                fbcon_cmap.cfb16[regno]=(u16)col;
                break;
#endif
//XXX
#if 0
#ifdef FBCON_HAS_CFB24
        case 24:
                fbcon_cmap.cfb24[regno]=col;
                break;
#endif
#ifdef FBCON_HAS_CFB32
        case 32:
                fbcon_cmap.cfb32[regno]=col;
                break;
#endif
#endif
        default:
                dprintk("bug line %d: bad depth %u\n",__LINE__,
                        disp.var.bits_per_pixel);
                break;
        }
        f_dddprintk("bpp: %d . encoded color: %#x\n",
                  disp.var.bits_per_pixel, col);
        return 0;
}


/*
 * Frame buffer API
 */
static int sstfb_get_fix(struct fb_fix_screeninfo *fix,
                         int con, struct fb_info *info)
{
        struct fb_var_screeninfo *var;

        f_dprintk("sstfb_get_fix(con: %d)\n",con);
        if (con == -1)
                var = &sstfb_default;
        else
                var = &fb_display[con].var;
        
/* TODO use an encode fix like the other drivers do. is it worthless ? */

        memset(fix, 0, sizeof(struct fb_fix_screeninfo));
        strcpy(fix->id, sstfb_name);
        /* lfb phys address = membase + 4Mb */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,1)
        fix->smem_start  = (char *) (membase_phys+0x400000);
#else
        fix->smem_start  = (membase_phys+0x400000);
#endif
        fix->smem_len    = lfb_size;

        fix->type        = FB_TYPE_PACKED_PIXELS;
        fix->visual      = FB_VISUAL_TRUECOLOR;
        fix->line_length = sstfb_get_linelength(var->bits_per_pixel);
        return 0;
}

static int sstfb_set_var(struct fb_var_screeninfo *var,
                         struct fb_info *info)
{
        struct display *display;
        int err;
        int real_length;
        int old_bpp,old_xres,old_yres;

        f_dprintk("sstfb_set_var(con: %d)\n",con);
        f_ddprintk("xres yres vxres vyres bpp activate\n");
        f_ddprintk("%-4d %-4d %-5d %-5d %-3d %#-8x\n",
                 var->xres,var->yres,var->xres_virtual,var->yres_virtual,
                 var->bits_per_pixel,var->activate);
        /*
         * basic checks
         */

        if (!var->xres)
                var->xres = 1;
        if (!var->yres)
                var->yres = 1;
        if (var->xres != var->xres_virtual)
                var->xres_virtual = var->xres;
        if (var->yres != var->yres_virtual)
                var->yres_virtual = var->yres;
        if (var->xres > 1024)
                return -EINVAL;

        if ((var->vmode & FB_VMODE_MASK) !=  FB_VMODE_NONINTERLACED) {
                eprintk("video mode non supported %#x\n",
                        (var->vmode & FB_VMODE_MASK));
                return -EINVAL;
        }
        
        if (var->bits_per_pixel <= 16)
                var->bits_per_pixel = 16;
//XXX
#if 0
        else if (var->bits_per_pixel <= 24)
                var->bits_per_pixel = 24;
        else if (var->bits_per_pixel <= 32)
                        var->bits_per_pixel = 32;
#endif
        else
                return -EINVAL;
        /*
         *  mem check
         */
        /* it seems that the fbi uses tiles of 64x16 pixels to "map" the mem*/ 
        /* FIXME: i don't like this... looks wrong*/
        real_length = ((var->xres + 63) / 64 ) * 64
                * ((var->bits_per_pixel == 16) ? 2 : 4);
        if ((real_length * var->yres_virtual) > lfb_size)
                return -ENOMEM;
        /*
         * correct the color bit fields
         */
        var->red.msb_right    = 0;
        var->green.msb_right  = 0;
        var->blue.msb_right   = 0;
        var->transp.msb_right = 0;
        switch (var->bits_per_pixel) {
        case 16:        /* RGB 565  LfbMode 0 */
                var->red.offset    = 11;
                var->red.length    = 5;
                var->green.offset  = 5;
                var->green.length  = 6;
                var->blue.offset   = 0;
                var->blue.length   = 5;
                var->transp.offset = 0;
                var->transp.length = 0;
                break;
//XXX
#if 0
        case 24:        /* RGB 888 LfbMode 4 */
                var->red.offset    = 16;
                var->red.length    = 8;
                var->green.offset  = 8;
                var->green.length  = 8;
                var->blue.offset   = 0;
                var->blue.length   = 8;
                var->transp.offset = 0;
                var->transp.length = 0;
                break;
        case 32:        /* ARGB 8888 LfbMode 5 */
                var->red.offset    = 16;
                var->red.length    = 8;
                var->green.offset  = 8;
                var->green.length  = 8;
                var->blue.offset   = 0;
                var->blue.length   = 8;
                var->transp.offset = 24;
                var->transp.length = 8;
                break;
#endif
        }

        switch (var->activate & FB_ACTIVATE_MASK) {
                case FB_ACTIVATE_TEST:
                        return 0;
                case FB_ACTIVATE_NXTOPEN:
                case FB_ACTIVATE_NOW:
                        break;
                default:
                        return -EINVAL;
        }
        old_xres = display->var.xres;
        old_yres = display->var.yres;
        old_bpp  = display->var.bits_per_pixel;
        display->var = *var;

        if ((old_xres != var->xres) || (old_yres != var->yres)
            || (old_bpp != var->bits_per_pixel)) {
                /* 2-3  lignes redondantes avec get_fix */
                display->screen_base = (char *) fbbase_virt;
                display->visual = FB_VISUAL_TRUECOLOR;
                display->type = FB_TYPE_PACKED_PIXELS;
                display->type_aux = 0;
                display->ypanstep = 0;
                display->ywrapstep = 0;
                display->line_length = sstfb_get_linelength(var->bits_per_pixel
);
                display->can_soft_blank = 0;
                display->inverse = 0;
        }

        print_var(var, "var");
        print_var(&display->var, "&display->var");

        if (old_bpp != var->bits_per_pixel) {
            if ((err = fb_alloc_cmap(&display->cmap, 0, 0)))
                return err;
            sstfb_install_cmap(con, info);
        }

        /* mmmh... */
        if ((currcon == con ) || (con <0))
                sst_set_vidmod(var);

        return 0;
}

/* TODO */
static int sstfb_pan_display(struct fb_var_screeninfo *var,
                             int con, struct fb_info *info)
{
        f_dprintk("sstfb_pan_display\n");
        return -EINVAL;
}

static int sstfb_ioctl(struct inode *inode, struct file *file,
                      u_int cmd, u_long arg, int con,
                      struct fb_info *info)
{
#if (SST_DEBUG_IOCTL >0)
        int i;
        u_long p;
        u32 tmp;
#endif

        f_dprintk("sstfb_ioctl\n");

#if (SST_DEBUG_IOCTL >0)
        switch (cmd) {
#  if (SST_DEBUG_VAR >0)
/* tmp ioctl : dumps fb_display[0-5] */
        case _IO('F', 0xdb):            /* 0x46db */
                v_dprintk("fb_display[0-5].var\n");
                for (i = 0 ; i< 6 ; i++) {
                        print_var(&fb_display[i].var, "var(%d)", i);;
                }
                return 0;
                break;
#  endif /* (SST_DEBUG_VAR >0) */
/* fills the lfb up to *(u32*)arg */
        case _IOW('F', 0xdc, u32):      /* 0x46dc */
                if (*(u32*)arg > 0x400000 )
                        *(u32*) arg = 0x400000; 
                dprintk("fills %#x \n", *(u32*)arg);
                for (p = fbbase_virt; p < fbbase_virt + *(u32*)arg; p+=2)
                        writew( (p & 0x3fffc0) >> 6 , p);
                return 0;
                break; 
/* change VGA pass_through */
        case _IO('F', 0xdd):            /* 0x46dd */
                dprintk("switch VGA pass-through\n");
                pci_read_config_dword(sst_dev, PCI_INIT_ENABLE, &tmp);
                pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                                       tmp | PCI_EN_INIT_WR );
                sst_write(FBIINIT0, sst_read (FBIINIT0) ^ EN_VGA_PASSTHROUGH);
                pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, tmp);
                return 0;
                break;
        }
#endif /* (SST_DEBUG_IOCTL >0) */
        return -EINVAL;
}


/*
 * Low level routines
 */

/* get lfb size */

static int sst_get_memsize(u_long *memsize)
{
        f_dprintk("sst_get_memsize\n");
        
        writel (0xdeadbeef, fbbase_virt);
        writel (0xdeadbeef, fbbase_virt+0x100000);
        writel (0xdeadbeef, fbbase_virt+0x200000);
        f_ddprintk("0Mb: %#x, 1Mb: %#x, 2Mb: %#x\n", 
                   readl(fbbase_virt), readl(fbbase_virt + 0x100000),
                   readl(fbbase_virt + 0x200000));

        writel (0xabcdef01, fbbase_virt);

        f_ddprintk("0Mb: %#x, 1Mb: %#x, 2Mb: %#x\n", 
                   readl(fbbase_virt), readl(fbbase_virt + 0x100000),
                   readl(fbbase_virt + 0x200000));
        /* checks for 4mb lfb */
        if (readl(fbbase_virt + 0x200000) == 0xdeadbeef) {
                *memsize = 0x400000;
                f_ddprintk("memsize: %#lx\n", *memsize);
                return 1;
        }
        /* checks for 2mb lfb */
        if (readl(fbbase_virt + 0x100000) == 0xdeadbeef) {
                *memsize = 0x200000;
                f_ddprintk("memsize: %#lx\n", *memsize);
                return 1;
        }
        *memsize = 0x100000;
        f_ddprintk("memsize: %#lx\n", *memsize);
        return 1;
}

/* wait for the fbi chip. ASK: what happens if the fbi is stuck ? */
static int sst_wait_idle(void)
{
        int count = 0;

        f_ddprintk("sst_wait_idle\n");
        while(1) {
                if (sst_read(STATUS) & STATUS_FBI_BUSY) {
                        f_dddprintk("status: busy\n");
                        count = 0;
                } else {
                        count++;
                        f_dddprintk("status: idle(%d)\n", count);
                }
                if (count >= 5) return 1; 
        }
}

/*
 * detect dac type
 * prerequisite : write to fbiinitx enabled, video and fbi and pci fifo reset,
 * dram refresh disabled, fbiinit remaped. 
 * TODO: mmh.. maybe i shoud put the "prerequisite" in the func ...
 */
static int sst_detect_dactype(void)
{
        int ret;

        f_dprintk("sst_detect_dactype\n");
        ret = sst_detect_ics();
        if (!ret)
                ret = sst_detect_att_ti();
        return ret;
}

/* fbi should be idle, and fifo emty and mem disabled */
/* supposed to detect AT&T ATT20C409 and Ti TVP3409 ramdacs */
static int sst_detect_att_ti(void)
{
        int i, mir, dir;

        f_dprintk("sst_detect_att_ti\n");
        for (i = 0; i<10; i++) {
                sst_dac_write(DACREG_WMA, 0);   /* backdoor */
                sst_dac_read(DACREG_RMR);       /* read 4 times RMR */
                sst_dac_read(DACREG_RMR);
                sst_dac_read(DACREG_RMR);
                sst_dac_read(DACREG_RMR);
                /* the fifth time,  CR0 is read */
                sst_dac_read(DACREG_RMR);       
                /* the 6th, manufacturer id register */
                mir = sst_dac_read(DACREG_RMR); 
                /*the 7th, device ID register */
                dir = sst_dac_read(DACREG_RMR);
                f_ddprintk("mir: %#x, dir: %#x\n", mir, dir);
                if ((mir == DACREG_MIR_TI ) && (dir == DACREG_DIR_TI)) {
                        dactype = DAC_TYPE_TI;
                        return 1;
                }
                if ((mir == DACREG_MIR_ATT ) && (dir == DACREG_DIR_ATT)) {
                        dactype = DAC_TYPE_ATT;
                        return 1;
                }
        }
        return 0;
}

/* 
 * try to detect ICS5342  ramdac
 * we get the 1st byte (M value) of preset f1,f7 and fB
 * why those 3 ? mmmh... for now, i'll do it the glide way... 
 * and ask questions later. anyway, it seems that all the freq registers are 
 * realy at their default state (cf specs) so i ask again, why those 3 regs ?
 * mmmmh.. it seems that's much more ugly than i thought. we use f0 and fA for
 * pll programming, so in fact, we *hope* that the f1, f7 & fB wont be
 * touched...
 * is it realy safe ? how can i reset this ramdac ? geee... 
 */
static int sst_detect_ics(void)
{
        int i;
        int m_clk0_1, m_clk0_7, m_clk1_b;
        
        f_dprintk("sst_detect_ics\n");
        for (i = 0; i<10; i++ ) {
                sst_dac_write(DACREG_ICS_PLLRMA, 0x1);  /* f1 */
                m_clk0_1 = sst_dac_read(DACREG_ICS_PLLDATA);
                sst_dac_write(DACREG_ICS_PLLRMA, 0x7);  /* f7 */
                m_clk0_7 = sst_dac_read(DACREG_ICS_PLLDATA);
                sst_dac_write(DACREG_ICS_PLLRMA, 0xb);  /* fB */
                m_clk1_b= sst_dac_read(DACREG_ICS_PLLDATA);
                f_ddprintk("m_clk0_1: %#x, m_clk0_7: %#x, m_clk1_b: %#x\n",
                        m_clk0_1, m_clk0_7, m_clk1_b); 
                if ((   m_clk0_1 == DACREG_ICS_PLL_CLK0_1_INI)
                    && (m_clk0_7 == DACREG_ICS_PLL_CLK0_7_INI)
                    && (m_clk1_b == DACREG_ICS_PLL_CLK1_B_INI)) {
                        dactype = DAC_TYPE_ICS;
                        return 1;
                }
        }
        return 0;
}

/* compute the m,n,p  , returns the real freq
 * (ics datasheet :  n <-> N1 p<->N2)
 *
 * Fout= Fref * (M+2)/( 2^P * (N+2)) 
 *  we try to get close to the asked freq
 *  with P as high, and M as low as possible
 * range:
 * ti/att : 0 <= M <= 255; 0 <= P <= 3; 0<= N <= 63
 * ics    : 0 <= M <= 127; 0 <= P <= 3; 0<= N <= 31
 * we'll use the lowest limitation, should be precise enouth 
 */
static int sst_calc_pll(int freq, int *freq_out, struct pll_timing *t)
{
/* FIXME needs a sanity check somewere */
        int m, n, p, best_err, fout;
        int best_n=-1;
        int best_m=-1;

        f_dprintk("sst_calc_pll(%dKhz)\n", freq);
        best_err = freq;
        p=3;
        /* f * 2^P = vco should be less than VCOmax ~ 250 MHz for ics*/
        while (((1 << p) * freq > 250000) && (p >= 0))
                p--;
        if (p == -1) return 0;
        for (n = 31; n >= 0; n--) {
                m = (freq * (1 << p) * (n + 2) / DAC_FREF) - 2 ;
                fout = (DAC_FREF * (m + 2)) / ((1 << p) * (n + 2));
                if ((abs (fout - freq) < best_err) && (m < 127)) {
                        best_n = n;
                        best_m = m;
                        best_err = abs(fout - freq);
                        /* we get lower m  and 2% error */
                        if (best_err < freq/50) break;
                }
        }
        if (best_n == -1) return 0; /* unlikely, but who knows ? */
        t->p=p;
        t->n=best_n;
        t->m=best_m;
        *freq_out=(DAC_FREF * (t->m + 2)) / ((1 << t->p) * (t->n + 2));
        f_ddprintk ("m: %d, n: %d, p: %d, F: %d\n",
                  t->m, t->n, t->p, *freq_out);
        return 1;
}

/*
 * gfx, video, pci fifo should be reset, dram refresh disabled 
 * see detect_dac 
 */
static int sst_set_pll(struct pll_timing *t, int clock)
{
        int ret;

        f_dprintk("sst_set_pll\n");
        f_ddprintk("m: %d, n: %d, p: %d, clk: %d\n",
                 t->m, t->n, t->p, clock);

        switch (dactype) {
        case DAC_TYPE_TI:
        case DAC_TYPE_ATT:
                ret = sst_set_pll_att_ti(t, clock);
                break;
        case DAC_TYPE_ICS:
                ret = sst_set_pll_ics(t, clock);
                break;
        default:
                f_dprintk("bug line %d: unknown dactype\n", __LINE__);
                ret = 0;
                break;
        }
        return ret;
}

static int sst_set_pll_att_ti(struct pll_timing *t, int clock)
{
        u8 cr0, cc;
        f_dprintk("sst_set_pll_att_ti\n");

        /* enable indexed mode */

        sst_dac_write(DACREG_WMA, 0);   /* backdoor */
        sst_dac_read(DACREG_RMR);       /* 1 time:  RMR */
        sst_dac_read(DACREG_RMR);       /* 2 RMR */
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);       /* 4 RMR */
        cr0 = sst_dac_read(DACREG_RMR); /* 5 CR0 */

        sst_dac_write(DACREG_WMA, 0);
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        sst_dac_write(DACREG_RMR, (cr0 & 0xf0)
                      | DACREG_CR0_EN_INDEXED
                      | DACREG_CR0_8BIT
                      | DACREG_CR0_POWERDOWN );
        /* so, now we are in indexed mode . dunno if its common, but
           i find this way of doing things a little bit weird :p */
        
        udelay(300);
        cc = dac_i_read(DACREG_CC_I);
        switch (clock) {
        case VID_CLOCK:
                dac_i_write(DACREG_AC0_I, t->m);
                dac_i_write(DACREG_AC1_I, t->p << 6 | t->n);
                dac_i_write(DACREG_CC_I,
                            (cc & 0x0f) | DACREG_CC_CLKA | DACREG_CC_CLKA_C);
                break;
        case GFX_CLOCK:
                dac_i_write(DACREG_BD0_I, t->m);
                dac_i_write(DACREG_BD1_I, t->p << 6 | t->n);
                dac_i_write(DACREG_CC_I,
                            (cc & 0xf0) | DACREG_CC_CLKB | DACREG_CC_CLKB_D);
                break;
        default:
                dprintk("bug line %d: wrong clock code\n", __LINE__);
                return 0;
                }
        udelay(300);
        
        /* power up the dac & return to "normal" non-indexed mode */
        dac_i_write(DACREG_CR0_I, 
                     cr0 & ~DACREG_CR0_POWERDOWN & ~DACREG_CR0_EN_INDEXED);
        return 1;
}

static int sst_set_pll_ics(struct pll_timing *t, int clock)
{
        u8 pll_ctrl;

        f_dprintk("sst_set_pll_ics\n");
        sst_dac_write(DACREG_ICS_PLLRMA, DACREG_ICS_PLL_CTRL);
        pll_ctrl = sst_dac_read(DACREG_ICS_PLLDATA);
        switch(clock) {
        case VID_CLOCK:
                sst_dac_write(DACREG_ICS_PLLWMA, 0x0);  /* CLK0, f0 */
                sst_dac_write(DACREG_ICS_PLLDATA, t->m);
                sst_dac_write(DACREG_ICS_PLLDATA, t->p << 5 | t->n);
                /* selects freq f0 for clock 0 */
                sst_dac_write(DACREG_ICS_PLLWMA, DACREG_ICS_PLL_CTRL);
                sst_dac_write(DACREG_ICS_PLLDATA, 
                              (pll_ctrl & 0xd8) 
                              | DACREG_ICS_CLK0 
                              | DACREG_ICS_CLK0_0);
                break;
        case GFX_CLOCK :
                sst_dac_write(DACREG_ICS_PLLWMA, 0xa);  /* CLK1, fA */
                sst_dac_write(DACREG_ICS_PLLDATA, t->m);
                sst_dac_write(DACREG_ICS_PLLDATA, t->p << 5 | t->n);
                /* selects freq fA for clock 1 */
                sst_dac_write(DACREG_ICS_PLLWMA, DACREG_ICS_PLL_CTRL);
                sst_dac_write(DACREG_ICS_PLLDATA, 
                              (pll_ctrl & 0xef) | DACREG_ICS_CLK1_A);
                break;
        default:
                dprintk("bug line %d: wrong clock code\n", __LINE__);
                return 0;
                }
        udelay(300);
        return 1;
}

static int sst_set_vidmod(struct fb_var_screeninfo *var)
{
        struct pll_timing vid_timings;
        int Fout;

        f_dprintk("sst_set_vidmod(%dx%d)\n", var->xres, var->yres);
        f_ddprintk("hSyncOn hSyncOff vSyncOn vSyncOff\n");
        f_ddprintk("%-7d %-8d %-7d %-8d\n",
                var->hsync_len,
                (var->right_margin + var->left_margin + var->xres),
                var->vsync_len,
                (var->upper_margin + var->lower_margin + var->yres));
        f_ddprintk("hBackPorch vBackPorch xDim yDim pixClock\n");
        f_ddprintk("%-10d %-10d %-4d %-4d %-8d\n",
                var->left_margin, var->upper_margin,
                var->xres, var->yres, var->pixclock);
        sst_write(NOPCMD, 0);
        sst_wait_idle();
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_INIT_WR);
        sst_set_bits(FBIINIT1, VIDEO_RESET);
        sst_set_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
        sst_unset_bits(FBIINIT2, EN_DRAM_REFRESH);
        sst_wait_idle();

        sst_write(BACKPORCH, var->left_margin | var->upper_margin << 16);
        sst_write(VIDEODIMENSIONS, (var->xres - 1) | (var->yres -1 ) << 16);
        /* Hsync on = hsync_len ; hsync off =  left + right margin +xres */ 
        sst_write(HSYNC, var->hsync_len | 
                  (var->right_margin + var->left_margin + var->xres) << 16);
        sst_write(VSYNC, var->vsync_len | 
                  (var->upper_margin + var->lower_margin + var->yres) << 16);
        /* everything is reset. we enable fbiinit2/3 remap : dac acces ok */
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                               PCI_EN_INIT_WR | PCI_REMAP_DAC );
        switch (dactype) {
        case DAC_TYPE_TI:
        case DAC_TYPE_ATT:
                sst_set_vidmod_att_ti(var->bits_per_pixel);
                break;
        case DAC_TYPE_ICS:
                sst_set_vidmod_ics(var->bits_per_pixel);
                break;
        default:
                dprintk("bug line %d: unknown dactype\n", __LINE__);
                return 0;
                break;
        }

        /* set video clock */
        sst_calc_pll(PICOS2KHZ(var->pixclock), &Fout, &vid_timings);
        sst_set_pll(&vid_timings, VID_CLOCK);

        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                               PCI_EN_INIT_WR);
/* number of 64x16 tiles needed to cover horizontaly the screen (tiles_in_x) */
        sst_write(FBIINIT1, ((sst_read(FBIINIT1) & VIDEO_MASK)
                             | EN_DATA_OE
                             | EN_BLANK_OE
                             | EN_HVSYNC_OE
                             | EN_DCLK_OE
/* XXX XXX (((var->xres + 63) / 64 )<< TILES_IN_X_SHIFT )*/ 
                             | (15 << TILES_IN_X_SHIFT)
                             | SEL_INPUT_VCLK_2X
                             | (2 << VCLK_2X_SEL_DEL_SHIFT)
                             | (2 << VCLK_DEL_SHIFT)));
/* try with vclk_in_delay =0 (bits 29:30) , vclk_out_delay =0 (bits(27:28)
 in (near) future set them accordingly to revision + resolution  (glide) 
 first understand what it stands for :) 
 FIXME: there are some artefacts... check for the vclk_in_delay 
 lets try with 6ns delay in both vclk_out & in...
 doh... they're still there :\
*/
        switch(var->bits_per_pixel) {
        case 16:
/* FIXME: recheck  the specs/glide ... vclk_sel,slave, 2xdiv2_2x...doh! */
                sst_set_bits(FBIINIT1, SEL_SOURCE_VCLK_2X_SEL);
                break;
//XXX
#if 0
        case 24:
        case 32:
                sst_set_bits(FBIINIT1, SEL_SOURCE_VCLK_2X_DIV2 | EN_24BPP);
                break;
#endif
        default:
                dprintk("bug line %d: bad depth %u\n", __LINE__,
                        var->bits_per_pixel );
                return 0;
                break;
        }
        sst_unset_bits(FBIINIT1, VIDEO_RESET);
        sst_unset_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
        sst_set_bits(FBIINIT2, EN_DRAM_REFRESH);
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_FIFO_WR);

        /* set lfbmode : set mode + front buffer for reads/writes
           + disable pipeline + disable byte swapping */
        switch(var->bits_per_pixel) {
        case 16:
                sst_write(LFBMODE, LFB_565);
                break;
//XXX
#if 0
        case 24:
                sst_write(LFBMODE, LFB_888);
                break;
        case 32:
                sst_write(LFBMODE, LFB_8888);
                break;
#endif
        default:
                dprintk("bug line %d: bad depth %u\n", __LINE__,
                        var->bits_per_pixel );
                return 0;
                break;
        }
        return 1;
}

static void  sst_set_vidmod_att_ti(int bpp)
{
        u8 cr0;

        f_dprintk("sst_set_vidmod_att_ti(bpp: %d)\n", bpp);
        
        sst_dac_write(DACREG_WMA, 0);   /* backdoor */
        sst_dac_read(DACREG_RMR);       /* read 4 times RMR */
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        /* the fifth time,  CR0 is read */
        cr0 = sst_dac_read(DACREG_RMR); 

        sst_dac_write(DACREG_WMA, 0);   /* backdoor */
        sst_dac_read(DACREG_RMR);       /* read 4 times RMR */
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        sst_dac_read(DACREG_RMR);
        /* cr0 */
        switch(bpp) {
        case 16:
                sst_dac_write(DACREG_RMR, (cr0 & 0x0f) | DACREG_CR0_16BPP);
                break;
//XXX
#if 0
        case 24:
        case 32:
                sst_dac_write(DACREG_RMR, (cr0 & 0x0f) | DACREG_CR0_24BPP);
                break;
#endif
        default:
                dprintk("bug line %d: bad depth %u\n", __LINE__, bpp);
                break;
        }
}

static void sst_set_vidmod_ics(int bpp)
{
        f_dprintk("sst_set_vidmod_ics(bpp: %d)\n", bpp);

        switch(bpp) {
        case 16:
                sst_dac_write(DACREG_ICS_CMD, DACREG_ICS_CMD_16BPP);
                break;
//XXX
#if 0
        case 24:
        case 32:
                sst_dac_write(DACREG_ICS_CMD, DACREG_ICS_CMD_24BPP);
                break;
        default:
                dprintk("bug line %d: bad depth %u\n", __LINE__, bpp);
                break;
#endif
        }
}

static int sst_init(void)
{
        struct pll_timing gfx_timings;
        int Fout;
        int dac_ok;

        f_dprintk("sst_init\n");
        f_ddprintk("fbiinit0   fbiinit1   fbiinit2   fbiinit3   fbiinit4\n");
        f_ddprintk("%#10x %#10x %#10x %#10x %#10x\n",
                 sst_read(FBIINIT0), sst_read(FBIINIT1), sst_read(FBIINIT2),
                 sst_read(FBIINIT3), sst_read(FBIINIT4));
        /* disable video clock */
        pci_write_config_dword(sst_dev, PCI_VCLK_DISABLE,0);
        
        /* enable writing to init registers ,disable pci fifo*/
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_INIT_WR);
        /* reset video */
        sst_set_bits(FBIINIT1, VIDEO_RESET);
        /* reset gfx + pci fifo */
        sst_set_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
        /* disable dram refresh */
        sst_unset_bits(FBIINIT2, EN_DRAM_REFRESH);
        sst_wait_idle();
        /* remap fbinit2/3 to dac */
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                               PCI_EN_INIT_WR | PCI_REMAP_DAC );
        /* detect dac type */
        dac_ok = sst_detect_dactype();
        if (!dac_ok) {
                eprintk("Unknown dac type\n");
        } else {
                switch (dactype) {
                case DAC_TYPE_TI:
                        iprintk("Voodoo Graphics with TI TVP3409 dac\n");
                        break;
                case DAC_TYPE_ATT:
                        iprintk("Voodoo Graphics with AT&T ATT20C409 dac\n");
                        break;
                case DAC_TYPE_ICS:
                        iprintk("Voodoo Graphics with ICS ICS5342 dac\n");
                        break;
                default:
                        dprintk("bug line %d: unknown dactype\n", __LINE__);
                        dac_ok = 0;
                        break;
                }
        }

        /* set graphic clock */
        if (dac_ok) {
                sst_calc_pll(GFX_DEFAULT_FREQ, &Fout, &gfx_timings);
                sst_set_pll(&gfx_timings, GFX_CLOCK);
        }
        /* disable fbiinit remap */
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                               PCI_EN_INIT_WR| PCI_EN_FIFO_WR );
        /* defaults init registers */
        /* fbiInit0: unreset gfx, unreset fifo */
        sst_write(FBIINIT0, FBIINIT0_DEFAULT);
        sst_wait_idle();
        sst_write(FBIINIT1, FBIINIT1_DEFAULT);
        sst_wait_idle();
        sst_write(FBIINIT2, FBIINIT2_DEFAULT);
        sst_wait_idle();
        sst_write(FBIINIT3, FBIINIT3_DEFAULT);
        sst_wait_idle();
        sst_write(FBIINIT4, FBIINIT4_DEFAULT);
        sst_wait_idle();

        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_FIFO_WR );
        pci_write_config_dword(sst_dev, PCI_VCLK_ENABLE, 0);

        return dac_ok;
}

#ifdef MODULE

static void  sst_shutdown(void)
{
        struct pll_timing gfx_timings;
        int Fout;
                
        f_dprintk("sst_shutdown\n");
        /* reset video, gfx, fifo, disable dram + remap fbiinit2/3 */
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE, PCI_EN_INIT_WR);
        sst_set_bits(FBIINIT1, VIDEO_RESET | EN_BLANKING);
        sst_unset_bits(FBIINIT2, EN_DRAM_REFRESH);
        sst_set_bits(FBIINIT0, FBI_RESET | FIFO_RESET);
        sst_wait_idle();
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                               PCI_EN_INIT_WR | PCI_REMAP_DAC );
        /*set 20Mhz gfx clock */
        sst_calc_pll(20000, &Fout, &gfx_timings); 
        sst_set_pll(&gfx_timings, GFX_CLOCK);
        /* TODO maybe shutdown the dac, vrefresh and so on... */
        pci_write_config_dword(sst_dev, PCI_INIT_ENABLE,
                               PCI_EN_INIT_WR);
        sst_unset_bits(FBIINIT0, FBI_RESET | FIFO_RESET | EN_VGA_PASSTHROUGH);
        pci_write_config_dword(sst_dev, PCI_VCLK_DISABLE,0);
}
#endif

/*
 * Interface to the world
 */

/* TODO */
void sstfb_setup(char *options)
{
        f_dprintk("sstfb_setup\n");
        return ;
}

int sstfb_init(void)
{
        struct pci_dev *pdev = NULL;
        struct fb_var_screeninfo var;

        f_dprintk("sstfb_init\n");
        dprintk("Compile date: "__DATE__" "__TIME__"\n");

        while ((pdev = pci_find_device(PCI_VENDOR_ID_3DFX, 
                                      PCI_DEVICE_ID_3DFX_VOODOO, pdev))) {
                sst_dev = pdev;
                membase_phys = pdev->resource[0].start;
                f_ddprintk("membase_phys: %#lx\n", membase_phys);
                pci_read_config_byte(sst_dev,
                                     PCI_REVISION_ID, &revision);
                f_ddprintk("revision: %d\n", revision);

                regbase_virt = (u32) ioremap(membase_phys, 0x400000);
                f_ddprintk("regbase_virt: %#lx\n", regbase_virt);
                if (!regbase_virt) {
                        eprintk("cannot remap register area %#lx\n",
                                membase_phys);
                        return -ENXIO;
                }
                fbbase_virt = (u32) ioremap(membase_phys+0x400000,
                                            0x400000);
                f_ddprintk("fbbase_virt: %#lx\n", fbbase_virt);
                if (!fbbase_virt) {
                        eprintk("cannot remap framebuffer %#lx\n",
                                membase_phys+0x400000);
                        iounmap((void*) regbase_virt);
                        return -ENXIO;
                }
                if(!sst_init()) {
                        eprintk("Init failed\n");
                        iounmap((void*)regbase_virt);
                        iounmap((void*)fbbase_virt);
                        return -ENXIO;
                }
                sst_get_memsize(&lfb_size);
                configured = 1;

                iprintk("framebuffer at %#lx, mapped to %#lx,"
                        " size %ldMb\n",
                        membase_phys, fbbase_virt, lfb_size >> 20);

                strcpy(fb_info.modename, sstfb_name);
                fb_info.node       = -1 ;
                fb_info.flags      = FBINFO_FLAG_DEFAULT;
                fb_info.fbops      = &sstfb_ops;
                fb_info.disp       = &disp;
                fb_info.changevar  = NULL;
                fb_info.switch_con = &sstfbcon_switch;
                fb_info.updatevar  = &sstfbcon_updatevar;
                fb_info.blank      = &sstfbcon_blank;
                var = sstfb_default;
                if (sstfb_set_var(&var, -1, &fb_info)) {
                        eprintk("can't set default video mode.\n");
                        return -ENXIO;
                }
                /*clear fb */
                memset_io(fbbase_virt, 0, lfb_size);
                /* print some squares ... */
                sstfb_test();

                /* register fb */
                if (register_framebuffer(&fb_info) < 0) {
                        eprintk("can't register framebuffer.\n");
                        return -ENXIO;
                }
                printk(KERN_INFO "fb%d: %s frame buffer device\n",
                       GET_FB_IDX(fb_info.node),fb_info.modename);
                return 0;
        }
        return -ENXIO;
}

static void sstfb_blank(int blank, struct fb_info *info)
{
        f_dprintk("sstfbcon_blank\n");
        f_ddprintk("blank level: %d\n", blank);
}

/* print some squares on the fb (16bpp)  */
static void sstfb_test(void)
{
        int i,j;
        u_long p;

        /* rect blanc 20x100 */
        for (i=0 ; i< 100; i++) {
          p = fbbase_virt + 2048 *i+400; 
          for (j=0 ; j < 10 ; j++) {
            writel( 0xffffffff, p);
            p+=4;
          }
        }
        /* rect bleu 180x200 */
        for (i=0 ; i< 200; i++) {
          p = fbbase_virt + 2048 *i;
          for (j=0 ; j < 90 ; j++) {
            writel(0x001f001f,p);
            p+=4;
          }
        }
        /* carre vert 40x40 */
        for (i=0 ; i< 40 ; i++) {
          p = fbbase_virt + 2048 *i;
          for (j=0; j <20;j++) {
            writel(0x07e007e0, p);
            p+=4;
          }
        }
        /*carre rouge 40x40 */
        for (i=0; i<40; i++) {
          p = fbbase_virt + 2048 * (i+40);
          for (j=0; j <20;j++) {
            writel( 0xf800f800, p);
            p+=4;
          }
        }

        f_dprintk("write ok\n");
}


#ifdef MODULE

int init_module(void)
{
        f_dprintk("init_module\n");
        sstfb_init();
        return 0;
}

void cleanup_module(void)
{
        f_dprintk("cleanup_module\n");
        f_ddprintk("conf %d\n",configured);

        if (configured) {
                sst_shutdown();
                iounmap((void*)regbase_virt);
                iounmap((void*)fbbase_virt);
                unregister_framebuffer(&fb_info);
        }
}

#endif  /* MODULE */

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
