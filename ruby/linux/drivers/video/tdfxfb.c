/*
 *
 * tdfxfb.c
 *
 * Author: Hannu Mallat <hmallat@cc.hut.fi>
 *
 * Copyright © 1999 Hannu Mallat
 * All rights reserved
 *
 * Created      : Thu Sep 23 18:17:43 1999, hmallat
 * Last modified: Tue Nov  2 21:19:47 1999, hmallat
 *
 * Lots of the information here comes from the Daryll Strauss' Banshee 
 * patches to the XF86 server, and the rest comes from the 3dfx
 * Banshee specification. I'm very much indebted to Daryll for his
 * work on the X server.
 *
 * Voodoo3 support was contributed Harold Oga. Lots of additions
 * (proper acceleration, 24 bpp, hardware cursor) and bug fixes by Attila
 * Kesmarki. Thanks guys!
 * 
 * While I _am_ grateful to 3Dfx for releasing the specs for Banshee,
 * I do wish the next version is a bit more complete. Without the XF86
 * patches I couldn't have gotten even this far... for instance, the
 * extensions to the VGA register set go completely unmentioned in the
 * spec! Also, lots of references are made to the 'SST core', but no
 * spec is publicly available, AFAIK.
 *
 * The structure of this driver comes pretty much from the Permedia
 * driver by Ilario Nardinocchi, which in turn is based on skeletonfb.
 * 
 * TODO:
 * - support for 16/32 bpp needs fixing (funky bootup penguin)
 * - multihead support (basically need to support an array of fb_infos)
 * - banshee and voodoo3 now supported -- any others? afaik, the original
 *   voodoo was a 3d-only card, so we won't consider that. what about
 *   voodoo2?
 * - support other architectures (PPC, Alpha); does the fact that the VGA
 *   core can be accessed only thru I/O (not memory mapped) complicate
 *   things?
 *
 * Version history:
 *
 * 0.1.3 (released 1999-11-02) added Attila's panning support, code
 *			       reorg, hwcursor address page size alignment
 *                             (for mmaping both frame buffer and regs),
 *                             and my changes to get rid of hardcoded
 *                             VGA i/o register locations (uses PCI
 *                             configuration info now)
 * 0.1.2 (released 1999-10-19) added Attila Kesmarki's bug fixes and
 *                             improvements
 * 0.1.1 (released 1999-10-07) added Voodoo3 support by Harold Oga.
 * 0.1.0 (released 1999-10-06) initial version
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <linux/kd.h>
#include <linux/vt_kern.h>
#include <asm/io.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/accels.h>	

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "fbcon.h"

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#include "tdfx.h"

//#define TDFXFB_DEBUG 
#ifdef TDFXFB_DEBUG
#define DPRINTK(a,b...) printk(KERN_DEBUG "fb: %s: " a, __FUNCTION__ , ## b)
#else
#define DPRINTK(a,b...)
#endif 

#define PICOS2KHZ(a) (1000000000UL/(a))
#define KHZ2PICOS(a) (1000000000UL/(a))

#define BANSHEE_MAX_PIXCLOCK 270000.0
#define VOODOO3_MAX_PIXCLOCK 300000.0

struct banshee_reg {
  /* VGA rubbish */
  unsigned char att[21];
  unsigned char crt[25];
  unsigned char gra[ 9];
  unsigned char misc[1];
  unsigned char seq[ 5];

  /* Banshee extensions */
  unsigned char ext[2];
  unsigned long vidcfg;
  unsigned long vidpll;
  unsigned long mempll;
  unsigned long gfxpll;
  unsigned long dacmode;
  unsigned long vgainit0;
  unsigned long vgainit1;
  unsigned long screensize;
  unsigned long stride;
  unsigned long cursloc;
  unsigned long curspataddr;
  unsigned long cursc0;
  unsigned long cursc1;
  unsigned long startaddr;
  unsigned long clip0min;
  unsigned long clip0max;
  unsigned long clip1min;
  unsigned long clip1max;
  unsigned long srcbase;
  unsigned long dstbase;
};

struct tdfxfb_par {
  u32 pixclock;
  u32 max_pixclock;
  u32 baseline;

  unsigned long regbase_virt;
  unsigned long iobase;

  u32 width;
  u32 height;
  u32 width_virt;
  u32 height_virt;
  u32 lpitch; /* line pitch, in bytes */
  u32 ppitch; /* pixel pitch, in bits */
  u32 bpp;    

  u32 hdispend;
  u32 hsyncsta;
  u32 hsyncend;
  u32 htotal;

  u32 vdispend;
  u32 vsyncsta;
  u32 vsyncend;
  u32 vtotal;

  u32 video;
  u32 accel_flags;
  u32 cmap_len;
  
  struct {
     int type;
     int state;
     int w,u,d;
     int x,y,redraw;
     unsigned long enable,disable;
     unsigned long cursorimage;
     struct timer_list timer;
  } cursor;

  spinlock_t DAClock;
};

static struct fb_info info;
static struct tdfxfb_par par;   
 
static struct fb_var_screeninfo tdfx_var __initdata = {
    /* "640x480, 8 bpp @ 60 Hz */
    640, 480, 640, 1024, 0, 0, 8, 0,
    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
    0, FB_ACTIVATE_NOW, -1, -1, FB_ACCELF_TEXT,
    39722, 40, 24, 32, 11, 96, 2,
    0, FB_VMODE_NONINTERLACED
};

/* default modedb mode */
static struct fb_videomode default_mode __initdata = {
    /* 640x480, 60 Hz, Non-Interlaced (25.172 MHz dotclock) */
    NULL, 60, 640, 480, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};

static struct fb_fix_screeninfo tdfx_fix __initdata = {
    "3Dfx", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_PSEUDOCOLOR, 0, 1, 1, 0, (unsigned long) NULL, 0, 
    FB_ACCEL_3DFX_BANSHEE 
};

/*
 *  Frame buffer device API
 */
int tdfxfb_init(void);
void tdfxfb_setup(char *options, int *ints);

static int tdfxfb_open(struct fb_info *info, int user); 
static int tdfxfb_release(struct fb_info *info, int user); 
static int  tdfxfb_decode_var(struct fb_var_screeninfo *var, 
                              struct tdfxfb_par *par,
                              struct fb_info *info);
