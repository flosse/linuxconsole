/*
 *  linux/drivers/video/pm3fb.c -- 3DLabs Permedia3 frame buffer device
 *  
 *  Copyright (C) 2001 Romain Dolbeau <dolbeau@irisa.fr>
 *  Based on code written by:
 *           Sven Luther, <luther@dpt-info.u-strasbg.fr>
 *           Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *  Based on linux/drivers/video/skeletonfb.c:
 *	Copyright (C) 1997 Geert Uytterhoeven
 *      Copyright (C) 2001 James Simmons
 *  Based on linux/driver/video/pm2fb.c:
 *      Copyright (C) 1998-1999 Ilario Nardinocchi (nardinoc@CS.UniBO.IT)
 *      Copyright (C) 1999 Jakub Jelinek (jakub@redhat.com)
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  $Header$
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>

#include "fbcon.h"

#include <linux/pci.h>
#include <linux/ioport.h>

#include "pm3fb.h"

/* Driver name */
static const char permedia3_name[16] = "Permedia3";

/* complement to the fb_info struct, no longer integrated with FBCON fb_info */
struct pm3fb_info {
	struct pm3fb_par *current_par; /* current HW parameter */
	unsigned long board_num; /* internal board number */
	unsigned long use_current;
	struct pci_dev *dev;    /* PCI device */
	unsigned long board_type; /* index in the cardbase */
	unsigned char *fb_base;	/* framebuffer memory base */
	u32 fb_size;		/* framebuffer memory size */
	unsigned char *p_fb;	/* physical address of frame buffer */
	unsigned char *v_fb;	/* virtual address of frame buffer */
	unsigned char *pIOBase;	/* physical address of registers region, must be rg_base or rg_base+PM2_REGS_SIZE depending on the host endianness */
	unsigned char *vIOBase;	/* address of registers after ioremap() */
	u32 pseudo_palette[17];

	unsigned long memcaps;
	unsigned long memtimings;
	unsigned long memcontrol;
	unsigned long memrefresh;
	unsigned long mempowerdown;
};

/* the fb_par struct, mandatory */
struct pm3fb_par {
	u32 pixclock;		/* pixclock in KHz */

	u32 width;		/* width of virtual screen */
	u32 height;		/* height of virtual screen */

	u32 hsstart;		/* horiz. sync start */
	u32 hsend;		/* horiz. sync end */
	u32 hbend;		/* horiz. blank end (also gate end) */
	u32 htotal;		/* total width (w/ sync & blank) */

	u32 vsstart;		/* vert. sync start */
	u32 vsend;		/* vert. sync end */
	u32 vbend;		/* vert. blank end */
	u32 vtotal;		/* total height (w/ sync & blank) */

	u32 stride;		/* screen stride */
	u32 base;		/* screen base (xoffset+yoffset) in 128 bits unit */
	/* NOTE : unlike other pm3 stuff above, stored *after* shiftbpp. don't ask */
	u32 depth;		/* screen depth (8, 12, 15, 16 or 32) */
	u32 video;		/* video control (hsync,vsync) */
	struct pm3fb_info *l_fb_info; /* HW-specific description */
	struct fb_info *f_fb_info; /* FBCON fb_info, non-HW specific */
};



/* more mandatory stuff (see skeletonfb.c + framebuffer driver HOWTO */
static struct pm3fb_info pm3fb_fb_info[PM3_MAX_BOARD];
static struct fb_info fbcon_fb_info[PM3_MAX_BOARD];
static struct pm3fb_par current_par[PM3_MAX_BOARD];
static int current_par_valid[PM3_MAX_BOARD];
/* to allow explicit filtering of board */
short bus[PM3_MAX_BOARD];
short slot[PM3_MAX_BOARD];
short func[PM3_MAX_BOARD];
short disable[PM3_MAX_BOARD];
short noaccel[PM3_MAX_BOARD];
char fontn[PM3_MAX_BOARD][PM3_FONTNAME_SIZE];
short depth[PM3_MAX_BOARD];
static char g_options[PM3_OPTIONS_SIZE] __initdata = "pm3fb:dummy";


/* ********************* */
/* ***** prototype ***** */
/* ********************* */
/* card-specific */
static void pm3fb_j2000_setup(struct pm3fb_info *l_fb_info);
/* permedia3-specific */
static void pm3fb_preserve_memory_timings(struct pm3fb_info *l_fb_info);
static void pm3fb_write_memory_timings(struct pm3fb_info *l_fb_info);
static unsigned long pm3fb_read_dac_reg(struct pm3fb_info *l_fb_info,
					unsigned long r);
static unsigned long pm3fb_CalculateClock(struct pm3fb_info *l_fb_info, unsigned long reqclock,	/* In kHz units */
					  unsigned long refclock,	/* In kHz units */
					  unsigned char *prescale,	/* ClkPreScale */
					  unsigned char *feedback,	/* ClkFeedBackScale */
					  unsigned char *postscale
					  /* ClkPostScale */ );
static int pm3fb_Shiftbpp(struct pm3fb_info *l_fb_info,
			  unsigned long depth, int v);
static int pm3fb_Unshiftbpp(struct pm3fb_info *l_fb_info,
			    unsigned long depth, int v);
static void pm3fb_mapIO(struct pm3fb_info *l_fb_info);
static void pm3fb_unmapIO(struct pm3fb_info *l_fb_info);
static void pm3fb_write_mode(struct pm3fb_info *l_fb_info);
static void pm3fb_read_mode(struct pm3fb_info *l_fb_info,
			    struct pm3fb_par *curpar);
static unsigned long pm3fb_size_memory(struct pm3fb_info *l_fb_info);
static void pm3fb_detect(void);
static void pm3fb_common_init(struct pm3fb_info *l_fb_info);
static void pm3fb_decode_var(const struct fb_var_screeninfo *var,
			     struct pm3fb_par *p, struct pm3fb_info *l_fb_info);
static void pm3fb_encode_var(struct fb_var_screeninfo *var,
			    struct pm3fb_par *p, struct pm3fb_info *l_fb_info);
static void pm3fb_encode_fix(struct fb_fix_screeninfo *fix,
			     struct pm3fb_par *p, struct pm3fb_info *l_fb_info);
static void pm3fb_set_color(struct pm3fb_info *l_fb_info,
			    unsigned char regno, unsigned char r,
			    unsigned char g, unsigned char b);
static void pm3fb_encode_depth(struct fb_var_screeninfo *var, long d);
/* accelerated permedia3-specific */
#ifdef PM3FB_USE_ACCEL
static void pm3fb_wait_pm3(struct pm3fb_info *l_fb_info);
static void pm3fb_init_engine(struct pm3fb_info *l_fb_info);
static void pm3fb_fillrect_8bpp(struct pm3fb_info *l_fb_info, int x1, int y1, unsigned int width,
				unsigned int height, unsigned long c, int GXrop);
static void pm3fb_fillrect_16bpp(struct pm3fb_info *l_fb_info, int x1, int y1, unsigned int width,
				 unsigned int height, unsigned long c, int GXrop);
static void pm3fb_fillrect_32bpp(struct pm3fb_info *l_fb_info, int x1, int y1, unsigned int width,
				 unsigned int height, unsigned long c, int GXrop);
void pm3fb_fillrect(struct fb_info *p, int x1, int y1, unsigned int width,
                    unsigned int height, unsigned long color, int rop);
void pm3fb_copyarea(struct fb_info *p, int sx, int sy, unsigned int width,
		    unsigned int height, int dx, int dy);
#endif /* PM3FB_USE_ACCEL */
/* fb API */
static int pm3fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int pm3fb_set_par(struct fb_info *info);
static int pm3fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info);
static int pm3fb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info);
static int pm3fb_blank(int blank_mode, struct fb_info *info);

/* pre-init */
static void pm3fb_mode_setup(char *mode, unsigned long board_num);
static void pm3fb_pciid_setup(char *pciid, unsigned long board_num);
static char *pm3fb_boardnum_setup(char *options, unsigned long *bn);
static void pm3fb_real_setup(char *options);
/* fb API init */
int __init pm3fb_init(void);
int __init pm3fb_setup(char *options);
static int pm3fb_open(struct fb_info *info, int user);
static int pm3fb_release(struct fb_info *info, int user);

/*
static int pm3fb_ioctl(struct inode *inode, struct file *file,
                       u_int cmd, u_long arg, int con,
		       struct fb_info *info);
*/

static struct fb_ops pm3fb_ops = {
	owner:		THIS_MODULE,
	fb_open:	pm3fb_open,    /* only if you need it to do something */
	fb_release:	pm3fb_release, /* only if you need it to do something */
	fb_check_var:	pm3fb_check_var,
	fb_set_par:	pm3fb_set_par,	   /* optional */	
	fb_setcolreg:	pm3fb_setcolreg,
	fb_blank:	pm3fb_blank,	   /* optional */
	fb_pan_display:	pm3fb_pan_display, /* optional */	
	fb_fillrect:	cfb_fillrect,  
	fb_copyarea:	cfb_copyarea,  
	fb_imageblit:	cfb_imageblit, 
	fb_ioctl:       NULL,     /* optional */
	fb_mmap:	NULL,      /* optional */	
};

/* ********************************** */
/* ***** card-specific function ***** */
/* ********************************** */
static struct {
		char cardname[32]; /* recognized card name */
		u16 subvendor; /* subvendor of the card */
		u16 subdevice; /* subdevice of the card */
		u8  func; /* function of the card to which the extra init apply */
		void (*specific_setup)(struct pm3fb_info *l_fb_info); /* card/func specific setup, done before _any_ FB access */
} cardbase[] = {
	{ "Unknown Permedia3 board", 0xFFFF, 0xFFFF, 0xFF, NULL },
	{ "Appian Jeronimo 2000 head 1", 0x1097, 0x3d32, 1, NULL },
	{ "Appian Jeronimo 2000 head 2", 0x1097, 0x3d32, 2, pm3fb_j2000_setup },
	{ "Formac ProFormance 3", PCI_VENDOR_ID_3DLABS, 0x000a, 0, NULL }, /* Formac use 3DLabs ID */
	{ "3DLabs Permedia3 Create!", PCI_VENDOR_ID_3DLABS, 0x0127, 0, NULL },
	{ "3DLabs Oxygen VX1 PCI", PCI_VENDOR_ID_3DLABS, 0x0121, 0, NULL },
	{ "3DLabs Oxygen VX1 AGP", PCI_VENDOR_ID_3DLABS, 0x0125, 0, NULL },
	{ "3DLabs Oxygen VX1-16 AGP", PCI_VENDOR_ID_3DLABS, 0x0140, 0, NULL },
	{ "3DLabs Oxygen VX1-1600SW PCI", PCI_VENDOR_ID_3DLABS, 0x0800, 0, NULL },
	{ "\0", 0x0, 0x0, 0, NULL }
};

