/*
 * linux/drivers/video/dnfb.c -- Apollo frame buffer device
 *
 * Copyright (C) 2000
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/apollohw.h>
#include <linux/fb.h>
#include <linux/module.h>
#include "dn_accel.h"
#include <video/fbcon.h>
#include <video/fbcon-mfb.h>

/* apollo video HW definitions */

/*
 * Control Registers.   IOBASE + $x
 *
 * Note: these are the Memory/IO BASE definitions for a mono card set to the
 * alternate address
 *
 * Control 3A and 3B serve identical functions except that 3A
 * deals with control 1 and 3b deals with Color LUT reg.
 */

#define AP_IOBASE       0x3b0          /* Base address of 1 plane board. */
#define AP_STATUS       isaIO2mem(AP_IOBASE+0) /* Status register.  Read */
#define AP_WRITE_ENABLE isaIO2mem(AP_IOBASE+0) /* Write Enable Register Write */
#define AP_DEVICE_ID    isaIO2mem(AP_IOBASE+1) /* Device ID Register. Read */
#define AP_ROP_1        isaIO2mem(AP_IOBASE+2) /* Raster Operation reg. Write Word */
#define AP_DIAG_MEM_REQ isaIO2mem(AP_IOBASE+4) /* Diagnostic Memory Request. Write Word */
#define AP_CONTROL_0    isaIO2mem(AP_IOBASE+8) /* Control Register 0.  Read/Write */
#define AP_CONTROL_1    isaIO2mem(AP_IOBASE+0xa) /* Control Register 1.  Read/Write */
#define AP_CONTROL_3A   isaIO2mem(AP_IOBASE+0xe) /* Control Register 3a. Read/Write */
#define AP_CONTROL_2    isaIO2mem(AP_IOBASE+0xc) /* Control Register 2. Read/Write */


#define FRAME_BUFFER_START 0x0FA0000
#define FRAME_BUFFER_LEN 0x40000

/* CREG 0 */
#define VECTOR_MODE 0x40 /* 010x.xxxx */
#define DBLT_MODE   0x80 /* 100x.xxxx */
#define NORMAL_MODE 0xE0 /* 111x.xxxx */
#define SHIFT_BITS  0x1F /* xxx1.1111 */
        /* other bits are Shift value */

/* CREG 1 */
#define AD_BLT      0x80 /* 1xxx.xxxx */
#define NORMAL      0x80 /* 1xxx.xxxx */   /* What is happening here ?? */
#define INVERSE     0x00 /* 0xxx.xxxx */   /* Clearing this reverses the screen */
#define PIX_BLT     0x00 /* 0xxx.xxxx */

#define AD_HIBIT        0x40 /* xIxx.xxxx */

#define ROP_EN          0x10 /* xxx1.xxxx */
#define DST_EQ_SRC      0x00 /* xxx0.xxxx */
#define nRESET_SYNC     0x08 /* xxxx.1xxx */
#define SYNC_ENAB       0x02 /* xxxx.xx1x */

#define BLANK_DISP      0x00 /* xxxx.xxx0 */
#define ENAB_DISP       0x01 /* xxxx.xxx1 */

#define NORM_CREG1      (nRESET_SYNC | SYNC_ENAB | ENAB_DISP) /* no reset sync */

/* CREG 2 */

/*
 * Following 3 defines are common to 1, 4 and 8 plane.
 */

#define S_DATA_1s   0x00 /* 00xx.xxxx */ /* set source to all 1's -- vector drawing */
#define S_DATA_PIX  0x40 /* 01xx.xxxx */ /* takes source from ls-bits and replicates over 16 bits */
#define S_DATA_PLN  0xC0 /* 11xx.xxxx */ /* normal, each data access =16-bits in
 one plane of image mem */

/* CREG 3A/CREG 3B */
#       define RESET_CREG 0x80 /* 1000.0000 */