static int  tdfxfb_encode_var(struct fb_var_screeninfo *var,
                              struct tdfxfb_par *par, struct fb_info *info);
static void tdfxfb_set_par(struct tdfxfb_par *par, struct fb_info *info);
static int tdfxfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                            u_int transp, struct fb_info *info);
static int tdfxfb_blank(int blank, struct fb_info *fb);
static int tdfxfb_pan_display(struct fb_var_screeninfo *var,	
			      struct fb_info *fb);

static struct fb_ops tdfxfb_ops = {
  tdfxfb_open,
  tdfxfb_release,
  tdfxfb_encode_var,
  tdfxfb_set_par,
  tdfxfb_setcolreg,
  tdfxfb_blank,
  tdfxfb_pan_display,
  NULL,           // fb_ioctl,
  NULL,           // fb_mmap
  NULL            // fb_rasterimg
};

/*
 *  Interface to the low level console driver
 */
static int tdfxfb_switch_con(int con, struct fb_info *fb);
static int tdfxfb_updatevar(int con, struct fb_info *fb); 

/*
 * do_xxx: Hardware-specific functions
 */
static u32 do_calc_pll(int freq, int* freq_out);
static void  do_write_regs(struct banshee_reg* reg);
static unsigned long do_lfb_size(void);

/* 
 * Accel engine handling
 */

#if 0
static void engine_init(void *par);
static void engine_reset(void *par);
static int  engine_state(void *par);
static void context_switch(void *old_par, void *new_par);
#endif
static void tdfx_fillrect(void *par, int x1, int y1, unsigned int width,
                          unsigned int height, unsigned long color, int rop);
static void tdfx_copyarea(void *par, int sx, int sy, unsigned int width,
                          unsigned int height, int dx, int dy);
static void tdfx_imageblit(void *par, int dx, int dy, unsigned int width,
                           unsigned int height, int image_depth, void *image);

static struct fb_info fb_info;

static int  noaccel = 0;
static int  nopan   = 0;
static int  nowrap  = 1;      // not implemented (yet)
static int  inverse = 0;
static int  nomtrr = 0;
static int  nohwcursor = 0;
static const char *mode_option __initdata = NULL;

/* ------------------------------------------------------------------------- 
 *                      Hardware-specific funcions
 * ------------------------------------------------------------------------- */

#ifdef VGA_REG_IO 
static inline  u8 vga_inb(u32 reg) { return inb(reg); }
static inline u16 vga_inw(u32 reg) { return inw(reg); }
static inline u16 vga_inl(u32 reg) { return inl(reg); }

static inline void vga_outb(u32 reg,  u8 val) { outb(val, reg); }
static inline void vga_outw(u32 reg, u16 val) { outw(val, reg); }
static inline void vga_outl(u32 reg, u32 val) { outl(val, reg); }
#else
static inline  u8 vga_inb(u32 reg) { 
  return inb(par.iobase + reg - 0x300); 
}
static inline u16 vga_inw(u32 reg) { 
  return inw(par.iobase + reg - 0x300); 
}
static inline u16 vga_inl(u32 reg) { 
  return inl(par.iobase + reg - 0x300); 
}

static inline void vga_outb(u32 reg,  u8 val) { 
  outb(val, par.iobase + reg - 0x300); 
}
static inline void vga_outw(u32 reg, u16 val) { 
  outw(val, par.iobase + reg - 0x300); 
}
static inline void vga_outl(u32 reg, u32 val) { 
  outl(val, par.iobase + reg - 0x300); 
}
#endif

static inline void gra_outb(u32 idx, u8 val) {
  vga_outb(GRA_I, idx); vga_outb(GRA_D, val);
}

static inline u8 gra_inb(u32 idx) {
  vga_outb(GRA_I, idx); return vga_inb(GRA_D);
}

static inline void seq_outb(u32 idx, u8 val) {
  vga_outb(SEQ_I, idx); vga_outb(SEQ_D, val);
}

static inline u8 seq_inb(u32 idx) {
  vga_outb(SEQ_I, idx); return vga_inb(SEQ_D);
}

static inline void crt_outb(u32 idx, u8 val) {
  vga_outb(CRT_I, idx); vga_outb(CRT_D, val);
}

static inline u8 crt_inb(u32 idx) {
  vga_outb(CRT_I, idx); return vga_inb(CRT_D);
}

static inline void att_outb(u32 idx, u8 val) {
  unsigned char tmp;
  tmp = vga_inb(IS1_R);
  vga_outb(ATT_IW, idx);
  vga_outb(ATT_IW, val);
}

static inline u8 att_inb(u32 idx) {
  unsigned char tmp;
  tmp = vga_inb(IS1_R);
  vga_outb(ATT_IW, idx);
  return vga_inb(ATT_IW);
}

static inline void vga_disable_video(void) {
  unsigned char s;
  s = seq_inb(0x01) | 0x20;
  seq_outb(0x00, 0x01);
  seq_outb(0x01, s);
  seq_outb(0x00, 0x03);
}

static inline void vga_enable_video(void) {
  unsigned char s;
  s = seq_inb(0x01) & 0xdf;
  seq_outb(0x00, 0x01);
  seq_outb(0x01, s);
  seq_outb(0x00, 0x03);
}

static inline void vga_disable_palette(void) {
  vga_inb(IS1_R);
  vga_outb(ATT_IW, 0x00);
}

static inline void vga_enable_palette(void) {
  vga_inb(IS1_R);
  vga_outb(ATT_IW, 0x20);
}

static inline u32 tdfx_inl(unsigned int reg) {
  return readl(par.regbase_virt + reg);
}

static inline void tdfx_outl(unsigned int reg, u32 val) {
  writel(val, par.regbase_virt + reg);
}

static inline void banshee_make_room(int size) {
  while((tdfx_inl(STATUS) & 0x1f) < size);
}
 
static inline void banshee_wait_idle(void) {
  int i = 0;

  banshee_make_room(1);
  tdfx_outl(COMMAND_3D, COMMAND_3D_NOP);

  while(1) {
    i = (tdfx_inl(STATUS) & STATUS_BUSY) ? 0 : i + 1;
    if(i == 3) break;
  }
}