static void pm3fb_j2000_setup(struct pm3fb_info *l_fb_info)
{       /* the appian j2000 require more initialization of the second head */
	/* l_fb_info must point to the _second_ head of the J2000 */
	
	DTRACE;
	
	/* Memory timings for the Appian J2000 board. */
	l_fb_info->memcaps = 0x02e311B8;
	l_fb_info->memtimings = 0x07424905;
	l_fb_info->memcontrol = 0x0c000003;
	l_fb_info->memrefresh = 0x00000061;
	l_fb_info->mempowerdown = 0x00000000;
	
	pm3fb_write_memory_timings(l_fb_info);
}

/* *************************************** */
/* ***** permedia3-specific function ***** */
/* *************************************** */

static void pm3fb_preserve_memory_timings(struct pm3fb_info *l_fb_info)
{
	l_fb_info->memcaps = PM3_READ_REG(PM3LocalMemCaps);
	l_fb_info->memtimings = PM3_READ_REG(PM3LocalMemTimings);
	l_fb_info->memcontrol = PM3_READ_REG(PM3LocalMemControl);
	l_fb_info->memrefresh = PM3_READ_REG(PM3LocalMemRefresh);
	l_fb_info->mempowerdown = PM3_READ_REG(PM3LocalMemPowerDown);
}

static void pm3fb_write_memory_timings(struct pm3fb_info *l_fb_info)
{
	unsigned char m, n, p;
	unsigned long clockused;
	
	PM3_SLOW_WRITE_REG(PM3LocalMemCaps, l_fb_info->memcaps);
	PM3_SLOW_WRITE_REG(PM3LocalMemTimings, l_fb_info->memtimings);
	PM3_SLOW_WRITE_REG(PM3LocalMemControl, l_fb_info->memcontrol);
	PM3_SLOW_WRITE_REG(PM3LocalMemRefresh, l_fb_info->memrefresh);
	PM3_SLOW_WRITE_REG(PM3LocalMemPowerDown, l_fb_info->mempowerdown);

	clockused =
	    pm3fb_CalculateClock(l_fb_info, 2 * 105000, PM3_REF_CLOCK, &m,
				 &n, &p);

	PM3_WRITE_DAC_REG(PM3RD_KClkPreScale, m);
	PM3_WRITE_DAC_REG(PM3RD_KClkFeedbackScale, n);
	PM3_WRITE_DAC_REG(PM3RD_KClkPostScale, p);
	PM3_WRITE_DAC_REG(PM3RD_KClkControl,
			  PM3RD_KClkControl_STATE_RUN |
			  PM3RD_KClkControl_SOURCE_PLL |
			  PM3RD_KClkControl_ENABLE);
	PM3_WRITE_DAC_REG(PM3RD_MClkControl,
			  PM3RD_MClkControl_STATE_RUN |
			  PM3RD_MClkControl_SOURCE_KCLK |
			  PM3RD_MClkControl_ENABLE);
	PM3_WRITE_DAC_REG(PM3RD_SClkControl,
			  PM3RD_SClkControl_STATE_RUN |
			  PM3RD_SClkControl_SOURCE_PCLK |
			  PM3RD_SClkControl_ENABLE);
}

static unsigned long pm3fb_read_dac_reg(struct pm3fb_info *l_fb_info,
					unsigned long r)
{
	DASSERT((l_fb_info->vIOBase != (unsigned char *) (-1)),
		"l_fb_info->vIOBase mapped in read dac reg\n");
	PM3_SET_INDEX(r);
	mb();
	return (PM3_READ_REG(PM3RD_IndexedData));
}

/* Calculating various clock parameter */
static unsigned long pm3fb_CalculateClock(struct pm3fb_info *l_fb_info, unsigned long reqclock,	/* In kHz units */
					  unsigned long refclock,	/* In kHz units */
					  unsigned char *prescale,	/* ClkPreScale */
					  unsigned char *feedback,	/* ClkFeedBackScale */
					  unsigned char *postscale
					  /* ClkPostScale */ )
{
	int f, pre, post;
	unsigned long freq;
	long freqerr = 1000;
	unsigned long actualclock = 0;

	DTRACE;

	for (f = 1; f < 256; f++) {
		for (pre = 1; pre < 256; pre++) {
			for (post = 0; post < 5; post++) {
				freq =
				    ((2 * refclock * f) /
				     (pre * (1 << post)));
				if ((reqclock > freq - freqerr)
				    && (reqclock < freq + freqerr)) {
					freqerr =
					    (reqclock >
					     freq) ? reqclock -
					    freq : freq - reqclock;
					*feedback = f;
					*prescale = pre;
					*postscale = post;
					actualclock = freq;
				}
			}
		}
	}

	return (actualclock);
}

static int pm3fb_Shiftbpp(struct pm3fb_info *l_fb_info,
			  unsigned long depth, int v)
{
	DTRACE;
	
	switch (depth) {
	case 8:
		return (v >> 4);
	case 12:
	case 15:
	case 16:
		return (v >> 3);
	case 32:
		return (v >> 2);
	}
	DPRINTK(1, "Unsupported depth %ld\n", depth);
	return (0);
}

static int pm3fb_Unshiftbpp(struct pm3fb_info *l_fb_info,
			    unsigned long depth, int v)
{
	DTRACE;

	switch (depth) {
	case 8:
		return (v << 4);
	case 12:	
	case 15:
	case 16:
		return (v << 3);
	case 32:
		return (v << 2);
	}
	DPRINTK(1, "Unsupported depth %ld\n", depth);
	return (0);
}

static void pm3fb_mapIO(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	l_fb_info->vIOBase =
	    ioremap((unsigned long) l_fb_info->pIOBase, PM3_REGS_SIZE);
	l_fb_info->v_fb =
	    ioremap((unsigned long) l_fb_info->p_fb, l_fb_info->fb_size);
	DPRINTK(2, "IO mapping : IOBase %lx / %lx, fb %lx / %lx\n",
		(unsigned long) l_fb_info->pIOBase,
		(unsigned long) l_fb_info->vIOBase,
		(unsigned long) l_fb_info->p_fb,
		(unsigned long) l_fb_info->v_fb);
}

static void pm3fb_unmapIO(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	iounmap(l_fb_info->vIOBase);
	iounmap(l_fb_info->v_fb);
	l_fb_info->vIOBase = (unsigned char *) -1;
	l_fb_info->v_fb = (unsigned char *) -1;
}

/* write the mode to registers */
static void pm3fb_write_mode(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	PM3_SLOW_WRITE_REG(PM3MemBypassWriteMask, 0xffffffff);
	PM3_SLOW_WRITE_REG(PM3Aperture0, 0x00000000);
	PM3_SLOW_WRITE_REG(PM3Aperture1, 0x00000000);
	PM3_SLOW_WRITE_REG(PM3FIFODis, 0x00000007);

	PM3_SLOW_WRITE_REG(PM3HTotal,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->htotal -
					  1));
	PM3_SLOW_WRITE_REG(PM3HsEnd,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->hsend));
	PM3_SLOW_WRITE_REG(PM3HsStart,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->
					  hsstart));
	PM3_SLOW_WRITE_REG(PM3HbEnd,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->hbend));
	PM3_SLOW_WRITE_REG(PM3HgEnd,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->hbend));
	PM3_SLOW_WRITE_REG(PM3ScreenStride,
			   pm3fb_Shiftbpp(l_fb_info,
					  l_fb_info->current_par->depth,
					  l_fb_info->current_par->stride));
	PM3_SLOW_WRITE_REG(PM3VTotal, l_fb_info->current_par->vtotal - 1);
	PM3_SLOW_WRITE_REG(PM3VsEnd, l_fb_info->current_par->vsend - 1);
	PM3_SLOW_WRITE_REG(PM3VsStart,
			   l_fb_info->current_par->vsstart - 1);
	PM3_SLOW_WRITE_REG(PM3VbEnd, l_fb_info->current_par->vbend);

	switch (l_fb_info->current_par->depth) {
	case 8:
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_8BIT);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_8BIT);
		break;

	case 12:
	case 15:
	case 16:
#ifndef __BIG_ENDIAN
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT);
#else
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT |
				   PM3ByApertureMode_BYTESWAP_BADC);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_16BIT |
				   PM3ByApertureMode_BYTESWAP_BADC);
#endif /* ! __BIG_ENDIAN */
		break;

	case 32:
#ifndef __BIG_ENDIAN
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT);
#else
		PM3_SLOW_WRITE_REG(PM3ByAperture1Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT |
				   PM3ByApertureMode_BYTESWAP_DCBA);
		PM3_SLOW_WRITE_REG(PM3ByAperture2Mode,
				   PM3ByApertureMode_PIXELSIZE_32BIT |
				   PM3ByApertureMode_BYTESWAP_DCBA);