/* ROP REG  -  all one nibble */
/*      ********* NOTE : this is used r0,r1,r2,r3 *********** */
#define ROP(r2,r3,r0,r1) ( (U_SHORT)((r0)|((r1)<<4)|((r2)<<8)|((r3)<<12)) )
#define DEST_ZERO               0x0
#define SRC_AND_DEST    0x1
#define SRC_AND_nDEST   0x2
#define SRC                             0x3
#define nSRC_AND_DEST   0x4
#define DEST                    0x5
#define SRC_XOR_DEST    0x6
#define SRC_OR_DEST             0x7
#define SRC_NOR_DEST    0x8
#define SRC_XNOR_DEST   0x9
#define nDEST                   0xA
#define SRC_OR_nDEST    0xB
#define nSRC                    0xC
#define nSRC_OR_DEST    0xD
#define SRC_NAND_DEST   0xE
#define DEST_ONE                0xF

#define SWAP(A) ((A>>8) | ((A&0xff) <<8))

#if 0
#define outb(a,d) *(char *)(a)=(d)
#define outw(a,d) *(unsigned short *)a=d
#endif

/* accel stuff */
#define USE_DN_ACCEL

static int currcon = 0;
static struct fb_info fb_info;
static struct display disp;
static struct display_switch dispsw_apollofb;

static struct fb_fix_screeninfo dnfb_fix __initdata = {
    "Apollo ", (FRAME_BUFFER_START+IO_BASE), FRAME_BUFFER_LEN, 
    FB_TYPE_PACKED_PIXELS, 0, FB_VISUAL_MONO10, 0, 0, 0, 256, 
    (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static struct fb_var_screeninfo dnfb_var __initdata = {
    /* 1280x1024, 1 bpp */
    1280, 1024, 2048, 1024, 0, 0, 1, 0,
    {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, 0},
    0, FB_ACTIVATE_NOW, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, FB_VMODE_NONINTERLACED
};

/* frame buffer operations */
int dnfb_init(void);
void dn_fb_setup(char *options, int *ints);

static int dn_fb_open(struct fb_info *info,int user);
static int dn_fb_release(struct fb_info *info,int user);
static int dn_fb_get_fix(struct fb_fix_screeninfo *fix, int con, 
			 struct fb_info *info);
static int dn_fb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int dn_fb_set_var(struct fb_var_screeninfo *var, int isactive,
			 struct fb_info *info);
static int dn_fb_get_cmap(struct fb_cmap *cmap,int kspc,int con,
			  struct fb_info *info);
static int dn_fb_set_cmap(struct fb_cmap *cmap,int kspc,int con,
			  struct fb_info *info);

static struct fb_ops dn_fb_ops = {
        fb_open:		dn_fb_open,
	fb_release:		dn_fb_release, 
	fb_get_fix:		dn_fb_get_fix, 
	fb_get_var:		dn_fb_get_var, 
	fb_set_var:		dn_fb_set_var,
        fb_get_cmap:		dn_fb_get_cmap, 
	fb_set_cmap:		dn_fb_set_cmap, 
};

static int dnfbcon_switch(int con,struct fb_info *info);
static int dnfbcon_updatevar(int con,struct fb_info *info);
static void dnfb_blank(int blank,struct fb_info *info);

static int dnfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                          u_int transp, struct fb_info *info);
static void dn_fb_set_disp(int con,struct fb_info *info);

static int dn_fb_open(struct fb_info *info,int user)
{
        /*
         * Nothing, only a usage count for the moment
         */

        MOD_INC_USE_COUNT;
        return(0);
}

static int dn_fb_release(struct fb_info *info,int user)
{
        MOD_DEC_USE_COUNT;
        return(0);
}

static int dn_fb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info) 
{
	*fix = info->fix;
	return 0;
}
        
static int dn_fb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info) 
{
	*var = info->var;
	return 0;
}

static int dn_fb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info) 
{
	return -EINVAL;
}

static int dn_fb_get_cmap(struct fb_cmap *cmap,int kspc,int con,
			  struct fb_info *info) 
{
	fb_copy_cmap(&info->cmap, cmap, kspc ? 0 : 1);
	return 0;
}

static int dn_fb_set_cmap(struct fb_cmap *cmap,int kspc,int con,
			  struct fb_info *info) 
{
	int err = 0;

        /* current console? */
        if (con == currcon) {
                if ((err = fb_set_cmap(cmap, kspc, dnfb_setcolreg, info))) {
                        return err;
                } else {
                        fb_copy_cmap(cmap, &info->cmap, kspc ? 0 : 1);
                }
        }
        /* Always copy colormap to fb_display. */
        fb_copy_cmap(cmap, &fb_display[con].cmap, kspc ? 0: 1);
        return err;
}