static u32 do_calc_pll(int freq, int* freq_out) {
  int m, n, k, best_m, best_n, best_k, f_cur, best_error;
  int fref = 14318;

  /* this really could be done with more intelligence --
     255*63*4 = 64260 iterations is silly */
  best_error = freq;
  best_n = best_m = best_k = 0;
  for(n = 1; n < 256; n++) {
    for(m = 1; m < 64; m++) {
      for(k = 0; k < 4; k++) {
        f_cur = fref*(n + 2)/(m + 2)/(1 << k);
        if(abs(f_cur - freq) < best_error) {
          best_error = abs(f_cur-freq);
          best_n = n;
          best_m = m;
          best_k = k;
        }
      }
    }
  }
  n = best_n;
  m = best_m;
  k = best_k;
  *freq_out = fref*(n + 2)/(m + 2)/(1 << k);

  return (n << 8) | (m << 2) | k;
}

/*
 * FillRect 2D command (solidfill or invert (via ROP_XOR))   
 */
static void tdfx_fillrect(void *par, int x1, int y1, unsigned int width,
                          unsigned int height, unsigned long color, int rop)
{	
   /* COMMAND_2D reg. values */
   #define ROP_COPY        0xcc     // src
   #define ROP_INVERT      0x55     // NOT dst
   #define ROP_XOR         0x66     // src XOR dst

   u32 bpp = info.var.bits_per_pixel; 
   u32 fmt = info.fix.line_length | ((bpp+((bpp==8) ? 0 : 8)) << 13); 

   banshee_make_room(5);
   tdfx_outl(DSTFORMAT, fmt);
   tdfx_outl(COLORFORE, color);
   tdfx_outl(COMMAND_2D, COMMAND_2D_FILLRECT | (rop << 24));
   tdfx_outl(DSTSIZE,    width | (height << 16));
   tdfx_outl(LAUNCH_2D,  x1 | (y1 << 16));
   banshee_wait_idle();
}

/*
 * Screen-to-Screen BitBlt 2D command  
 */
static void tdfx_copyarea(void *par, int sx, int sy, unsigned int width,
                          unsigned int height, int dx, int dy)
{
   u32 blitcmd = COMMAND_2D_S2S_BITBLT | (ROP_COPY << 24);
   u32 bpp = info.var.bits_per_pixel;
   u32 fmt = info.fix.line_length | ((bpp + ((bpp == 8) ? 0 : 8)) << 13); 
   
   if (sx <= dx) {
     //-X 
     blitcmd |= BIT(14);
     sx += width-1;
     dx += width-1;
   }
   if (sy <= dy) {
     //-Y  
     blitcmd |= BIT(15);
     sy += height-1;
     dy += height-1;
   }
   
   banshee_make_room(6);

   tdfx_outl(SRCFORMAT, fmt);
   tdfx_outl(DSTFORMAT, fmt);
   tdfx_outl(COMMAND_2D, blitcmd); 
   tdfx_outl(DSTSIZE,   width | (height << 16));
   tdfx_outl(DSTXY,     dx | (dy << 16));
   tdfx_outl(LAUNCH_2D, sx | (sy << 16)); 
   banshee_wait_idle();
}

static void tdfx_imageblit(void *par, int dx, int dy, unsigned int width,
                           unsigned int height, int image_depth, void *drawing)
{
   int stride = info.fix.line_length;
   u32 bpp = info.var.bits_per_pixel;
   u8 *image = drawing;
   int i, num_longs, left_overs, fw = (width + 7) >> 3;
   u32 fmt = stride | ((bpp + ((bpp == 8) ? 0 : 8)) << 13); 
   u32 fgx, bgx;

   banshee_make_room(8+((height*fw + 3) >> 2));
   tdfx_outl(COLORFORE, fgx);
   tdfx_outl(COLORBACK, bgx);
   tdfx_outl(SRCXY,     0);
   tdfx_outl(DSTXY,     dx | (dy << 16));
   tdfx_outl(COMMAND_2D, COMMAND_2D_H2S_BITBLT | (ROP_COPY << 24));
   tdfx_outl(SRCFORMAT, 0x400000);
   tdfx_outl(DSTFORMAT, fmt);
   tdfx_outl(DSTSIZE,   width | (height << 16));
   
   /* First we have to see how many longs (32 bits) the image is in length */ 
   num_longs = (height * fw) >> 2; 			 
   left_overs = (height * fw)%4;	

   for (i = num_longs; i > 0; i--) {
         tdfx_outl(LAUNCH_2D, *(u32*)image);
         image=+4;
   }
   switch (left_overs) {
    case 0: break;
    case 1:  tdfx_outl(LAUNCH_2D,*image); break;
    case 2:  tdfx_outl(LAUNCH_2D,*(u16*)image); break;
    case 3:  tdfx_outl(LAUNCH_2D,*(u16*)image | ((image[3]) << 24)); 
      break;
   }
   banshee_wait_idle();
}

static void do_write_regs(struct banshee_reg* reg) {
  int i;

  banshee_wait_idle();

  tdfx_outl(MISCINIT1, tdfx_inl(MISCINIT1) | 0x01);

  crt_outb(0x11, crt_inb(0x11) & 0x7f); /* CRT unprotect */

  banshee_make_room(3);
  tdfx_outl(VGAINIT1,      reg->vgainit1 &  0x001FFFFF);
  tdfx_outl(VIDPROCCFG,    reg->vidcfg   & ~0x00000001);
#if 0
  tdfx_outl(PLLCTRL1,      reg->mempll);
  tdfx_outl(PLLCTRL2,      reg->gfxpll);
#endif
  tdfx_outl(PLLCTRL0,      reg->vidpll);

  vga_outb(MISC_W, reg->misc[0x00] | 0x01);

  for(i = 0; i < 5; i++)
    seq_outb(i, reg->seq[i]);

  for(i = 0; i < 25; i++)
    crt_outb(i, reg->crt[i]);

  for(i = 0; i < 9; i++)
    gra_outb(i, reg->gra[i]);

  for(i = 0; i < 21; i++)
    att_outb(i, reg->att[i]);

  crt_outb(0x1a, reg->ext[0]);
  crt_outb(0x1b, reg->ext[1]);

  vga_enable_palette();
  vga_enable_video();

  banshee_make_room(11);
  tdfx_outl(VGAINIT0,      reg->vgainit0);
  tdfx_outl(DACMODE,       reg->dacmode);
  tdfx_outl(VIDDESKSTRIDE, reg->stride);
  if (nohwcursor) {
     tdfx_outl(HWCURPATADDR,  0);
  } else {
     tdfx_outl(HWCURPATADDR,  reg->curspataddr);
     tdfx_outl(HWCURC0,       reg->cursc0);
     tdfx_outl(HWCURC1,       reg->cursc1);
     tdfx_outl(HWCURLOC,      reg->cursloc);
  }
   
  tdfx_outl(VIDSCREENSIZE, reg->screensize);
  tdfx_outl(VIDDESKSTART,  reg->startaddr);
  tdfx_outl(VIDPROCCFG,    reg->vidcfg);
  tdfx_outl(VGAINIT1,      reg->vgainit1);  

  banshee_make_room(8);
  tdfx_outl(SRCBASE,         reg->srcbase);
  tdfx_outl(DSTBASE,         reg->dstbase);
  tdfx_outl(COMMANDEXTRA_2D, 0);
  tdfx_outl(CLIP0MIN,        0);
  tdfx_outl(CLIP0MAX,        0x0fff0fff);
  tdfx_outl(CLIP1MIN,        0);
  tdfx_outl(CLIP1MAX,        0x0fff0fff);
  tdfx_outl(SRCXY, 0);

  banshee_wait_idle();
}