#endif /* ! __BIG_ENDIAN */
		break;

	default:
		DPRINTK(1, "Unsupported depth %d\n",
			l_fb_info->current_par->depth);
		break;
	}

	PM3_SLOW_WRITE_REG(PM3VideoControl, l_fb_info->current_par->video);
	PM3_SLOW_WRITE_REG(PM3VClkCtl,
			   (PM3_READ_REG(PM3VClkCtl) & 0xFFFFFFFC));
	PM3_SLOW_WRITE_REG(PM3ScreenBase, l_fb_info->current_par->base);
	PM3_SLOW_WRITE_REG(PM3ChipConfig,
			   (PM3_READ_REG(PM3ChipConfig) & 0xFFFFFFFD));

	{
		unsigned char m;	/* ClkPreScale */
		unsigned char n;	/* ClkFeedBackScale */
		unsigned char p;	/* ClkPostScale */
		(void)pm3fb_CalculateClock(l_fb_info, l_fb_info->current_par->pixclock, PM3_REF_CLOCK, &m, &n, &p);

		DPRINTK(2,
			"Pixclock: %d, Pre: %d, Feedback: %d, Post: %d\n",
			l_fb_info->current_par->pixclock, (int) m, (int) n,
			(int) p);

		PM3_WRITE_DAC_REG(PM3RD_DClk0PreScale, m);
		PM3_WRITE_DAC_REG(PM3RD_DClk0FeedbackScale, n);
		PM3_WRITE_DAC_REG(PM3RD_DClk0PostScale, p);
	}
	/*
	   PM3_WRITE_DAC_REG(PM3RD_IndexControl, 0x00);
	 */
	/*
	   PM3_SLOW_WRITE_REG(PM3RD_IndexControl, 0x00);
	 */
	{
		char tempsync = 0x00;

		if ((l_fb_info->current_par->
		     video & PM3VideoControl_HSYNC_MASK) ==
		    PM3VideoControl_HSYNC_ACTIVE_HIGH)
			tempsync |= PM3RD_SyncControl_HSYNC_ACTIVE_HIGH;
		if ((l_fb_info->current_par->
		     video & PM3VideoControl_VSYNC_MASK) ==
		    PM3VideoControl_VSYNC_ACTIVE_HIGH)
			tempsync |= PM3RD_SyncControl_VSYNC_ACTIVE_HIGH;

		PM3_WRITE_DAC_REG(PM3RD_SyncControl, tempsync);
		DPRINTK(2, "PM3RD_SyncControl: %d\n", tempsync);
	}
	PM3_WRITE_DAC_REG(PM3RD_DACControl, 0x00);

	switch (l_fb_info->current_par->depth) {
	case 8:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_8_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_CI8_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW);
		PM3_WRITE_DAC_REG(PM3RD_MiscControl,
				  PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE);
		break;
	case 12:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_4444_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		PM3_WRITE_DAC_REG(PM3RD_MiscControl,
				  PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
				  PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE);
		break;		
	case 15:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_5551_FRONT_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		PM3_WRITE_DAC_REG(PM3RD_MiscControl,
				  PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
				  PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE);
		break;		
	case 16:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_16_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_565_FRONT_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW |
				  PM3RD_ColorFormat_LINEAR_COLOR_EXT_ENABLE);
		PM3_WRITE_DAC_REG(PM3RD_MiscControl,
				  PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
				  PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE);
		break;
	case 32:
		PM3_WRITE_DAC_REG(PM3RD_PixelSize,
				  PM3RD_PixelSize_32_BIT_PIXELS);
		PM3_WRITE_DAC_REG(PM3RD_ColorFormat,
				  PM3RD_ColorFormat_8888_COLOR |
				  PM3RD_ColorFormat_COLOR_ORDER_BLUE_LOW);
		PM3_WRITE_DAC_REG(PM3RD_MiscControl,
				  PM3RD_MiscControl_DIRECTCOLOR_ENABLE |
				  PM3RD_MiscControl_HIGHCOLOR_RES_ENABLE);
		break;
	}

	PM3_SHOW_CUR_MODE;
	PM3_SHOW_CUR_TIMING;
}

static void pm3fb_read_mode(struct pm3fb_info *l_fb_info,
			    struct pm3fb_par *curpar)
{
	unsigned long pixsize1, pixsize2, clockused;
	unsigned long pre, feedback, post;

	DTRACE;

	clockused = PM3_READ_REG(PM3VClkCtl);

	switch (clockused) {
	case 3:
		pre = PM3_READ_DAC_REG(PM3RD_DClk3PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk3FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk3PostScale);

		DPRINTK(2,
			"DClk3 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	case 2:
		pre = PM3_READ_DAC_REG(PM3RD_DClk2PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk2FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk2PostScale);

		DPRINTK(2,
			"DClk2 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	case 1:
		pre = PM3_READ_DAC_REG(PM3RD_DClk1PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk1FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk1PostScale);

		DPRINTK(2,
			"DClk1 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	case 0:
		pre = PM3_READ_DAC_REG(PM3RD_DClk0PreScale);
		feedback = PM3_READ_DAC_REG(PM3RD_DClk0FeedbackScale);
		post = PM3_READ_DAC_REG(PM3RD_DClk0PostScale);

		DPRINTK(2,
			"DClk0 parameter: Pre: %ld, Feedback: %ld, Post: %ld ; giving pixclock: %ld\n",
			pre, feedback, post, PM3_SCALE_TO_CLOCK(pre,
								feedback,
								post));
		break;
	default:
		pre = feedback = post = 0;
		DPRINTK(1, "Unknowk D clock used : %ld\n", clockused);
		break;
	}

	curpar->pixclock = PM3_SCALE_TO_CLOCK(pre, feedback, post);

	pixsize1 =
	    PM3ByApertureMode_PIXELSIZE_MASK &
	    (PM3_READ_REG(PM3ByAperture1Mode));
	pixsize2 =
	    PM3ByApertureMode_PIXELSIZE_MASK &
	    (PM3_READ_REG(PM3ByAperture2Mode));

	DASSERT((pixsize1 == pixsize2),
		"pixsize the same in both aperture\n");

	if (pixsize1 & PM3ByApertureMode_PIXELSIZE_32BIT)
		curpar->depth = 32;
	else if (pixsize1 & PM3ByApertureMode_PIXELSIZE_16BIT)
	{
		curpar->depth = 16;
	}
	else
		curpar->depth = 8;

	/* not sure if I need to add one on the next ; it give better result with */
	curpar->htotal =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     1 + PM3_READ_REG(PM3HTotal));
	curpar->hsend =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3HsEnd));
	curpar->hsstart =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3HsStart));
	curpar->hbend =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3HbEnd));

	curpar->stride =
	    pm3fb_Unshiftbpp(l_fb_info, curpar->depth,
			     PM3_READ_REG(PM3ScreenStride));

	curpar->vtotal = 1 + PM3_READ_REG(PM3VTotal);
	curpar->vsend = 1 + PM3_READ_REG(PM3VsEnd);
	curpar->vsstart = 1 + PM3_READ_REG(PM3VsStart);
	curpar->vbend = PM3_READ_REG(PM3VbEnd);

	curpar->video = PM3_READ_REG(PM3VideoControl);

	curpar->base = PM3_READ_REG(PM3ScreenBase);
	curpar->width = curpar->htotal - curpar->hbend; /* make virtual == displayed resolution */
	curpar->height = curpar->vtotal - curpar->vbend;

	DPRINTK(2, "Found : %d * %d, %d Khz, stride is %08x\n",
		curpar->width, curpar->height, curpar->pixclock,
		curpar->stride);
}

static unsigned long pm3fb_size_memory(struct pm3fb_info *l_fb_info)
{
	unsigned long memsize, tempBypass, i, temp1, temp2;
	u16 subvendor, subdevice;

	DTRACE;

	l_fb_info->fb_size = 64 * 1024 * 1024;	/* pm3 aperture always 64 MB */
	pm3fb_mapIO(l_fb_info);	/* temporary map IO */

	DASSERT((l_fb_info->vIOBase != NULL),
		"IO successfully mapped before mem detect\n");
	DASSERT((l_fb_info->v_fb != NULL),
		"FB successfully mapped before mem detect\n");

	/* card-specific stuff, *before* accessing *any* FB memory */
	if ((!pci_read_config_word
	     (l_fb_info->dev, PCI_SUBSYSTEM_VENDOR_ID, &subvendor))
	    &&
	    (!pci_read_config_word
	     (l_fb_info->dev, PCI_SUBSYSTEM_ID, &subdevice))) {
		i = 0; l_fb_info->board_type = 0;
		while ((cardbase[i].cardname[0]) && !(l_fb_info->board_type)) {
			if ((cardbase[i].subvendor == subvendor) &&
			    (cardbase[i].subdevice == subdevice) &&
			    (cardbase[i].func == PCI_FUNC(l_fb_info->dev->devfn))) {
				DPRINTK(2, "Card #%ld is an %s\n",
					l_fb_info->board_num,
					cardbase[i].cardname);
				if (cardbase[i].specific_setup)
					cardbase[i].specific_setup(l_fb_info);
				l_fb_info->board_type = i;
			}
			i++;
		}
		if (!l_fb_info->board_type) {
			DPRINTK(1, "Card #%ld is an unknown 0x%04x / 0x%04x\n",
				l_fb_info->board_num, subvendor, subdevice);
		}
	} else {
		printk(KERN_ERR "pm3fb: Error: pci_read_config_word failed, board #%ld\n",
		       l_fb_info->board_num);
	}

	/* card-specific setup is done, we preserve the final
           memory timing for future reference */
	pm3fb_preserve_memory_timings(l_fb_info);
	
	tempBypass = PM3_READ_REG(PM3MemBypassWriteMask);

	DPRINTK(2, "PM3MemBypassWriteMask was: 0x%08lx\n", tempBypass);

	PM3_SLOW_WRITE_REG(PM3MemBypassWriteMask, 0xFFFFFFFF);

	/* pm3 split up memory, replicates, and do a lot of nasty stuff IMHO ;-) */
	for (i = 0; i < 32; i++) {
		fb_writel(i * 0x00345678,
			  (l_fb_info->v_fb + (i * 1048576)));
		mb();
		temp1 = fb_readl((l_fb_info->v_fb + (i * 1048576)));
		/* Let's check for wrapover, write will fail at 16MB boundary */
		if (temp1 == (i * 0x00345678))
			memsize = i;
		else
			break;
	}

	DPRINTK(2, "First detect pass already got %ld MB\n", memsize + 1);

	if (memsize == i) {
		for (i = 0; i < 32; i++) {
			/* Clear first 32MB ; 0 is 0, no need to byteswap */
			writel(0x0000000,
			       (l_fb_info->v_fb + (i * 1048576)));
			mb();
		}

		for (i = 32; i < 64; i++) {
			fb_writel(i * 0x00345678,
				  (l_fb_info->v_fb + (i * 1048576)));
			mb();
			temp1 =
			    fb_readl((l_fb_info->v_fb + (i * 1048576)));
			temp2 =
			    fb_readl((l_fb_info->v_fb +
				      ((i - 32) * 1048576)));
			if ((temp1 == (i * 0x00345678)) && (temp2 == 0))	/* different value, different RAM... */
				memsize = i;
			else
				break;
		}
	}

	DPRINTK(2, "Second detect pass got %ld MB\n", memsize + 1);

	PM3_SLOW_WRITE_REG(PM3MemBypassWriteMask, tempBypass);

	pm3fb_unmapIO(l_fb_info);
	memsize = 1048576 * (memsize + 1);

	DPRINTK(2, "Returning 0x%08lx bytes\n", memsize);

	l_fb_info->fb_size = memsize;

	return (memsize);
}