static int dnfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                          u_int transp, struct fb_info *info)
{
	/* Do nothing We support only mono at this time */
	return 0;
}

static void dn_fb_set_disp(int con, struct fb_info *info) 
{
  struct display *display;

  if (con>=0)
	display = &fb_display[con];
  else
	display = &disp;

  if(con==-1) 
    con=0;

   display->screen_base = (u_char *)info->fix.smem_start;
   display->visual = info->fix.visual;
   display->type = info->fix.type;
   display->type_aux = info->fix.type_aux;
   display->ypanstep = info->fix.ypanstep;
   display->ywrapstep = info->fix.ywrapstep;
   display->can_soft_blank = 1;
   display->inverse = 0;
   display->line_length = info->fix.line_length;
#ifdef FBCON_HAS_MFB
   display->dispsw = &fbcon_mfb;
#else
   display->dispsw = &fbcon_dummy;
#endif
}
  
int dnfb_init(void) 
{
	int err;

	fb_info.changevar=NULL;
	strcpy(&fb_info.modename[0],fb_info.fix.id);
	fb_info.fontname[0]=0;
	fb_info.disp = disp;
	fb_info.var = dnfb_var;
	fb_info.fix = dnfb_fix;
	fb_info.switch_con = &dnfbcon_switch;
	fb_info.updatevar = &dnfbcon_updatevar;
	fb_info.blank = &dnfb_blank;	
	fb_info.node = -1;
	fb_info.fbops = &dn_fb_ops;

	fb_alloc_cmap(&fb_info.cmap, 2, 0);
	fb_set_cmap(&fb_info.cmap, 1, dnfb_setcolreg, &fb_info);
	
	dn_fb_set_disp(-1, &fb_info);
	
	if (register_framebuffer(&fb_info) < 0)
                panic("unable to register apollo frame buffer\n"); 
                return 1;
	}
 
	/* now we have registered we can safely setup the hardware */
        outb(RESET_CREG,  AP_CONTROL_3A);
        outw(0x0,  AP_WRITE_ENABLE);
        outb(NORMAL_MODE, AP_CONTROL_0); 
        outb((AD_BLT | DST_EQ_SRC | NORM_CREG1),  AP_CONTROL_1);
        outb(S_DATA_PLN,  AP_CONTROL_2);
        outw(SWAP(0x3), AP_ROP_1);

        printk("apollo frame buffer alive and kicking !\n");
	return 0;
}	

	
static int dnfbcon_switch(int con, struct fb_info *info) 
{
        struct display *prev = &fb_display[currcon];
        struct display *new = &fb_display[con];

        currcon = con;
        /* Save current colormap */
        fb_copy_cmap(&prev->fb_info->cmap, &prev->cmap, 0);
        /* Install new colormap */
        new->fb_info->fbops->fb_set_cmap(&new->cmap, 0, con, new->fb_info); 
	return 0;
}

static int dnfbcon_updatevar(int con,  struct fb_info *info) 
{
	return 0;
}

static void dnfb_blank(int blank,  struct fb_info *info) 
{

	if (blank)  {
        	outb(0x0,  AP_CONTROL_3A);
	} else {
	        outb(0x1,  AP_CONTROL_3A);
	}
	return ;
}

void dn_fb_setup(char *options, int *ints) {
	return;
}