static unsigned long do_lfb_size(void) {
  u32 draminit0 = 0;
  u32 draminit1 = 0;
  u32 miscinit1 = 0;
  u32 lfbsize   = 0;
  int sgram_p     = 0;

  draminit0 = tdfx_inl(DRAMINIT0);  
  draminit1 = tdfx_inl(DRAMINIT1);
   
  sgram_p = (draminit1 & DRAMINIT1_MEM_SDRAM) ? 0 : 1;
  
  lfbsize = sgram_p ?
    (((draminit0 & DRAMINIT0_SGRAM_NUM)  ? 2 : 1) * 
     ((draminit0 & DRAMINIT0_SGRAM_TYPE) ? 8 : 4) * 1024 * 1024) :
    16 * 1024 * 1024;

  /* disable block writes for SDRAM (why?) */
  miscinit1 = tdfx_inl(MISCINIT1);
  miscinit1 |= sgram_p ? 0 : MISCINIT1_2DBLOCK_DIS;
  miscinit1 |= MISCINIT1_CLUT_INV;

  banshee_make_room(1); 
  tdfx_outl(MISCINIT1, miscinit1);

  return lfbsize;
}

static int tdfxfb_open(struct fb_info *info, int user) 
{
  MOD_INC_USE_COUNT;
  return(0);
}

static int tdfxfb_release(struct fb_info *info, int user) 
{
  MOD_DEC_USE_COUNT;
  return(0);
}

static int tdfxfb_decode_var(struct fb_var_screeninfo *var,
			     struct tdfxfb_par *par,
			     struct fb_info *info) 
{
  if(var->bits_per_pixel != 8  &&
     var->bits_per_pixel != 16 &&
     var->bits_per_pixel != 24 &&
     var->bits_per_pixel != 32) {
    DPRINTK("depth not supported: %u\n", var->bits_per_pixel);
    return -EINVAL;
  }

  if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
    DPRINTK("interlace not supported\n");
    return -EINVAL;
  }

  if(var->xoffset) {
    DPRINTK("xoffset not supported\n");
    return -EINVAL;
  }

  if(var->xres != var->xres_virtual) {
    DPRINTK("virtual x resolution != physical x resolution not supported\n");
    return -EINVAL;
  }

  if(var->yres > var->yres_virtual) {
    DPRINTK("virtual y resolution < physical y resolution not possible\n");
    return -EINVAL;
  }

  /* fixme: does Voodoo3 support interlace? Banshee doesn't */
  if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
    DPRINTK("interlace not supported\n");
    return -EINVAL;
  }

  memset(par, 0, sizeof(struct tdfxfb_par));

  par->width       = (var->xres + 15) & ~15; /* could sometimes be 8 */
  par->width_virt  = par->width;
  par->height      = var->yres;
  par->height_virt = var->yres_virtual;
  par->bpp         = var->bits_per_pixel;
  par->ppitch      = var->bits_per_pixel;
  par->lpitch      = par->width* ((par->ppitch+7)>>3);
  par->cmap_len    = (par->bpp == 8) ? 256 : 16;
     
  par->baseline = 0;

  if(par->width < 320 || par->width > 2048) {
     DPRINTK("width not supported: %u\n", par->width);
     return -EINVAL;
  }
  if(par->height < 200 || par->height > 2048) {
     DPRINTK("height not supported: %u\n", par->height);
     return -EINVAL;
  }
  if(par->lpitch*par->height_virt > info->fix.smem_len) {
     DPRINTK("no memory for screen (%ux%ux%u)\n",
     par->width, par->height_virt, par->bpp);
     return -EINVAL;
  }
  par->pixclock = PICOS2KHZ(var->pixclock);
  if(par->pixclock > par->max_pixclock) {
     DPRINTK("pixclock too high (%uKHz)\n", par->pixclock);
     return -EINVAL;
  }

  par->hdispend = var->xres;
  par->hsyncsta = par->hdispend + var->right_margin;
  par->hsyncend = par->hsyncsta + var->hsync_len;
  par->htotal   = par->hsyncend + var->left_margin;

  par->vdispend = var->yres;
  par->vsyncsta = par->vdispend + var->lower_margin;
  par->vsyncend = par->vsyncsta + var->vsync_len;
  par->vtotal   = par->vsyncend + var->upper_margin;

  if(var->sync & FB_SYNC_HOR_HIGH_ACT)
    par->video |= TDFXF_HSYNC_ACT_HIGH;
  else {
    par->video |= TDFXF_HSYNC_ACT_LOW;
    if(var->sync & FB_SYNC_VERT_HIGH_ACT)
      par->video |= TDFXF_VSYNC_ACT_HIGH;
    else
      par->video |= TDFXF_VSYNC_ACT_LOW;
    if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
      par->video |= TDFXF_LINE_DOUBLE;
    if(var->activate == FB_ACTIVATE_NOW)
      par->video |= TDFXF_VIDEO_ENABLE;
  }
  return 0;
}