static void pm3fb_detect(void)
{
	struct pci_dev *dev_array[PM3_MAX_BOARD];
	struct pci_dev *dev = NULL;
	struct pm3fb_info *l_fb_info = &(pm3fb_fb_info[0]);
	unsigned long i, j, done;

	DTRACE;

	for (i = 0; i < PM3_MAX_BOARD; i++) {
		dev_array[i] = NULL;
		pm3fb_fb_info[i].dev = NULL;
	}

	dev =
	    pci_find_device(PCI_VENDOR_ID_3DLABS,
			    PCI_DEVICE_ID_3DLABS_PERMEDIA3, dev);

	for (i = 0; ((i < PM3_MAX_BOARD) && dev); i++) {
		dev_array[i] = dev;
		dev =
		    pci_find_device(PCI_VENDOR_ID_3DLABS,
				    PCI_DEVICE_ID_3DLABS_PERMEDIA3, dev);
	}

	if (dev) {		/* more than PM3_MAX_BOARD */
		printk(KERN_WARNING "pm3fb: Warning: more than %d boards found\n",
		       PM3_MAX_BOARD);
	}

	if (!dev_array[0]) {	/* not a single board, abort */
		return;
	}

	/* allocate user-defined boards */
	for (i = 0; i < PM3_MAX_BOARD; i++) {
		if ((bus[i] >= 0) && (slot[i] >= 0) && (func[i] >= 0)) {
			for (j = 0; j < PM3_MAX_BOARD; j++) {
				if ((dev_array[j] != NULL) &&
				    (dev_array[j]->bus->number == bus[i])
				    && (PCI_SLOT(dev_array[j]->devfn) ==
					slot[i])
				    && (PCI_FUNC(dev_array[j]->devfn) ==
					func[i])) {
					pm3fb_fb_info[i].dev = dev_array[j];
					dev_array[j] = NULL;
				}
			}
		}
	}
	/* allocate remaining boards */
	for (i = 0; i < PM3_MAX_BOARD; i++) {
		if (pm3fb_fb_info[i].dev == NULL) {
			done = 0;
			for (j = 0; ((j < PM3_MAX_BOARD) && (!done)); j++) {
				if (dev_array[j] != NULL) {
					pm3fb_fb_info[i].dev = dev_array[j];
					dev_array[j] = NULL;
					done = 1;
				}
			}
		}
	}

	/* at that point, all PCI Permedia3 are detected and allocated */
	/* now, initialize... or not */
	for (i = 0; i < PM3_MAX_BOARD; i++) {
		l_fb_info = &(pm3fb_fb_info[i]);
		if ((l_fb_info->dev) && (!disable[i])) {	/* PCI device was found and not disabled by user */
			DPRINTK(2,
				"found @%lx Vendor %lx Device %lx ; base @ : %lx - %lx - %lx - %lx - %lx - %lx, irq %ld\n",
				(unsigned long) l_fb_info->dev,
				(unsigned long) l_fb_info->dev->
				vendor,
				(unsigned long) l_fb_info->dev->
				device,
				(unsigned long)
				pci_resource_start(l_fb_info->dev,
						   0),
				(unsigned long)
				pci_resource_start(l_fb_info->dev,
						   1),
				(unsigned long)
				pci_resource_start(l_fb_info->dev,
						   2),
				(unsigned long)
				pci_resource_start(l_fb_info->dev,
						   3),
				(unsigned long)
				pci_resource_start(l_fb_info->dev,
						   4),
				(unsigned long)
				pci_resource_start(l_fb_info->dev,
						   5),
				(unsigned long) l_fb_info->dev->
				irq);
			
			l_fb_info->pIOBase =
				(unsigned char *)
				pci_resource_start(l_fb_info->dev, 0);
#ifdef __BIG_ENDIAN
			l_fb_info->pIOBase += PM3_REGS_SIZE;
#endif
			l_fb_info->vIOBase = (unsigned char *) -1;
			l_fb_info->p_fb =
				(unsigned char *)
				pci_resource_start(l_fb_info->dev, 1);
			l_fb_info->v_fb = (unsigned char *) -1;
			
			if (!request_mem_region
			    ((unsigned long)l_fb_info->p_fb, 64 * 1024 * 1024, /* request full aperture size */
			     "pm3fb")) {
				printk
					(KERN_ERR "pm3fb: Error: couldn't request framebuffer memory, board #%ld\n",
					 l_fb_info->board_num);
				continue;
			}
			if (!request_mem_region
			    ((unsigned long)l_fb_info->pIOBase, PM3_REGS_SIZE,
			     "pm3fb I/O regs")) {
				printk
					(KERN_ERR "pm3fb: Error: couldn't request IObase memory, board #%ld\n",
					 l_fb_info->board_num);
				continue;
			}
			
			l_fb_info->fb_size =
				pm3fb_size_memory(l_fb_info);
			
			(void) pci_enable_device(l_fb_info->dev);
			
			pm3fb_common_init(l_fb_info);			
		}
	}
}

/* common initialisation */
static void pm3fb_common_init(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	DPRINTK(2, "Initializing board #%ld @ %lx\n", l_fb_info->board_num,
		(unsigned long) l_fb_info);


	l_fb_info->current_par->f_fb_info->node = -1;
	l_fb_info->current_par->f_fb_info->fbops = &pm3fb_ops;
	l_fb_info->current_par->f_fb_info->pseudo_palette = l_fb_info->pseudo_palette;
	l_fb_info->current_par->f_fb_info->flags = FBINFO_FLAG_DEFAULT;
	l_fb_info->current_par->f_fb_info->par = l_fb_info->current_par;

	pm3fb_mapIO(l_fb_info);
	l_fb_info->current_par->f_fb_info->screen_base = l_fb_info->v_fb;
	
/*
	if (fontn[l_fb_info->board_num][0])
		strcpy(l_fb_info->gen.fontname,
		       fontn[l_fb_info->board_num]);
*/

	if (!current_par_valid[l_fb_info->board_num])
	{
		if (!l_fb_info->use_current)
			pm3fb_mode_setup("640x480@60", l_fb_info->board_num);
		else
		{
			pm3fb_read_mode(l_fb_info, l_fb_info->current_par);
			pm3fb_encode_var(&(l_fb_info->current_par->f_fb_info->var),
					 l_fb_info->current_par,
					 l_fb_info);
		}
	}

	if (depth[l_fb_info->board_num]) /* override mode-defined depth */
	{
		pm3fb_encode_depth(&(l_fb_info->current_par->f_fb_info->var), depth[l_fb_info->board_num]);
		l_fb_info->current_par->f_fb_info->var.bits_per_pixel = depth2bpp(depth[l_fb_info->board_num]);
	}

	fb_copy_cmap(fb_default_cmap(16),&(l_fb_info->current_par->f_fb_info->cmap), 0);
	
	pm3fb_set_par((struct fb_info*)l_fb_info);

	if (register_framebuffer(l_fb_info->current_par->f_fb_info) < 0) {
		DPRINTK(1, "Couldn't register framebuffer\n");
		return;
	}

	PM3_WRITE_DAC_REG(PM3RD_CursorMode,
			  PM3RD_CursorMode_CURSOR_DISABLE);

	PM3_SHOW_CUR_MODE;
	PM3_SHOW_CUR_TIMING;

	pm3fb_write_mode(l_fb_info);

	printk("fb%d: %s, using %uK of video memory (%s)\n",
	       GET_FB_IDX(l_fb_info->current_par->f_fb_info->node),
	       permedia3_name, (u32) (l_fb_info->fb_size >> 10),
	       cardbase[l_fb_info->board_type].cardname);
}

/* helper for pm3fb_set_par */
static void pm3fb_decode_var(const struct fb_var_screeninfo *var,
			     struct pm3fb_par *p, struct pm3fb_info *l_fb_info)
{
	struct pm3fb_par temp_p;
	u32 xres;

	DTRACE;

	DASSERT((var != NULL), "fb_var_screeninfo* not NULL");
	DASSERT((p != NULL), "pm3fb_par* not NULL");
	DASSERT((l_fb_info != NULL), "pm3fb_info* not NULL");

	memset(&temp_p, 0, sizeof(struct pm3fb_par));
	temp_p.width = (var->xres_virtual + 7) & ~7;
	temp_p.height = var->yres_virtual;

	if (!(depth_supported(var->bits_per_pixel))) /* round unsupported up to a multiple of 8 */
		temp_p.depth = depth2bpp(var->bits_per_pixel);
	else
		temp_p.depth = var->bits_per_pixel;

	temp_p.depth = (temp_p.depth > 32) ? 32 : temp_p.depth; /* max 32 */
	temp_p.depth = (temp_p.depth == 24) ? 32 : temp_p.depth; /* 24 unsupported, round-up to 32 */

	if ((temp_p.depth == 16) && (var->red.length == 5) && (var->green.length == 5) && (var->blue.length == 5))
		temp_p.depth = 15; /* RGBA 5551 is stored as depth 15 */

	if ((temp_p.depth == 16) && (var->red.length == 4) && (var->green.length == 4) && (var->blue.length == 4))
		temp_p.depth = 12; /* RGBA 4444  is stored as depth 12 */


	DPRINTK(2,
		"xres: %d, yres: %d, vxres: %d, vyres: %d ; xoffset:%d, yoffset: %d\n",
		var->xres, var->yres, var->xres_virtual, var->yres_virtual,
		var->xoffset, var->yoffset);

	xres = (var->xres + 31) & ~31;
	if (temp_p.width < xres + var->xoffset)
		temp_p.width = xres + var->xoffset;
	if (temp_p.height < var->yres + var->yoffset)
		temp_p.height = var->yres + var->yoffset;

	temp_p.pixclock = PICOS2KHZ(var->pixclock);
	
	temp_p.hsstart = var->right_margin;
	temp_p.hsend = var->right_margin + var->hsync_len;
	temp_p.hbend =
		var->right_margin + var->hsync_len + var->left_margin;
	temp_p.htotal = xres + temp_p.hbend;
	
	temp_p.vsstart = var->lower_margin;
	temp_p.vsend = var->lower_margin + var->vsync_len;
	temp_p.vbend =
		var->lower_margin + var->vsync_len + var->upper_margin;
	temp_p.vtotal = var->yres + temp_p.vbend;
	
	temp_p.stride = temp_p.width;
	
	DPRINTK(2, "Using %d * %d, %d Khz, stride is %08x\n",
		temp_p.width, temp_p.height, temp_p.pixclock,
		temp_p.stride);
	
	temp_p.base =
		pm3fb_Shiftbpp(l_fb_info, temp_p.depth,
			       (var->yoffset * xres) + var->xoffset);
	
	temp_p.video = 0;
	
	if (var->sync & FB_SYNC_HOR_HIGH_ACT)
		temp_p.video |= PM3VideoControl_HSYNC_ACTIVE_HIGH;
	else
		temp_p.video |= PM3VideoControl_HSYNC_ACTIVE_LOW;
	