void dn_bitblt(struct display *p,int x_src,int y_src, int x_dest, int y_dest,
               int x_count, int y_count) 
{
	int incr,y_delta,pre_read=0,x_end,x_word_count;
	ushort *src,dummy;
	uint start_mask,end_mask,dest;
	short i,j;

	incr=(y_dest<=y_src) ? 1 : -1 ;

	src=(ushort *)(p->screen_base+ y_src*p->next_line+(x_src >> 4));
	dest=y_dest*(p->next_line >> 1)+(x_dest >> 4);
	
	if(incr>0) {
		y_delta=(p->next_line*8)-x_src-x_count;
		x_end=x_dest+x_count-1;
		x_word_count=(x_end>>4) - (x_dest >> 4) + 1;
		start_mask=0xffff0000 >> (x_dest & 0xf);
		end_mask=0x7ffff >> (x_end & 0xf);
		outb((((x_dest & 0xf) - (x_src &0xf))  % 16)|(0x4 << 5),AP_CONTROL_0);
		if((x_dest & 0xf) < (x_src & 0xf))
			pre_read=1;
	}
	else {
		y_delta=-((p->next_line*8)-x_src-x_count);
		x_end=x_dest-x_count+1;
		x_word_count=(x_dest>>4) - (x_end >> 4) + 1;
		start_mask=0x7ffff >> (x_dest & 0xf);
		end_mask=0xffff0000 >> (x_end & 0xf);
		outb(((-((x_src & 0xf) - (x_dest &0xf))) % 16)|(0x4 << 5),AP_CONTROL_0);
		if((x_dest & 0xf) > (x_src & 0xf))
			pre_read=1;
	}

	for(i=0;i<y_count;i++) {

		outb(0xc | (dest >> 16), AP_CONTROL_3A);
			
		if(pre_read) {
			dummy=*src;
			src+=incr;
		}

		if(x_word_count) {
			outb(start_mask,AP_WRITE_ENABLE);
			*src=dest;
			src+=incr;
			dest+=incr;
			outb(0,AP_WRITE_ENABLE);

			for(j=1;j<(x_word_count-1);j++) {
				*src=dest;
				src+=incr;	
				dest+=incr;
			}

			outb(start_mask,AP_WRITE_ENABLE);
			*src=dest;
			dest+=incr;
			src+=incr;
		}
		else {
			outb(start_mask | end_mask, AP_WRITE_ENABLE);
			*src=dest;
			dest+=incr;
			src+=incr;
		}
		src+=(y_delta/16);
		dest+=(y_delta/16);
	}
	outb(NORMAL_MODE,AP_CONTROL_0);
}

static void bmove_apollofb(struct display *p, int sy, int sx, int dy, int dx,
		      int height, int width)
{
    int fontheight,fontwidth;

    fontheight=fontheight(p);
    fontwidth=fontwidth(p);

#ifdef USE_DN_ACCEL
    dn_bitblt(p,sx,sy*fontheight,dx,dy*fontheight,width*fontwidth,
	      height*fontheight);
#else
    u_char *src, *dest;
    u_int rows;

    if (sx == 0 && dx == 0 && width == p->next_line) {
	src = p->screen_base+sy*fontheight*width;
	dest = p->screen_base+dy*fontheight*width;
	mymemmove(dest, src, height*fontheight*width);
    } else if (dy <= sy) {
	src = p->screen_base+sy*fontheight*next_line+sx;
	dest = p->screen_base+dy*fontheight*next_line+dx;
	for (rows = height*fontheight; rows--;) {
	    mymemmove(dest, src, width);
	    src += p->next_line;
	    dest += p->next_line;
	}
    } else {
	src = p->screen_base+((sy+height)*fontheight-1)*p->next_line+sx;
	dest = p->screen_base+((dy+height)*fontheight-1)*p->next_line+dx;
	for (rows = height*fontheight; rows--;) {
	    mymemmove(dest, src, width);
	    src -= p->next_line;
	    dest -= p->next_line;
	}
    }
#endif
}

static void clear_apollofb(struct vc_data *conp, struct display *p, int sy, int sx,
		      int height, int width)
{
	fbcon_mfb_clear(conp,p,sy,sx,height,width);
}

static void putc_apollofb(struct vc_data *conp, struct display *p, int c, int yy,
		     int xx)
{
	fbcon_mfb_putc(conp,p,c,yy,xx);
}

static void putcs_apollofb(struct vc_data *conp, struct display *p, const char *s,
		      int count, int yy, int xx)
{
	fbcon_mfb_putcs(conp,p,s,count,yy,xx);
}

static void rev_char_apollofb(struct display *p, int xx, int yy)
{
	fbcon_mfb_revc(p,xx,yy);
}

static struct display_switch dispsw_apollofb = {
    fbcon_mfb_setup, bmove_apollofb, clear_apollofb,
    putc_apollofb, putcs_apollofb, rev_char_apollofb
};