static int  tdfxfb_encode_var(struct fb_var_screeninfo *var,
                              struct tdfxfb_par *par,
                              struct fb_info *info)
{
  struct fb_var_screeninfo v;

  memset(&v, 0, sizeof(struct fb_var_screeninfo));
  v.xres_virtual   = par->width_virt;
  v.yres_virtual   = par->height_virt;
  v.xres           = par->width;
  v.yres           = par->height;
  v.right_margin   = par->hsyncsta - par->hdispend;
  v.hsync_len      = par->hsyncend - par->hsyncsta;
  v.left_margin    = par->htotal   - par->hsyncend;
  v.lower_margin   = par->vsyncsta - par->vdispend;
  v.vsync_len      = par->vsyncend - par->vsyncsta;
  v.upper_margin   = par->vtotal   - par->vsyncend;
  v.bits_per_pixel = par->bpp;
  switch(par->bpp) {
  case 8:
    v.red.length = v.green.length = v.blue.length = 8;
    break;
  case 16:
    v.red.offset   = 11;
    v.red.length   = 5;
    v.green.offset = 5;
    v.green.length = 6;
    v.blue.offset  = 0;
    v.blue.length  = 5;
    break;
  case 24:
    v.red.offset=16;
    v.green.offset=8;
    v.blue.offset=0;
    v.red.length = v.green.length = v.blue.length = 8;
  case 32:
    v.red.offset   = 16;
    v.green.offset = 8;
    v.blue.offset  = 0;
    v.red.length = v.green.length = v.blue.length = 8;
    break;
  }
  v.height = v.width = -1;
  v.pixclock = KHZ2PICOS(par->pixclock);
  if((par->video & TDFXF_HSYNC_MASK) == TDFXF_HSYNC_ACT_HIGH)
    v.sync |= FB_SYNC_HOR_HIGH_ACT;
  if((par->video & TDFXF_VSYNC_MASK) == TDFXF_VSYNC_ACT_HIGH)
    v.sync |= FB_SYNC_VERT_HIGH_ACT;
  if(par->video & TDFXF_LINE_DOUBLE)
    v.vmode = FB_VMODE_DOUBLE;

  var->accel_flags &= FB_ACCELF_TEXT;	

  *var = v;
  return 0;
}

static void tdfxfb_set_par(struct tdfxfb_par* par,
			   struct fb_info *info) {
  struct banshee_reg reg;
  u32 cpp;
  u32 hd, hs, he, ht, hbs, hbe;
  u32 vd, vs, ve, vt, vbs, vbe;
  u32 wd;
  int fout;
  int freq;
   
  memset(&reg, 0, sizeof(reg));

  cpp = (par->bpp + 7)/8;
  
  wd = (par->hdispend >> 3) - 1;

  hd  = (par->hdispend >> 3) - 1;
  hs  = (par->hsyncsta >> 3) - 1;
  he  = (par->hsyncend >> 3) - 1;
  ht  = (par->htotal   >> 3) - 1;
  hbs = hd;
  hbe = ht;

  vd  = par->vdispend - 1;
  vs  = par->vsyncsta - 1;
  ve  = par->vsyncend - 1;
  vt  = par->vtotal   - 2;
  vbs = vd;
  vbe = vt;
  
  /* this is all pretty standard VGA register stuffing */
  reg.misc[0x00] = 
    0x0f |
    (par->hdispend < 400 ? 0xa0 :
     par->hdispend < 480 ? 0x60 :
     par->hdispend < 768 ? 0xe0 : 0x20);
     
  reg.gra[0x00] = 0x00;
  reg.gra[0x01] = 0x00;
  reg.gra[0x02] = 0x00;
  reg.gra[0x03] = 0x00;
  reg.gra[0x04] = 0x00;
  reg.gra[0x05] = 0x40;
  reg.gra[0x06] = 0x05;
  reg.gra[0x07] = 0x0f;
  reg.gra[0x08] = 0xff;

  reg.att[0x00] = 0x00;
  reg.att[0x01] = 0x01;
  reg.att[0x02] = 0x02;
  reg.att[0x03] = 0x03;
  reg.att[0x04] = 0x04;
  reg.att[0x05] = 0x05;
  reg.att[0x06] = 0x06;
  reg.att[0x07] = 0x07;
  reg.att[0x08] = 0x08;
  reg.att[0x09] = 0x09;
  reg.att[0x0a] = 0x0a;
  reg.att[0x0b] = 0x0b;
  reg.att[0x0c] = 0x0c;
  reg.att[0x0d] = 0x0d;
  reg.att[0x0e] = 0x0e;
  reg.att[0x0f] = 0x0f;
  reg.att[0x10] = 0x41;
  reg.att[0x11] = 0x00;
  reg.att[0x12] = 0x0f;
  reg.att[0x13] = 0x00;
  reg.att[0x14] = 0x00;

  reg.seq[0x00] = 0x03;
  reg.seq[0x01] = 0x01; /* fixme: clkdiv2? */
  reg.seq[0x02] = 0x0f;
  reg.seq[0x03] = 0x00;
  reg.seq[0x04] = 0x0e;

  reg.crt[0x00] = ht - 4;
  reg.crt[0x01] = hd;
  reg.crt[0x02] = hbs;
  reg.crt[0x03] = 0x80 | (hbe & 0x1f);
  reg.crt[0x04] = hs;
  reg.crt[0x05] = 
    ((hbe & 0x20) << 2) | 
    (he & 0x1f);
  reg.crt[0x06] = vt;
  reg.crt[0x07] = 
    ((vs & 0x200) >> 2) |
    ((vd & 0x200) >> 3) |
    ((vt & 0x200) >> 4) |
    0x10 |
    ((vbs & 0x100) >> 5) |
    ((vs  & 0x100) >> 6) |
    ((vd  & 0x100) >> 7) |
    ((vt  & 0x100) >> 8);
  reg.crt[0x08] = 0x00;
  reg.crt[0x09] = 
    0x40 |
    ((vbs & 0x200) >> 4);
  reg.crt[0x0a] = 0x00;
  reg.crt[0x0b] = 0x00;
  reg.crt[0x0c] = 0x00;
  reg.crt[0x0d] = 0x00;
  reg.crt[0x0e] = 0x00;
  reg.crt[0x0f] = 0x00;
  reg.crt[0x10] = vs;
  reg.crt[0x11] = 
    (ve & 0x0f) |
    0x20;
  reg.crt[0x12] = vd;
  reg.crt[0x13] = wd;
  reg.crt[0x14] = 0x00;
  reg.crt[0x15] = vbs;
  reg.crt[0x16] = vbe + 1; 
  reg.crt[0x17] = 0xc3;
  reg.crt[0x18] = 0xff;
  
  /* Banshee's nonvga stuff */
  reg.ext[0x00] = (((ht  & 0x100) >> 8) | 
		   ((hd  & 0x100) >> 6) |
		   ((hbs & 0x100) >> 4) |
		   ((hbe &  0x40) >> 1) |
		   ((hs  & 0x100) >> 2) |
		   ((he  &  0x20) << 2)); 
  reg.ext[0x01] = (((vt  & 0x400) >> 10) |
		   ((vd  & 0x400) >>  8) | 
		   ((vbs & 0x400) >>  6) |
		   ((vbe & 0x400) >>  4));
  
  reg.vgainit0 = 
    VGAINIT0_8BIT_DAC     |
    VGAINIT0_EXT_ENABLE   |
    VGAINIT0_WAKEUP_3C3   |
    VGAINIT0_ALT_READBACK |
    VGAINIT0_EXTSHIFTOUT;
  reg.vgainit1 = tdfx_inl(VGAINIT1) & 0x1fffff;

  reg.vidcfg = 
    VIDCFG_VIDPROC_ENABLE |
    VIDCFG_DESK_ENABLE    |
    VIDCFG_CURS_X11 |
    ((cpp - 1) << VIDCFG_PIXFMT_SHIFT) |
    (cpp != 1 ? VIDCFG_CLUT_BYPASS : 0);
  
  par->cursor.enable = reg.vidcfg | VIDCFG_HWCURSOR_ENABLE;
  par->cursor.disable = reg.vidcfg;
   
  reg.stride    = par->width*cpp;
  reg.cursloc   = 0;
   
  reg.cursc0    = 0; 
  reg.cursc1    = 0xffffff;
   
  reg.curspataddr = par->cursor.cursorimage;   
  
  reg.startaddr = par->baseline*reg.stride;
  reg.srcbase   = reg.startaddr;
  reg.dstbase   = reg.startaddr;

  /* PLL settings */
  freq = par->pixclock;

  reg.dacmode &= ~DACMODE_2X;
  reg.vidcfg  &= ~VIDCFG_2X;
  if(freq > par->max_pixclock/2) {
    freq = freq > par->max_pixclock ? par->max_pixclock : freq;
    reg.dacmode |= DACMODE_2X;
    reg.vidcfg  |= VIDCFG_2X;
  }
  reg.vidpll = do_calc_pll(freq, &fout);
#if 0
  reg.mempll = do_calc_pll(..., &fout);
  reg.gfxpll = do_calc_pll(..., &fout);
#endif

  reg.screensize = par->width | (par->height << 12);
  reg.vidcfg &= ~VIDCFG_HALF_MODE;

  do_write_regs(&reg);
	
  info->fix.visual = (info->var.bits_per_pixel == 8)
                       ? FB_VISUAL_PSEUDOCOLOR
                       : FB_VISUAL_DIRECTCOLOR;

  info->fix.ypanstep    = nopan ? 0 : 1;
  info->fix.ywrapstep   = nowrap ? 0 : 1;

  info->var.accel_flags &= FB_ACCELF_TEXT;
}
 