	if (var->sync & FB_SYNC_VERT_HIGH_ACT)
		temp_p.video |= PM3VideoControl_VSYNC_ACTIVE_HIGH;
	else
		temp_p.video |= PM3VideoControl_VSYNC_ACTIVE_LOW;
	
	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		temp_p.video |= PM3VideoControl_LINE_DOUBLE_ON;
	else
		temp_p.video |= PM3VideoControl_LINE_DOUBLE_OFF;
	
	if (var->activate == FB_ACTIVATE_NOW)
		temp_p.video |= PM3VideoControl_ENABLE;
	else {
		temp_p.video |= PM3VideoControl_DISABLE;
		DPRINTK(2, "PM3Video disabled\n");
	}
	
	switch (temp_p.depth) {
	case 8:
		temp_p.video |= PM3VideoControl_PIXELSIZE_8BIT;
		break;
	case 12:
	case 15:
	case 16:
		temp_p.video |= PM3VideoControl_PIXELSIZE_16BIT;
		break;
	case 32:
		temp_p.video |= PM3VideoControl_PIXELSIZE_32BIT;
		break;
	default:
		DPRINTK(1, "Unsupported depth\n");
		break;
	}
	
	(*p) = temp_p;
	
#ifdef PM3FB_USE_ACCEL
	if (var->accel_flags & FB_ACCELF_TEXT)
		noaccel[l_fb_info->board_num] = 0;
	else
		noaccel[l_fb_info->board_num] = 1;
#endif /* PM3FB_USE_ACCEL */
}

static void pm3fb_encode_var(struct fb_var_screeninfo *var,
			     struct pm3fb_par *p, struct pm3fb_info *l_fb_info)
{
	u32 base;

	DTRACE;

	DASSERT((var != NULL), "fb_var_screeninfo* not NULL");
	DASSERT((p != NULL), "pm3fb_par* not NULL");
	DASSERT((l_fb_info != NULL), "pm3fb_info* not NULL");

	memset(var, 0, sizeof(struct fb_var_screeninfo));

#ifdef PM3FB_USE_ACCEL
	if (!(noaccel[l_fb_info->board_num]))
		var->accel_flags |= FB_ACCELF_TEXT;
#endif /* PM3FB_USE_ACCEL */

	var->xres_virtual = p->width;
	var->yres_virtual = p->height;
	var->xres = p->htotal - p->hbend;
	var->yres = p->vtotal - p->vbend;

	DPRINTK(2, "xres = %d, yres : %d\n", var->xres, var->yres);

	var->right_margin = p->hsstart;
	var->hsync_len = p->hsend - p->hsstart;
	var->left_margin = p->hbend - p->hsend;
	var->lower_margin = p->vsstart;
	var->vsync_len = p->vsend - p->vsstart;
	var->upper_margin = p->vbend - p->vsend;
	var->bits_per_pixel = depth2bpp(p->depth);
	
	pm3fb_encode_depth(var, p->depth);

	base = pm3fb_Unshiftbpp(l_fb_info, p->depth, p->base);

	var->xoffset = base % var->xres;
	var->yoffset = base / var->xres;

	var->height = var->width = -1;

	var->pixclock = KHZ2PICOS(p->pixclock);

	if ((p->video & PM3VideoControl_HSYNC_MASK) ==
	    PM3VideoControl_HSYNC_ACTIVE_HIGH)
		var->sync |= FB_SYNC_HOR_HIGH_ACT;
	if ((p->video & PM3VideoControl_VSYNC_MASK) ==
	    PM3VideoControl_VSYNC_ACTIVE_HIGH)
		var->sync |= FB_SYNC_VERT_HIGH_ACT;
	if (p->video & PM3VideoControl_LINE_DOUBLE_ON)
		var->vmode = FB_VMODE_DOUBLE;
}

/* helper for pm3fb_set_par */
static void pm3fb_encode_fix(struct fb_fix_screeninfo *fix,
			     struct pm3fb_par *p, struct pm3fb_info *l_fb_info)
{
	DTRACE;
	
	strcpy(fix->id, permedia3_name);
	fix->smem_start = (unsigned long)l_fb_info->p_fb;
	fix->smem_len = l_fb_info->fb_size;
	fix->mmio_start = (unsigned long)l_fb_info->pIOBase;
	fix->mmio_len = PM3_REGS_SIZE;
#ifdef PM3FB_USE_ACCEL
	if (!(noaccel[l_fb_info->board_num]))
		fix->accel = FB_ACCEL_3DLABS_PERMEDIA3;
	else
#endif /* PM3FB_USE_ACCEL */
		fix->accel = FB_ACCEL_NONE;
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->visual =
		(p->depth == 8) ? FB_VISUAL_PSEUDOCOLOR : FB_VISUAL_TRUECOLOR;
	if (current_par_valid[l_fb_info->board_num])
		fix->line_length =
			l_fb_info->current_par->width *
			depth2ByPP(l_fb_info->current_par->depth);
	else
		fix->line_length = 0;
	fix->xpanstep = 64 / depth2bpp(p->depth);
	fix->ypanstep = 1;
	fix->ywrapstep = 0;
}

static void pm3fb_set_color(struct pm3fb_info *l_fb_info,
			    unsigned char regno, unsigned char r,
			    unsigned char g, unsigned char b)
{
	DTRACE;

	PM3_SLOW_WRITE_REG(PM3RD_PaletteWriteAddress, regno);
	PM3_SLOW_WRITE_REG(PM3RD_PaletteData, r);
	PM3_SLOW_WRITE_REG(PM3RD_PaletteData, g);
	PM3_SLOW_WRITE_REG(PM3RD_PaletteData, b);
}

static void pm3fb_encode_depth(struct fb_var_screeninfo *var, long d)
{
	switch (d) {
	case 8:
		var->red.length = var->green.length = var->blue.length = 8;
		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->transp.offset = var->transp.length = 0;
		break;

	case 12:
		var->red.offset = 8;
		var->red.length = 4;
		var->green.offset = 4;
		var->green.length = 4;
		var->blue.offset = 0;
		var->blue.length = 4;
		var->transp.offset = 12;
		var->transp.length = 4;
		break;

	case 15:
		var->red.offset = 10;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 5;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 15;
		var->transp.length = 1;
		break;

	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = var->transp.length = 0;
		break;

	case 32:
		var->transp.offset = 24;
		var->red.offset = 16;
		var->green.offset = 8;
		var->blue.offset = 0;
		var->red.length = var->green.length =
			var->blue.length = var->transp.length = 8;
		break;

	default:
		DPRINTK(1, "Unsupported depth %ld\n", d);
		break;
	}
}

/* **************************************************** */
/* ***** accelerated permedia3-specific functions ***** */
/* **************************************************** */
#ifdef PM3FB_USE_ACCEL
static void pm3fb_wait_pm3(struct pm3fb_info *l_fb_info)
{
	DTRACE;

	PM3_SLOW_WRITE_REG(PM3FilterMode, PM3FilterModeSync);
	PM3_SLOW_WRITE_REG(PM3Sync, 0);
	mb();
	do {
		while ((PM3_READ_REG(PM3OutFIFOWords)) == 0);
		rmb();
	} while ((PM3_READ_REG(PM3OutputFifo)) != PM3Sync_Tag);
}