static int tdfxfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue,unsigned transp,struct fb_info *info) 
{
   u32 rgbcol;

   if (regno >= info->cmap.len) return 1;

   /* grayscale works only partially under directcolor */
   if (info->var.grayscale) {
      /* grayscale = 0.30*R + 0.59*G + 0.11*B */
      red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
   }

   switch (info->fix.visual) {
	case FB_VISUAL_PSEUDOCOLOR:
		/* This is only 8 bpp colro depth */
		rgbcol=(((u32) red   & 0xff00) << 8) |
		       (((u32) green & 0xff00) << 0) |
       		       (((u32) blue  & 0xff00) >> 8);
		banshee_make_room(2);
		tdfx_outl(DACADDR, regno);
		tdfx_outl(DACDATA, rgbcol);
		break;
   	/* Truecolor has hardware independent palette. This is all other bpp
	   for this video card */
   	case FB_VISUAL_TRUECOLOR:
	        rgbcol = (red << info->var.red.offset) |
		         (green << info->var.green.offset) |
		         (blue << info->var.blue.offset) |
		         (transp << info->var.transp.offset);
		if (info->var.bits_per_pixel <= 16)
			((u16*)(info->pseudo_palette))[regno] = rgbcol;
		else
			((u32*)(info->pseudo_palette))[regno] = rgbcol;
		return 0;
   }
   return 0;
}

static int tdfxfb_pan_display(struct fb_var_screeninfo *var, 
			      struct fb_info *info) 
{
  u32 addr;

  if(nopan)                return -EINVAL;
  if(var->xoffset)         return -EINVAL;
  if(var->yoffset > var->yres_virtual)   return -EINVAL;
  if(nowrap && 
     (var->yoffset + var->yres > var->yres_virtual)) return -EINVAL;
  
  addr = var->yoffset * info->fix.line_length;
  banshee_make_room(1);
  tdfx_outl(VIDDESKSTART, addr);
 
  info->var.xoffset = var->xoffset;
  info->var.yoffset = var->yoffset; 
  return 0;
}

/* 0 unblank, 1 blank, 2 no vsync, 3 no hsync, 4 off */
static int tdfxfb_blank(int blank, struct fb_info *fb) 
{
  u32 dacmode, state = 0, vgablank = 0;

  dacmode = tdfx_inl(DACMODE);

  switch(blank) {
  case 0: /* Screen: On; HSync: On, VSync: On */    
    state    = 0;
    vgablank = 0;
    break;
  case 1: /* Screen: Off; HSync: On, VSync: On */
    state    = 0;
    vgablank = 1;
    break;
  case 2: /* Screen: Off; HSync: On, VSync: Off */
    state    = BIT(3);
    vgablank = 1;
    break;
  case 3: /* Screen: Off; HSync: Off, VSync: On */
    state    = BIT(1);
    vgablank = 1;
    break;
  case 4: /* Screen: Off; HSync: Off, VSync: Off */
    state    = BIT(1) | BIT(3);
    vgablank = 1;
    break;
  }

  dacmode &= ~(BIT(1) | BIT(3));
  dacmode |= state;
  banshee_make_room(1); 
  tdfx_outl(DACMODE, dacmode);
  if(vgablank) 
    vga_disable_video();
  else
    vga_enable_video();

  return 0;
}