static void pm3fb_init_engine(struct pm3fb_info *l_fb_info)
{
	PM3_SLOW_WRITE_REG(PM3FilterMode, PM3FilterModeSync);
	PM3_SLOW_WRITE_REG(PM3StatisticMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3DeltaMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3RasterizerMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3ScissorMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LineStippleMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AreaStippleMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3GIDMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3DepthMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3StencilMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3StencilData, 0x0);
	PM3_SLOW_WRITE_REG(PM3ColorDDAMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCoordMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureIndexMode0, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureIndexMode1, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureReadMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LUTMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureFilterMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureApplicationMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeColorMode1, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeAlphaMode1, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeColorMode0, 0x0);
	PM3_SLOW_WRITE_REG(PM3TextureCompositeAlphaMode0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FogMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3ChromaTestMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AlphaTestMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AntialiasMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3YUVMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AlphaBlendColorMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3AlphaBlendAlphaMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3DitherMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LogicalOpMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3RouterMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3Window, 0x0);

	PM3_SLOW_WRITE_REG(PM3Config2D, 0x0);

	PM3_SLOW_WRITE_REG(PM3SpanColorMask, 0xffffffff);

	PM3_SLOW_WRITE_REG(PM3XBias, 0x0);
	PM3_SLOW_WRITE_REG(PM3YBias, 0x0);
	PM3_SLOW_WRITE_REG(PM3DeltaControl, 0x0);

	PM3_SLOW_WRITE_REG(PM3BitMaskPattern, 0xffffffff);

	PM3_SLOW_WRITE_REG(PM3FBDestReadEnables,
			   PM3FBDestReadEnables_E(0xff) |
			   PM3FBDestReadEnables_R(0xff) |
			   PM3FBDestReadEnables_ReferenceAlpha(0xff));
	PM3_SLOW_WRITE_REG(PM3FBDestReadBufferAddr0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBDestReadBufferOffset0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBDestReadBufferWidth0,
			   PM3FBDestReadBufferWidth_Width(l_fb_info->
							  current_par->
							  width));

	PM3_SLOW_WRITE_REG(PM3FBDestReadMode,
			   PM3FBDestReadMode_ReadEnable |
			   PM3FBDestReadMode_Enable0);
	PM3_SLOW_WRITE_REG(PM3FBSourceReadBufferAddr, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBSourceReadBufferOffset, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBSourceReadBufferWidth,
			   PM3FBSourceReadBufferWidth_Width(l_fb_info->
							    current_par->
							    width));
	PM3_SLOW_WRITE_REG(PM3FBSourceReadMode,
			   PM3FBSourceReadMode_Blocking |
			   PM3FBSourceReadMode_ReadEnable);

	{
		unsigned long rm = 1;
		switch (l_fb_info->current_par->depth) {
		case 8:
			PM3_SLOW_WRITE_REG(PM3PixelSize,
					   PM3PixelSize_GLOBAL_8BIT);
			break;
		case 12:
		case 15:
		case 16:
			PM3_SLOW_WRITE_REG(PM3PixelSize,
					   PM3PixelSize_GLOBAL_16BIT);
			break;
		case 32:
			PM3_SLOW_WRITE_REG(PM3PixelSize,
					   PM3PixelSize_GLOBAL_32BIT);
			break;
		default:
			DPRINTK(1, "Unsupported depth %d\n",
				l_fb_info->current_par->depth);
			break;
		}
		PM3_SLOW_WRITE_REG(PM3RasterizerMode, rm);
	}

	PM3_SLOW_WRITE_REG(PM3FBSoftwareWriteMask, 0xffffffff);
	PM3_SLOW_WRITE_REG(PM3FBWriteMode,
			   PM3FBWriteMode_WriteEnable |
			   PM3FBWriteMode_OpaqueSpan |
			   PM3FBWriteMode_Enable0);
	PM3_SLOW_WRITE_REG(PM3FBWriteBufferAddr0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBWriteBufferOffset0, 0x0);
	PM3_SLOW_WRITE_REG(PM3FBWriteBufferWidth0,
			   PM3FBWriteBufferWidth_Width(l_fb_info->
						       current_par->
						       width));

	PM3_SLOW_WRITE_REG(PM3SizeOfFramebuffer, 0x0);
	{
		unsigned long sofb = (8UL * l_fb_info->fb_size) /
			((depth2bpp(l_fb_info->current_par->depth))
			 * l_fb_info->current_par->width);	/* size in lines of FB */
		if (sofb > 4095)
			PM3_SLOW_WRITE_REG(PM3SizeOfFramebuffer, 4095);
		else
			PM3_SLOW_WRITE_REG(PM3SizeOfFramebuffer, sofb);

		PM3_SLOW_WRITE_REG(PM3FBHardwareWriteMask, 0xffffffff);

		switch (l_fb_info->current_par->depth) {
		case 8:
			PM3_SLOW_WRITE_REG(PM3DitherMode,
					   (1 << 10) | (2 << 3));
			break;
		case 12:
		case 15:
		case 16:
			PM3_SLOW_WRITE_REG(PM3DitherMode,
					   (1 << 10) | (1 << 3));
			break;
		case 32:
			PM3_SLOW_WRITE_REG(PM3DitherMode,
					   (1 << 10) | (0 << 3));
			break;
		default:
			DPRINTK(1, "Unsupported depth %d\n",
				l_fb_info->current_par->depth);
			break;
		}
	}

	PM3_SLOW_WRITE_REG(PM3dXDom, 0x0);
	PM3_SLOW_WRITE_REG(PM3dXSub, 0x0);
	PM3_SLOW_WRITE_REG(PM3dY, (1 << 16));
	PM3_SLOW_WRITE_REG(PM3StartXDom, 0x0);
	PM3_SLOW_WRITE_REG(PM3StartXSub, 0x0);
	PM3_SLOW_WRITE_REG(PM3StartY, 0x0);
	PM3_SLOW_WRITE_REG(PM3Count, 0x0);
	
/* Disable LocalBuffer. better safe than sorry */
	PM3_SLOW_WRITE_REG(PM3LBDestReadMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LBDestReadEnables, 0x0);
	PM3_SLOW_WRITE_REG(PM3LBSourceReadMode, 0x0);
	PM3_SLOW_WRITE_REG(PM3LBWriteMode, 0x0);
	
	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_fillrect_8bpp(struct pm3fb_info *l_fb_info, int x1, int y1, unsigned int width,
				unsigned int height, unsigned long c, int GXrop)
{
	PM3_WAIT(4);

	PM3_WRITE_REG(PM3Config2D,
		      PM3Config2D_UseConstantSource |
		      PM3Config2D_ForegroundROPEnable |
		      (PM3Config2D_ForegroundROP(GXrop)) |
		      PM3Config2D_FBWriteEnable);
	
	PM3_WRITE_REG(PM3ForegroundColor, c);
	
	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(x1)) |
		      (PM3RectanglePosition_YOffset(y1)));
	
	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(width)) |
		      (PM3Render2D_Height(height)));
	
	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_fillrect_16bpp(struct pm3fb_info *l_fb_info, int x1, int y1, unsigned int width,
				 unsigned int height, unsigned long c, int GXrop)
{
	if (l_fb_info->current_par->width > 1600) {
		PM3_WAIT(4);
		
		PM3_WRITE_REG(PM3Config2D,
			      PM3Config2D_UseConstantSource |
			      PM3Config2D_ForegroundROPEnable |
			      (PM3Config2D_ForegroundROP(GXrop)) |
			      PM3Config2D_FBWriteEnable);
		
		PM3_WRITE_REG(PM3ForegroundColor, c);
		
		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(x1)) |
			      (PM3RectanglePosition_YOffset(y1)));
		
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(width)) |
			      (PM3Render2D_Height(height)));
	} else {
		PM3_WAIT(4);
		
		PM3_WRITE_REG(PM3FBBlockColor, c);
		
		PM3_WRITE_REG(PM3Config2D,
			      PM3Config2D_UseConstantSource |
			      PM3Config2D_ForegroundROPEnable |
			      (PM3Config2D_ForegroundROP(GXrop)) |
			      PM3Config2D_FBWriteEnable);
		
		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(x1)) |
			      (PM3RectanglePosition_YOffset(y1)));
		
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      (PM3Render2D_Width(width)) |
			      (PM3Render2D_Height(height)));
	}
	
	pm3fb_wait_pm3(l_fb_info);
}

static void pm3fb_fillrect_32bpp(struct pm3fb_info *l_fb_info, int x1, int y1, unsigned int width,
				 unsigned int height, unsigned long c, int GXrop)
{
	if (l_fb_info->current_par->width > 1600) {
		PM3_WAIT(4);
		
		PM3_WRITE_REG(PM3Config2D,
			      PM3Config2D_UseConstantSource |
			      PM3Config2D_ForegroundROPEnable |
			      (PM3Config2D_ForegroundROP(GXrop)) |
			      PM3Config2D_FBWriteEnable);
		
		PM3_WRITE_REG(PM3ForegroundColor, c);
		
		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(x1)) |
			      (PM3RectanglePosition_YOffset(y1)));
		
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      PM3Render2D_SpanOperation |
			      (PM3Render2D_Width(width)) |
			      (PM3Render2D_Height(height)));
	} else {/* block fills in 32bpp are hard, but in low res (width <= 1600 :-) we can use 16bpp operations */
		PM3_WAIT(8);
		
		PM3_WRITE_REG(PM3FBBlockColor, c);
		
		PM3_WRITE_REG(PM3PixelSize, PM3PixelSize_GLOBAL_16BIT);
		
		PM3_WRITE_REG(PM3FBWriteBufferWidth0,
			      PM3FBWriteBufferWidth_Width(l_fb_info->
							  current_par->
							  width << 1));
		
		PM3_WRITE_REG(PM3Config2D,
			      PM3Config2D_UseConstantSource |
			      PM3Config2D_ForegroundROPEnable |
			      (PM3Config2D_ForegroundROP(GXrop)) |
			      PM3Config2D_FBWriteEnable);
		
		PM3_WRITE_REG(PM3RectanglePosition,
			      (PM3RectanglePosition_XOffset(x1 << 1)) |
			      (PM3RectanglePosition_YOffset(y1)));
		
		PM3_WRITE_REG(PM3Render2D,
			      PM3Render2D_XPositive |
			      PM3Render2D_YPositive |
			      PM3Render2D_Operation_Normal |
			      (PM3Render2D_Width(width << 1)) |
			      (PM3Render2D_Height(height)));
		
		PM3_WRITE_REG(PM3FBWriteBufferWidth0,
			      PM3FBWriteBufferWidth_Width(l_fb_info->
							  current_par->
							  width));
		
		PM3_WRITE_REG(PM3PixelSize, PM3PixelSize_GLOBAL_32BIT);
	}
	
	pm3fb_wait_pm3(l_fb_info);
}

void pm3fb_fillrect(struct fb_info *info, int x1, int y1, unsigned int width,
                    unsigned int height, unsigned long color, int rop)
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	int GXrop;
	unsigned long c = color;
	
	switch(rop)
	{
	case ROP_COPY:
		GXrop = 0x3; /* GXcopy */
		break;
	case ROP_XOR:
		GXrop = 0x6; /* GXxor */
		break;
	default:
		DPRINTK(1, "Unknown ROP 0x%x\n", rop);
		GXrop = 0x3; /* assume GXcopy */
		break;
	}

	PM3_COLOR(c);
	
	switch(depth2bpp(l_fb_info->current_par->depth))
	{
	case 32:
		pm3fb_fillrect_32bpp(l_fb_info, x1, y1, width, height, c, GXrop);
		break;

	case 16:
		pm3fb_fillrect_16bpp(l_fb_info, x1, y1, width, height, c, GXrop);
		break;
		
	case 8:
		pm3fb_fillrect_8bpp(l_fb_info, x1, y1, width, height, c, GXrop);
		break;

	default:
		break;
	}

}

void pm3fb_copyarea(struct fb_info *info, int sx, int sy, unsigned int width,
		    unsigned int height, int dx, int dy)
{
       	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	int x_align, o_x, o_y;
	
	o_x = sx - dx;		/*(sx > dx ) ? (sx - dx) : (dx - sx); */
	o_y = sy - dy;		/*(sy > dy ) ? (sy - dy) : (dy - sy); */

	x_align = (sx & 0x1f);

	PM3_WAIT(6);

	PM3_WRITE_REG(PM3Config2D,
		      PM3Config2D_UserScissorEnable |
		      PM3Config2D_ForegroundROPEnable |
		      PM3Config2D_Blocking |
		      (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
		      PM3Config2D_FBWriteEnable);
	
	PM3_WRITE_REG(PM3ScissorMinXY,
		      ((dy & 0x0fff) << 16) | (dx & 0x0fff));
	PM3_WRITE_REG(PM3ScissorMaxXY,
		      (((dy + height) & 0x0fff) << 16) |
		      ((dx + width) & 0x0fff));
	
	PM3_WRITE_REG(PM3FBSourceReadBufferOffset,
		      PM3FBSourceReadBufferOffset_XOffset(o_x) |
		      PM3FBSourceReadBufferOffset_YOffset(o_y));
	
	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(dx - x_align)) |
		      (PM3RectanglePosition_YOffset(dy)));

	PM3_WRITE_REG(PM3Render2D,
		      ((sx > dx) ? PM3Render2D_XPositive : 0) |
		      ((sy > dy) ? PM3Render2D_YPositive : 0) |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      PM3Render2D_FBSourceReadEnable |
		      (PM3Render2D_Width(width + x_align)) |
		      (PM3Render2D_Height(height)));

	pm3fb_wait_pm3(l_fb_info);
}

/**
 *      xxxfb_imageblit - REQUIRED function. Can use generic routines if
 *                        non acclerated hardware and packed pixel based.
 *                        Copies a image from system memory to the screen. 
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *	@image:	structure defining the image.
 *
 *      This drawing operation draws a image on the screen. It can be a 
 *	mono image (needed for font handling) or a color image (needed for
 *	tux). 
 */
void fb_imageblit(struct fb_info *info, struct fb_image *image) 
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	unsigned long fgx, bgx, asx, asy, o_x = 0, o_y = 0, wm, ldat;
	int i;
	
	/* should be improved someday... */
	if ((image->depth != 1) || (image->width > 32) || (image->height > 32))
	{
		cfb_imageblit(info, image);
		return;
	}
	
	fgx = image->fg_color;
	bgx = image->bg_color;
	PM3_COLOR(fgx);
	PM3_COLOR(bgx);
	
	if (image->width < 32)
		wm = (1 << image->width) - 1;
	else
		wm = 0xFFFFFFFF;

#define ADRESS_SELECT(size) (size <= 8 ? 2 : (size <= 16 ? 3 : 4))
	asx = ADRESS_SELECT(image->width);
	asy = ADRESS_SELECT(image->height);
	
	PM3_WAIT(6 + image->height);
	
	PM3_WRITE_REG(PM3Config2D,
		      PM3Config2D_UseConstantSource |
		      PM3Config2D_ForegroundROPEnable |
		      (PM3Config2D_ForegroundROP(0x3)) |	/* Ox3 is GXcopy */
		      PM3Config2D_FBWriteEnable | PM3Config2D_OpaqueSpan);
	
	PM3_WRITE_REG(PM3ForegroundColor, fgx);
	PM3_WRITE_REG(PM3FillBackgroundColor, bgx);
       
	PM3_WRITE_REG(PM3AreaStippleMode,
		      (o_x << 7) | (o_y << 12) |	/* x_offset, y_offset in pattern */
		      (1 << 18) |	/* BE */
		      1 | (asx << 1) | (asy << 4) |	/* address select x/y */
		      (1 << 20));	/* OpaqueSpan */

	for (i = 0; i < image->height; i++) {
		switch (asx)
		{
		case 2: /* width <= 8 */
			ldat = ((u8*)image->data)[i] & wm;
			break;
		case 3: /* width <= 16 */
			ldat = ((u16*)image->data)[i] & wm;
			break;
		case 4: /* width <= 32 */
			ldat = ((u32*)image->data)[i] & wm;
			break;
		}
		PM3_WRITE_REG(AreaStipplePattern_indexed(i), ldat);
	}
	
	PM3_WRITE_REG(PM3RectanglePosition,
		      (PM3RectanglePosition_XOffset(image->x)) |
		      (PM3RectanglePosition_YOffset(image->y)));
	
	PM3_WRITE_REG(PM3Render2D,
		      PM3Render2D_AreaStippleEnable |
		      PM3Render2D_XPositive |
		      PM3Render2D_YPositive |
		      PM3Render2D_Operation_Normal |
		      PM3Render2D_SpanOperation |
		      (PM3Render2D_Width(image->width)) |
		      (PM3Render2D_Height(image->height)));
	
	pm3fb_wait_pm3(l_fb_info);
}

#endif /* PM3FB_USE_ACCEL */

/* ********************************************** */
/* ***** framebuffer API standard functions ***** */
/* ********************************************** */

static int pm3fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	u32 xres;
	
	DTRACE;
	
	DASSERT((var != NULL), "fb_var_screeninfo* not NULL");
	DASSERT((par != NULL), "pm3fb_par* not NULL");
	DASSERT((l_fb_info != NULL), "pm3fb_info* not NULL");
	
	if (!(depth_supported(var->bits_per_pixel)))
		return(-EINVAL);
	
	DPRINTK(2,
		"xres: %d, yres: %d, vxres: %d, vyres: %d ; xoffset:%d, yoffset: %d\n",
		var->xres, var->yres, var->xres_virtual, var->yres_virtual,
		var->xoffset, var->yoffset);
	
	xres = (var->xres + 31) & ~31;
	if ((xres + var->xoffset) > 2048) {
		DPRINTK(1, "virtual width not supported: %u\n",
			temp_p.width);
		return (-EINVAL);
	}
	if ((var->yres + var->yoffset) > 4096) {
		DPRINTK(1, "virtual height not supported: %u\n",
			(var->yres + var->yoffset));
		return (-EINVAL);
	}
	
	if (((var->yres + var->yoffset) * (xres + var->xoffset) * depth2ByPP(var->bits_per_pixel)) >
	    l_fb_info->fb_size) {
		DPRINTK(1, "no memory for screen (%ux%ux%u)\n",
			(var->yres + var->yoffset), (xres + var->xoffset), var->bits_per_pixel);
		return (-EINVAL);
	}
	
	if ((!var->pixclock) ||
	    (!var->right_margin) ||
	    (!var->hsync_len) ||
	    (!var->left_margin) ||
	    (!var->lower_margin) ||
	    (!var->vsync_len) ||
	    (!var->upper_margin))
		return (-EINVAL);

	if (PICOS2KHZ(var->pixclock) > PM3_MAX_PIXCLOCK) {
		DPRINTK(1, "pixclock too high (%uKHz)\n",
			temp_p.pixclock);
		return (-EINVAL);
	}

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		DPRINTK(1, "Interlaced mode not supported\n\n");
		return (-EINVAL);
	}
	
	return(0);	   	
}

static int pm3fb_set_par(struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	struct pm3fb_par par;
	
	DTRACE;

	pm3fb_decode_var(&(info->var), &par, l_fb_info);
	pm3fb_encode_fix(&(info->fix), &par, l_fb_info);
	
	*(l_fb_info->current_par) = par;
	current_par_valid[l_fb_info->board_num] = 1;
	
	pm3fb_write_mode(l_fb_info);

#ifdef PM3FB_USE_ACCEL
	pm3fb_init_engine(l_fb_info);
#endif /* PM3FB_USE_ACCEL */

	return(0);
}

static int pm3fb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;

	DTRACE;

	if (regno > 255)
		return(1);

	if (regno < 16) {
		switch (l_fb_info->current_par->depth) {
#ifdef FBCON_HAS_CFB8
		case 8:
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 12:
			((u16*)(info->pseudo_palette))[regno] =
				(((u32) red & 0xf000) >> 4) |
				(((u32) green & 0xf000) >> 8) |
				(((u32) blue & 0xf000) >> 12);
			break;

		case 15:
			((u16*)(info->pseudo_palette))[regno] =
				(((u32) red & 0xf800) >> 1) |
				(((u32) green & 0xf800) >> 6) |
				(((u32) blue & 0xf800) >> 11);
			break;

		case 16:
			((u16*)(info->pseudo_palette))[regno] =
			    ((u32) red & 0xf800) |
			    (((u32) green & 0xfc00) >> 5) |
			    (((u32) blue & 0xf800) >> 11);
			break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			((u32*)(info->pseudo_palette))[regno] =
			    (((u32) transp & 0xff00) << 16) |
			    (((u32) red & 0xff00) << 8) |
			    (((u32) green & 0xff00)) |
			    (((u32) blue & 0xff00) >> 8);
			break;
#endif
		default:
			DPRINTK(1, "bad depth %u\n",
				l_fb_info->current_par->depth);
			break;
		}
	}

	if (l_fb_info->current_par->depth == 8)
		pm3fb_set_color(l_fb_info, regno, red >> 8,
				green >> 8, blue >> 8);
	
	return(0);
}

static int pm3fb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	
	DTRACE;
	
	if (!current_par_valid[l_fb_info->board_num])
		return -EINVAL;
	
	l_fb_info->current_par->base =	/* in 128 bits chunk - i.e. AFTER Shiftbpp */
		pm3fb_Shiftbpp(l_fb_info,
			       l_fb_info->current_par->depth,
			       (var->yoffset * l_fb_info->current_par->width) +
			       var->xoffset);
	PM3_SLOW_WRITE_REG(PM3ScreenBase, l_fb_info->current_par->base);
	return 0;
}

static int pm3fb_blank(int blank_mode, struct fb_info *info)
{
	struct pm3fb_info *l_fb_info = ((struct pm3fb_par*)info->par)->l_fb_info;
	u32 video;

	DTRACE;

	if (!current_par_valid[l_fb_info->board_num])
		return (1);

	video = l_fb_info->current_par->video;

	if (blank_mode > 0) {
		switch (blank_mode - 1) {

		case VESA_NO_BLANKING:	/* FIXME */
			video = video & ~(PM3VideoControl_ENABLE);
			break;

		case VESA_HSYNC_SUSPEND:
			video = video & ~(PM3VideoControl_HSYNC_MASK |
					  PM3VideoControl_BLANK_ACTIVE_LOW);
			break;
		case VESA_VSYNC_SUSPEND:
			video = video & ~(PM3VideoControl_VSYNC_MASK |
					  PM3VideoControl_BLANK_ACTIVE_LOW);
			break;
		case VESA_POWERDOWN:
			video = video & ~(PM3VideoControl_HSYNC_MASK |
					  PM3VideoControl_VSYNC_MASK |
					  PM3VideoControl_BLANK_ACTIVE_LOW);
			break;
		default:
			DPRINTK(1, "Unsupported blanking %d\n",
				blank_mode);
			return (1);
			break;
		}
	}

	PM3_SLOW_WRITE_REG(PM3VideoControl, video);

	return (0);
}

/* *********************************** */
/* ***** pre-init board(s) setup ***** */
/* *********************************** */

static void pm3fb_mode_setup(char *mode, unsigned long board_num)
{
	struct pm3fb_info *l_fb_info = &(pm3fb_fb_info[board_num]);
/*
	struct pm3fb_par *l_fb_par = &(current_par[board_num]);
	unsigned long i = 0;
*/

	current_par_valid[board_num] = 0;

	if (!strncmp(mode, "current", 7)) {
		l_fb_info->use_current = 1;
	} else {
/*
		while ((mode_base[i].name[0])
		       && (!current_par_valid[board_num])) {
			if (!
			    (strncmp
			     (mode, mode_base[i].name,
			      strlen(mode_base[i].name)))) {
				memcpy(l_fb_par, &(mode_base[i].user_mode),
				       sizeof(struct pm3fb_par));
				current_par_valid[board_num] = 1;
				DPRINTK(2, "Mode set to %s\n",
				mode_base[i].name);
			}
			i++;
*/
		int retval;
		retval = fb_find_mode(&(l_fb_info->current_par->f_fb_info->var), l_fb_info->current_par->f_fb_info, mode, NULL, 0, NULL, 8);
		if (!retval || retval == 4)
			current_par_valid[board_num] = 0;
		else
		{
			current_par_valid[board_num] = 1;
			pm3fb_decode_var(&(l_fb_info->current_par->f_fb_info->var),
					 l_fb_info->current_par,
					 l_fb_info);
		}
	}
	DASSERT(current_par_valid[board_num],
		"Valid mode on command line\n");
}