int __init tdfxfb_init(void) 
{
  struct pci_dev *pdev = NULL;

  while ((pdev = pci_find_device(PCI_VENDOR_ID_3DFX, PCI_ANY_ID, pdev))) {
    if(((pdev->class >> 16) == PCI_BASE_CLASS_DISPLAY) &&
       ((pdev->device == PCI_DEVICE_ID_3DFX_BANSHEE) ||
	(pdev->device == PCI_DEVICE_ID_3DFX_VOODOO3))) {
      char *name = pdev->device == PCI_DEVICE_ID_3DFX_BANSHEE
	? "Banshee"
	: "Voodoo3";

      par.max_pixclock = 
	pdev->device == PCI_DEVICE_ID_3DFX_BANSHEE 
	? BANSHEE_MAX_PIXCLOCK
	: VOODOO3_MAX_PIXCLOCK;

      /* Configure the default fb_screeninfo_fix first */
      info.fix = tdfx_fix;

      info.fix.mmio_start = pdev->resource[0].start;
      info.fix.mmio_len = 1 << 24;
      par.regbase_virt = 
	(u32)ioremap_nocache(info.fix.smem_start, 1 << 24);
      if(!par.regbase_virt) {
	printk("fb: Can't remap %s register area.\n", name);
	return -ENXIO;
      }
      
      info.fix.smem_start = pdev->resource[1].start;
      if(!(info.fix.smem_len = do_lfb_size())) {
	iounmap((void*)par.regbase_virt);
	printk("fb: Can't count %s memory.\n", name);
	return -ENXIO;
      }
      info.screen_base = ioremap_nocache(info.fix.smem_start, 
					 info.fix.smem_len);
      if(!info.screen_base) {
	printk("fb: Can't remap %s framebuffer.\n", name);
	iounmap((void*)info.screen_base);
	return -ENXIO;
      }

      printk("fb: %s memory = %ldK\n", name, info.fix.smem_len >> 10);

      par.iobase = pdev->resource[2].start;
      
#ifdef CONFIG_MTRR
       if (!nomtrr) {
	  if (mtrr_add(info.fix.smem_start, info.fix.smem_len, 
		       MTRR_TYPE_WRCOMB, 1)>=0)
	    printk("fb: MTRR's  turned on\n");
       }
#endif	  	 

      /* clear framebuffer memory */
      memset_io(info.screen_base, 0, info.fix.smem_len);
     
      /* Need to define a cursor API */
#if 0
      if (!nohwcursor) tdfxfb_hwcursor_init();
       
      par.cursor.timer.function = do_flashcursor; 
      par.cursor.state = CM_ERASE;
      par.cursor.timer.prev = par.cursor.timer.next=NULL;
      par.cursor.timer.data = (unsigned long)(&info);
      spin_lock_init(&par.DAClock);
#endif
       
      strcat(info.fix.id, name);
      
      info.changevar  = NULL;
      info.node       = -1;
      info.fbops      = &tdfxfb_ops;
      info.switch_con = &tdfxfb_switch_con;
      info.updatevar  = &tdfxfb_updatevar;
      info.flags      = FBINFO_FLAG_DEFAULT;
      
      if(!mode_option || 
	 !fb_find_mode(&info.var, &info, mode_option, NULL, 0, NULL, 8)) {
    	 info.var = tdfx_var;
      
      if(tdfxfb_decode_var(&info.var, fb_info.par, &info)) {
	/* ugh -- can't use the mode from the mode db. (or command line),
	   so try the default */
	
	printk("tdfxfb: "
	       "can't decode the supplied video mode, using default\n");
	      
	if(tdfxfb_decode_var(&info.var, &par, &info)) {
	  /* this is getting really bad!... */
	  printk("tdfxfb: can't decode default video mode\n");
	  return -ENXIO;
	}
      }
    
      if (noaccel) info.var.accel_flags &= ~FB_ACCELF_TEXT;
      else info.var.accel_flags |= FB_ACCELF_TEXT;
            
      if(register_framebuffer(&info) < 0) {
	printk("tdfxfb: can't register framebuffer\n");
	return -ENXIO;
      }

      printk("fb%d: %s frame buffer device\n", GET_FB_IDX(info.node),
	     info.modename);
      
      MOD_INC_USE_COUNT;
      return 0;
      }
    }
  }
  return -ENXIO;
}

void tdfxfb_setup(char *options, int *ints) 
{
  char *this_opt;

  if(!options || !*options)
    return;

  for(this_opt = strtok(options, ","); 
      this_opt;
      this_opt = strtok(NULL, ",")) {
    if(!strcmp(this_opt, "inverse")) {
      inverse = 1;
      fb_invert_cmaps();
    } else if(!strcmp(this_opt, "noaccel")) {
      noaccel = nopan = nowrap = nohwcursor = 1; 
    } else if(!strcmp(this_opt, "nopan")) {
      nopan = 1;
    } else if(!strcmp(this_opt, "nowrap")) {
      nowrap = 1;
    } else if (!strcmp(this_opt, "nohwcursor")) {
      nohwcursor = 1;
#ifdef CONFIG_MTRR
    } else if (!strcmp(this_opt, "nomtrr")) {
      nomtrr = 1;
#endif
    } else {	
      mode_option = this_opt;
    }
  } 
}