static void pm3fb_pciid_setup(char *pciid, unsigned long board_num)
{
	short l_bus = -1, l_slot = -1, l_func = -1;
	char *next;

	if (pciid) {
		l_bus = simple_strtoul(pciid, &next, 10);
		if (next && (next[0] == ':')) {
			pciid = next + 1;
			l_slot = simple_strtoul(pciid, &next, 10);
			if (next && (next[0] == ':')) {
				pciid = next + 1;
				l_func =
				    simple_strtoul(pciid, (char **) NULL,
						   10);
			}
		}
	} else
		return;

	if ((l_bus >= 0) && (l_slot >= 0) && (l_func >= 0)) {
		bus[board_num] = l_bus;
		slot[board_num] = l_slot;
		func[board_num] = l_func;
		DPRINTK(2, "Board #%ld will be PciId: %hd:%hd:%hd\n",
			board_num, l_bus, l_slot, l_func);
	} else {
		DPRINTK(1, "Invalid PciId: %hd:%hd:%hd for board #%ld\n",
			l_bus, l_slot, l_func, board_num);
	}
}

static void pm3fb_font_setup(char *lf, unsigned long board_num)
{
	unsigned long lfs = strlen(lf);

	if (lfs > (PM3_FONTNAME_SIZE - 1)) {
		DPRINTK(1, "Fontname %s too long\n", lf);
		return;
	}
	strncpy(fontn[board_num], lf, lfs);
	fontn[board_num][lfs] = '\0';
}

static void pm3fb_bootdepth_setup(char *bds, unsigned long board_num)
{
	unsigned long bd = simple_strtoul(bds, (char **) NULL, 10);

	if (!(depth_supported(bd))) {
		DPRINTK(1, "Invalid depth: %s\n", bds);
		return;
	}
	depth[board_num] = bd;
}

static char *pm3fb_boardnum_setup(char *options, unsigned long *bn)
{
	char *next;

	if (!(CHAR_IS_NUM(options[0]))) {
		(*bn) = 0;
		return (options);
	}

	(*bn) = simple_strtoul(options, &next, 10);

	if (next && (next[0] == ':') && ((*bn) >= 0)
	    && ((*bn) <= PM3_MAX_BOARD)) {
		DPRINTK(2, "Board_num seen as %ld\n", (*bn));
		return (next + 1);
	} else {
		(*bn) = 0;
		DPRINTK(2, "Board_num default to %ld\n", (*bn));
		return (options);
	}
}

static void pm3fb_real_setup(char *options)
{
	char *next;
	unsigned long i, bn;
	struct pm3fb_info *l_fb_info;

	DTRACE;

	DPRINTK(2, "Options : %s\n", options);

	for (i = 0; i < PM3_MAX_BOARD; i++) {
		l_fb_info = &(pm3fb_fb_info[i]);
		memset(l_fb_info, 0, sizeof(struct pm3fb_info));
		l_fb_info->board_num = i;
		current_par_valid[i] = 0;
		slot[i] = -1;
		func[i] = -1;
		bus[i] = -1;
		disable[i] = 0;
		noaccel[i] = 0;
		fontn[i][0] = '\0';
		depth[i] = 0;
		l_fb_info->current_par = &(current_par[i]);
		l_fb_info->current_par->l_fb_info = l_fb_info;
		l_fb_info->current_par->f_fb_info = &(fbcon_fb_info[i]);
	}

	/* eat up prefix pm3fb and whatever is used as separator i.e. :,= */
	if (!strncmp(options, "pm3fb", 5)) {
		options += 5;
		while (((*options) == ',') || ((*options) == ':')
		       || ((*options) == '='))
			options++;
	}

	while (options) {
		bn = 0;
		if ((next = strchr(options, ','))) {
			(*next) = '\0';
			next++;
		}

		if (!strncmp(options, "mode:", 5)) {
			options = pm3fb_boardnum_setup(options + 5, &bn);
			DPRINTK(2, "Setting mode for board #%ld\n", bn);
			pm3fb_mode_setup(options, bn);
		} else if (!strncmp(options, "off:", 4)) {
			options = pm3fb_boardnum_setup(options + 4, &bn);
			DPRINTK(2, "Disabling board #%ld\n", bn);
			disable[bn] = 1;
		} else if (!strncmp(options, "off", 3)) {	/* disable everything */
			for (i = 0; i < PM3_MAX_BOARD; i++)
				disable[i] = 1;
		} else if (!strncmp(options, "disable:", 8)) {
			options = pm3fb_boardnum_setup(options + 8, &bn);
			DPRINTK(2, "Disabling board #%ld\n", bn);
			disable[bn] = 1;
		} else if (!strncmp(options, "pciid:", 6)) {
			options = pm3fb_boardnum_setup(options + 6, &bn);
			DPRINTK(2, "Setting PciID for board #%ld\n", bn);
			pm3fb_pciid_setup(options, bn);
		} else if (!strncmp(options, "noaccel:", 8)) {
			options = pm3fb_boardnum_setup(options + 8, &bn);
			noaccel[bn] = 1;
		} else if (!strncmp(options, "font:", 5)) {
			options = pm3fb_boardnum_setup(options + 5, &bn);
			pm3fb_font_setup(options, bn);
		} else if (!strncmp(options, "depth:", 6)) {
			options = pm3fb_boardnum_setup(options + 6, &bn);
			pm3fb_bootdepth_setup(options, bn);
		}
		options = next;
	}
}

/* ****************************************** */
/* ***** standard FB API init functions ***** */
/* ****************************************** */
int __init pm3fb_init(void)
{
	DTRACE;

	DPRINTK(2, "This is pm3fb.c, CVS version: $Header$");

	pm3fb_real_setup(g_options);
	
	pm3fb_detect();

	if (!pm3fb_fb_info[0].dev) {	/* not even one board ??? */
		DPRINTK(1, "No PCI Permedia3 board detected\n");
	}
	
	return (0);
}

int __init pm3fb_setup(char *options)
{
	long opsi = strlen(options);

	DTRACE;

	memcpy(g_options, options,
	       ((opsi + 1) >
		PM3_OPTIONS_SIZE) ? PM3_OPTIONS_SIZE : (opsi + 1));
	g_options[PM3_OPTIONS_SIZE - 1] = 0;

	return (0);
}

static int pm3fb_open(struct fb_info *info, int user)
{
	DTRACE;

	MOD_INC_USE_COUNT;

	return (0);
}

static int pm3fb_release(struct fb_info *info, int user)
{
	DTRACE;

	MOD_DEC_USE_COUNT;

	return (0);
}

/* ************************* */
/* **** Module support ***** */
/* ************************* */

#ifdef MODULE
MODULE_AUTHOR("Romain Dolbeau");
MODULE_DESCRIPTION("Permedia3 framebuffer device driver");
static char *mode[PM3_MAX_BOARD];
MODULE_PARM(mode,PM3_MAX_BOARD_MODULE_ARRAY_STRING);
MODULE_PARM_DESC(mode,"video mode");
MODULE_PARM(disable,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(disable,"disable board");
static short off[PM3_MAX_BOARD];
MODULE_PARM(off,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(off,"disable board");
static char *pciid[PM3_MAX_BOARD];
MODULE_PARM(pciid,PM3_MAX_BOARD_MODULE_ARRAY_STRING);
MODULE_PARM_DESC(pciid,"board PCI Id");
MODULE_PARM(noaccel,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(noaccel,"disable accel");
static char *font[PM3_MAX_BOARD];
MODULE_PARM(font,PM3_MAX_BOARD_MODULE_ARRAY_STRING);
MODULE_PARM_DESC(font,"choose font");
MODULE_PARM(depth,PM3_MAX_BOARD_MODULE_ARRAY_SHORT);
MODULE_PARM_DESC(depth,"boot-time depth");
/*
MODULE_SUPPORTED_DEVICE("Permedia3 PCI boards")
MODULE_GENERIC_TABLE(gtype,name)
MODULE_DEVICE_TABLE(type,name)
*/

void pm3fb_build_options(void)
{
	int i;
	char ts[128];

	strcpy(g_options, "pm3fb");
	for (i = 0; i < PM3_MAX_BOARD ; i++)
	{
		if (mode[i])
		{
			sprintf(ts, ",mode:%d:%s", i, mode[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (disable[i] || off[i])
		{
			sprintf(ts, ",disable:%d:", i);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (pciid[i])
		{
			sprintf(ts, ",pciid:%d:%s", i, pciid[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (noaccel[i])
		{
			sprintf(ts, ",noaccel:%d:", i);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (font[i])
		{
			sprintf(ts, ",font:%d:%s", i, font[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
		if (depth[i])
		{
			sprintf(ts, ",depth:%d:%d", i, depth[i]);
			strncat(g_options, ts, PM3_OPTIONS_SIZE - strlen(g_options));
		}
	}
	g_options[PM3_OPTIONS_SIZE - 1] = '\0';
	DPRINTK(1, "pm3fb use options: %s\n", g_options);
}

int pm3fb_init_module(void)
{
	DTRACE;

	pm3fb_build_options();

	pm3fb_init();

	return (0);
}

void pm3fb_cleanup_module(void)
{
	DTRACE;
	{
		unsigned long i;
		struct pm3fb_info *l_fb_info;
		for (i = 0; i < PM3_MAX_BOARD; i++) {
			l_fb_info = &(pm3fb_fb_info[i]);
			if ((l_fb_info->dev != NULL)
			    && (!(disable[l_fb_info->board_num]))) {
				if (l_fb_info->vIOBase !=
				    (unsigned char *) -1) {
					pm3fb_unmapIO(l_fb_info);
					release_mem_region(l_fb_info->p_fb,
							   l_fb_info->
							   fb_size);
					release_mem_region(l_fb_info->
							   pIOBase,
							   PM3_REGS_SIZE);
				}
				unregister_framebuffer(l_fb_info->current_par->f_fb_info);
			}
		}
	}
	return;
}
module_init(pm3fb_init_module);
module_exit(pm3fb_cleanup_module);
#endif /* MODULE */