static int tdfxfb_switch_con(int con, struct fb_info *info) 
{
   struct tdfxfb_par *par = (struct tdfxfb_par *) info->par;	

   /* Do we have to save the colormap? */
   if(fb_display[fg_console].cmap.len)
       // fb_get_cmap(&fb_display[fg_console].cmap, 1, fb);
   
   fb_display[fg_console].var.activate = FB_ACTIVATE_NOW; 
   tdfxfb_decode_var(&fb_display[con].var, par, info);
   tdfxfb_set_par(par, info);
   if (fb_display[fg_console].dispsw && fb_display[con].conp)
     fb_con.con_cursor(fb_display[con].conp, CM_ERASE);
   
   del_timer(&(par->cursor.timer));
   par->cursor.state=CM_ERASE; 
   
#if 0
   if (!nohwcursor) 
     if (fb_display[con].conp)
       tdfxfb_createcursor( &fb_display[con] );
#endif   
   par->cursor.redraw=1;

#if 0   
   tdfxfb_install_cmap(&fb_display[con], info);
#endif
   tdfxfb_updatevar(con, info);
   
   return 1;
}

static int  tdfxfb_updatevar(int con, struct fb_info *info) 
{
   int err;	

   if (!nopan && info->fbops->fb_pan_display) {
        if ((err = info->fbops->fb_pan_display(&info->var, info)))
             return err;
   }
   return 0;
}

/* Hardware Cursor support */

#if 0

static void  do_flashcursor(unsigned long ptr);
static void tdfxfb_hwcursor_init(void);
static void tdfxfb_createcursorshape(struct display* p);
static void tdfxfb_createcursor(struct display * p);

/*
 * Invert the hardware cursor image (timerfunc)
 */
static void do_flashcursor(unsigned long ptr)
{
   struct tdfx_par *i = (struct tdfx_par *)ptr;
   spin_lock(&i->DAClock);
   banshee_make_room(1);
   tdfx_outl( VIDPROCCFG, tdfx_inl(VIDPROCCFG) ^ VIDCFG_HWCURSOR_ENABLE );
   i->cursor.timer.expires=jiffies+HZ/2;
   add_timer(&i->cursor.timer);
   spin_unlock(&i->DAClock);
}

static void tdfx_cfbX_cursor(struct display *p, int mode, int x, int y) 
{
   unsigned long flags;
   int tip;
   struct fb_info *info=(struct fb_info *)p->fb_info;
   struct tdfxfb_par *par = (struct tdfxfb_par *) fb_info->par; 
     
   tip = p->conp->vc_cursor_type & CUR_HWMASK;
   if (mode == CM_ERASE) {
	if (par->cursor.state != CM_ERASE) {
	     spin_lock_irqsave(&par->DAClock, flags);
	     par->cursor.state=CM_ERASE;
	     del_timer(&(par->cursor.timer));
	     tdfx_outl(VIDPROCCFG, par->cursor.disable); 
	     spin_unlock_irqrestore(&par->DAClock, flags);
	}
	return;
   }
   if ((p->conp->vc_cursor_type & CUR_HWMASK) != par->cursor.type)
	 tdfxfb_createcursor(p);
   x *= fontwidth(p);
   y *= fontheight(p);
   y -= info->var.yoffset;
   spin_lock_irqsave(&par->DAClock, flags);
   if ((x! = par->cursor.x) || (y! = par->cursor.y) || (par->cursor.redraw)) {
          par->cursor.x=x;
	  par->cursor.y=y;
	  par->cursor.redraw=0;
	  x += 63;
	  y += 63;    
          banshee_make_room(2);
	  tdfx_outl(VIDPROCCFG, par->cursor.disable);
	  tdfx_outl(HWCURLOC, (y << 16) + x);
   }
   par->cursor.state = CM_DRAW;
   mod_timer(&par->cursor.timer,jiffies+HZ/2);
   banshee_make_room(1);
   tdfx_outl(VIDPROCCFG, par->cursor.enable);
   spin_unlock_irqrestore(&par->DAClock, flags);
   return;     
}

static void tdfxfb_createcursorshape(struct display* p) 
{
   struct tdfxfb_par *par = (struct tdfxfb_par *) info.par;	

   unsigned int h,cu,cd;
   
   h=fontheight(p);
   cd=h;
   if (cd >= 10) cd --; 
   par->cursor.type = p->conp->vc_cursor_type & CUR_HWMASK;
   switch (par->cursor.type) {
      case CUR_NONE: 
	cu=cd; 
	break;
      case CUR_UNDERLINE: 
	cu=cd - 2; 
	break;
      case CUR_LOWER_THIRD: 
	cu=(h * 2) / 3; 
	break;
      case CUR_LOWER_HALF: 
	cu=h / 2; 
	break;
      case CUR_TWO_THIRDS: 
	cu=h / 3; 
	break;
      case CUR_BLOCK:
      default:
	cu=0;
	cd = h;
	break;
   }
   par->cursor.w=fontwidth(p);
   par->cursor.u=cu;
   par->cursor.d=cd;
}
   
static void tdfxfb_createcursor(struct display *p)
{
   struct tdfxfb_par *par = (struct tdfxfb_par *) info.par;	
   u8 *cursorbase;
   u32 xline;
   unsigned int i;
   unsigned int h,to;

   tdfxfb_createcursorshape(p);
   xline = (1 << par->cursor.w)-1;
   cursorbase = (u8*) par->bufbase_virt;
   h = par->cursor.cursorimage;     
   
   to = par->cursor.u;
   for (i = 0; i < to; i++) {
	writel(0, cursorbase+h);
	writel(0, cursorbase+h+4);
	writel(~0, cursorbase+h+8);
	writel(~0, cursorbase+h+12);
	h += 16;
   }
   
   to = par->cursor.d;
   
   for (; i < to; i++) {
	writel(xline, cursorbase+h);
	writel(0, cursorbase+h+4);
	writel(~0, cursorbase+h+8);
	writel(~0, cursorbase+h+12);
	h += 16;
   }
   
   for (; i < 64; i++) {
	writel(0, cursorbase+h);
	writel(0, cursorbase+h+4);
	writel(~0, cursorbase+h+8);
	writel(~0, cursorbase+h+12);
	h += 16;
   }
}
   
static void tdfxfb_hwcursor_init(void)
{
   struct tdfxfb_par *par = (struct tdfxfb_par *) info.par;

   unsigned int start;
   start = (info.fix.smem_len - 1024) & PAGE_MASK;
   info.fix.smem_len = start; 
   par->cursor.cursorimage = info.fix.smem_len;
   printk("tdfxfb: reserving 1024 bytes for the hwcursor at 0x%08lx\n",
	  par->regbase_virt + par->cursor.cursorimage);
}
#endif
