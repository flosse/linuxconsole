/*
 *  linux/drivers/video/vfb.c -- Virtual frame buffer device
 *
 *      Copyright (C) 1999 James Simmons
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  I have started rewriting this driver as a example of the upcoming new API
 *  The primary goal is to remove the console code from fbdev and place it 
 *  into fbcon.c. This reduces the code and makes writing a new fbdev driver
 *  easy since the author doesn't need to worry about console internals.
 * 
 *  First the roles of struct fb_info and struct display have changed. For 
 *  each framebuffer device you can allocate a set of virtual terminals to 
 *  it. Only one virtual terminal can be active per framebuffer device.
 *  So I have struct fb_info represent the current hardware state of the 
 *  framebuffer. Meaning the resolution of the active VT (the one you're 
 *  looking at) and other data is stored in the fb_info struct. When you VT 
 *  switch the current video state then is stored into struct display for that 
 *  terminal you just switched away from. Then the current video state is set
 *  to the data values stored in struct display for the VT you are switching
 *  too. As you can see doing this makes the con parameter pretty much useless
 *  for the fb_ops functions. As it should be. Since struct display is used to
 *  represent the video state of the hardware, for each terminal it also 
 *  represents the extra parameters for a framebuffer device to act as a 
 *  console terminal. In the future these parameters will be handled inside
 *  of fbcon.c so they will be of no concern to the driver writer.   
 *
 *  Also having fb_var_screeninfo and other data in fb_info pretty much 
 *  eliminates the need for get_fix and get_var. Once all drivers use the
 *  fix, var, and cmap field fbcon can be written around these fields. This 
 *  will also eliminate the need to regenerate fb_var_screeninfo and 
 *  fb_fix_screeninfo data every time the get_var and get_fix functions are
 *  called as many drivers do now. The fb_var_screeninfo and 
 *  fb_fix_screeninfo field in fb_info can be generated just in set_var and
 *  placed into struct fb_info. 
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

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
#include <asm/uaccess.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <video/fbcon.h>
#include <video/fbcon-mfb.h>
#include <video/fbcon-cfb2.h>
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb24.h>
#include <video/fbcon-cfb32.h>


#define arraysize(x)	(sizeof(x)/sizeof(*(x)))


    /*
     *  RAM we reserve for the frame buffer. This defines the maximum screen
     *  size
     *
     *  The default can be overridden if the driver is compiled as a module
     */

#define VIDEOMEMSIZE	(1*1024*1024)	/* 1 MB */

static void* videomemory;
static u_long videomemorysize = VIDEOMEMSIZE;
MODULE_PARM(videomemorysize, "l");
static struct display disp;
static struct fb_info fb_info;
static u32 vfb_pseudo_palette[17];

static struct fb_var_screeninfo vfb_default __initdata = {
    /* 640x480, 8 bpp */
    640, 480, 640, 480, 0, 0, 8, 0,
    {0, 8, 0}, {0, 8, 0}, {0, 8, 0}, {0, 0, 0},
    0, 0, -1, -1, 0, 20000, 64, 64, 32, 32, 64, 2,
    0, FB_VMODE_NONINTERLACED
};

#ifndef MODULE
/* default modedb mode */
static struct fb_videomode default_mode __initdata = {
    /* 640x480, 60 Hz, Non-Interlaced (25.172 MHz dotclock) */
    NULL, 60, 640, 480, 39722, 48, 16, 33, 10, 96, 2,
    0, FB_VMODE_NONINTERLACED
};
#endif

static struct fb_fix_screeninfo vfb_fix __initdata = {
    "Virtual FB", (unsigned long) NULL, 0, FB_TYPE_PACKED_PIXELS, 0,
    FB_VISUAL_PSEUDOCOLOR, 1, 1, 1, 0, (unsigned long) NULL, 0, FB_ACCEL_NONE
};

static int vfb_enable __initdata = 0;	/* disabled by default */
MODULE_PARM(vfb_enable, "i");

    /*
     *  Interface used by the world
     */
int vfb_init(void);
int vfb_setup(char*);

static int vfb_open(struct fb_info *info, int user);
static int vfb_release(struct fb_info *info, int user);
static int vfb_get_fix(struct fb_fix_screeninfo *fix, int con,
		       struct fb_info *info);
static int vfb_get_var(struct fb_var_screeninfo *var, int con,
		       struct fb_info *info);
static int vfb_set_var(struct fb_var_screeninfo *var, int con,
		       struct fb_info *info);
static int vfb_pan_display(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info);
static int vfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int vfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info);
static int vfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		     u_long arg, int con, struct fb_info *info);
static int vfb_mmap(struct fb_info *info, struct file *file,
                    struct vm_area_struct *vma);

    /*
     *  Interface to the low level console driver
     */

static int vfbcon_switch(int con, struct fb_info *info);
static int vfbcon_updatevar(int con, struct fb_info *info);
static void vfbcon_blank(int blank, struct fb_info *info);

    /*
     *  Internal routines
     */

static u_long get_line_length(int xres_virtual, int bpp);
static int vfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info);
static int vfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);

static struct fb_ops vfb_ops = {
    vfb_open, vfb_release, vfb_get_fix, vfb_get_var, vfb_set_var, vfb_get_cmap,
    vfb_set_cmap, vfb_pan_display, vfb_ioctl, vfb_mmap
};


    /*
     *  Open/Release the frame buffer device
     */

static int vfb_open(struct fb_info *info, int user)
{
    /*                                                                     
     *  Nothing, only a usage count for the moment                          
     */                                                                    

    MOD_INC_USE_COUNT;
    return(0);                              
}
        
static int vfb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return(0);                                                    
}


    /*
     *  Get the Fixed Part of the Display
     */

static int vfb_get_fix(struct fb_fix_screeninfo *fix, int con,
		       struct fb_info *info)
{
    *fix = info->fix;
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int vfb_get_var(struct fb_var_screeninfo *var, int con,
		       struct fb_info *info)
{
    *var = info->var;
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     *
     *  Note: This function should be split into two parts.
     *        First part, before "here hardware starts" comment,
     *        must not write anything to hardware, it should only
     *        verify and adjust var.
     *        Second part, after "here hardware starts" comment,
     *        should initialize hardware.
     *        *_switch code should call only second part, as
     *        videomode was already validated.
     *        When this function is invoked from *_switch, it
     *        should not return error, it should switch into
     *        dummycon instead of that.
     */

static int vfb_set_var(struct fb_var_screeninfo *var, int con,
		       struct fb_info *info)
{
    int err, oldbpp, activate = var->activate;
    u_long line_length;
    struct display *display;	

    if (con < 0)
       display = info->disp;
    else
       display = fb_display + con;

    /*
     *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
     *  as FB_VMODE_SMOOTH_XPAN is only used internally
     */

    if (var->vmode & FB_VMODE_CONUPDATE) {
	var->vmode |= FB_VMODE_YWRAP;
	var->xoffset = info->var.xoffset;
	var->yoffset = info->var.yoffset;
    }

    /*
     *  Some very basic checks
     */
    if (!var->xres)
	var->xres = 1;
    if (!var->yres)
	var->yres = 1;
    if (var->xres > var->xres_virtual)
	var->xres_virtual = var->xres;
    if (var->yres > var->yres_virtual)
	var->yres_virtual = var->yres;
    if (var->bits_per_pixel <= 1)
	var->bits_per_pixel = 1;
    else if (var->bits_per_pixel <= 8)
	var->bits_per_pixel = 8;
    else if (var->bits_per_pixel <= 16)
	var->bits_per_pixel = 16;
    else if (var->bits_per_pixel <= 24)
	var->bits_per_pixel = 24;
    else if (var->bits_per_pixel <= 32)
	var->bits_per_pixel = 32;
    else
	return -EINVAL;

    if (var->xres_virtual < var->xoffset+var->xres)
        var->xres_virtual = var->xoffset+var->xres;
    if (var->yres_virtual < var->yoffset+var->yres)
       var->yres_virtual = var->yoffset+var->yres;

    /*
     *  Memory limit
     */
    line_length = get_line_length(var->xres_virtual, var->bits_per_pixel);
    if (line_length*var->yres_virtual > videomemorysize)
	return -ENOMEM;

    switch (var->bits_per_pixel) {
       case 1:
       case 8:
           var->red.offset = 0;
           var->red.length = 8;
           var->green.offset = 0;
           var->green.length = 8;
           var->blue.offset = 0;
           var->blue.length = 8;
           var->transp.offset = 0;
           var->transp.length = 0;
           break;
       case 16:        /* RGBA 5551 */
           if (var->transp.length) {
               var->red.offset = 0;
               var->red.length = 5;
               var->green.offset = 5;
               var->green.length = 5;
               var->blue.offset = 10;
               var->blue.length = 5;
               var->transp.offset = 15;
               var->transp.length = 1;
           } else {    /* RGB 565 */
               var->red.offset = 0;
               var->red.length = 5;
               var->green.offset = 5;
               var->green.length = 6;
               var->blue.offset = 11;
               var->blue.length = 5;
               var->transp.offset = 0;
               var->transp.length = 0;
           }
           break;
       case 24:        /* RGB 888 */
           var->red.offset = 0;
           var->red.length = 8;
           var->green.offset = 8;
           var->green.length = 8;
           var->blue.offset = 16;
           var->blue.length = 8;
           var->transp.offset = 0;
           var->transp.length = 0;
           break;
       case 32:        /* RGBA 8888 */
           var->red.offset = 0;
           var->red.length = 8;
           var->green.offset = 8;
           var->green.length = 8;
           var->blue.offset = 16;
           var->blue.length = 8;
           var->transp.offset = 24;
           var->transp.length = 8;
           break;
    }
    var->red.msb_right = 0;
    var->green.msb_right = 0;
    var->blue.msb_right = 0;
    var->transp.msb_right = 0;

    if ((activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
	if (info->var.xres != var->xres || info->var.yres != var->yres ||
           info->var.xres_virtual != var->xres_virtual || 
           info->var.yres_virtual != var->yres_virtual ||
           info->var.bits_per_pixel != var->bits_per_pixel) {
	    /* Set the video mode. For a real video card we would fill in
             * fb_info->par which is the hardware dependent structure.
             */
	    switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_MFB
		case 1:
		    info->fix.visual = FB_VISUAL_MONO01; 
		    display->dispsw = &fbcon_mfb;
		    break;
#endif
#ifdef FBCON_HAS_CFB2
		case 2:
		    info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;
		    display->dispsw = &fbcon_cfb2;
		    break;
#endif
#ifdef FBCON_HAS_CFB4
		case 4:
		    info->fix.visual = FB_VISUAL_STATIC_PSEUDOCOLOR;	
		    display->dispsw = &fbcon_cfb4;
		    break;
#endif
#ifdef FBCON_HAS_CFB8
		case 8:
 		    info->fix.visual = FB_VISUAL_PSEUDOCOLOR;
		    display->dispsw = &fbcon_cfb8;
		    break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
		    info->fix.visual = FB_VISUAL_TRUECOLOR;
		    display->dispsw = &fbcon_cfb16;
		    display->dispsw_data = info->pseudo_palette;
		    break;
#endif
#ifdef FBCON_HAS_CFB24
		case 24:
		    info->fix.visual = FB_VISUAL_DIRECTCOLOR;	
		    display->dispsw = &fbcon_cfb24;
		    display->dispsw_data = info->pseudo_palette; 
		    break;
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
		    info->fix.visual = FB_VISUAL_DIRECTCOLOR;
		    display->dispsw = &fbcon_cfb32;
		    display->dispsw_data = info->pseudo_palette; 
		    break;
#endif
		default:
		    display->dispsw = &fbcon_dummy;
		    break;
	    }
	    info->fix.line_length = get_line_length(var->xres_virtual,
                                        		var->bits_per_pixel);
            oldbpp = var->bits_per_pixel;
	    display->var = info->var = *var;

	    display->visual = info->fix.visual;
            display->type = info->fix.type;
            display->type_aux = info->fix.type_aux;
            display->ypanstep = info->fix.ypanstep;
            display->ywrapstep = info->fix.ywrapstep;
            display->line_length = info->fix.line_length;
            display->can_soft_blank = 1;
            display->inverse = 0;
	   	
	    if (fb_info.changevar)
		(*fb_info.changevar)(con);
	}
	if (info->var.bits_per_pixel != oldbpp) {
	    if ((err = fb_set_cmap(&info->cmap, 1, vfb_setcolreg, info)))
		return err;
	}
    }
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int vfb_pan_display(struct fb_var_screeninfo *var, int con,
			   struct fb_info *info)
{
    if (var->vmode & FB_VMODE_YWRAP) {
	if (var->yoffset < 0 || var->yoffset >= info->var.yres_virtual ||
	    var->xoffset)
	    return -EINVAL;
    } else {
	if (var->xoffset + var->xres > info->var.xres_virtual ||
            var->yoffset + var->yres > info->var.yres_virtual)
            return -EINVAL;
    }
    info->var.xoffset = var->xoffset;
    info->var.yoffset = var->yoffset;
    if (var->vmode & FB_VMODE_YWRAP)
	info->var.vmode |= FB_VMODE_YWRAP;
    else
	info->var.vmode &= ~FB_VMODE_YWRAP;
    return 0;
}

    /*
     *  Get the Colormap
     */

static int vfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
    *cmap = info->cmap;	
    return 0;
}

    /*
     *  Set the Colormap
     */

static int vfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			struct fb_info *info)
{
    return fb_set_cmap(cmap, kspc, vfb_setcolreg, info); 
}


    /*
     *  Virtual Frame Buffer Specific ioctls
     */

static int vfb_ioctl(struct inode *inode, struct file *file, u_int cmd,
		     u_long arg, int con, struct fb_info *info)
{
    return -EINVAL;
}

    /*
     *  Most drivers don't need their own mmap function 
     */

static int vfb_mmap(struct fb_info *info, struct file *file,
                    struct vm_area_struct *vma)
{
    return -EINVAL;	
}

int __init vfb_setup(char *options)
{
    char *this_opt;

    vfb_enable = 1;

    if (!options || !*options)
	return 1;

    for (this_opt = strtok(options, ","); this_opt;
	 this_opt = strtok(NULL, ",")) {
	if (!strncmp(this_opt, "disable", 7))
	    vfb_enable = 0;	    
    }
    return 1;
}

    /*
     *  Initialisation
     */

int __init vfb_init(void)
{
    if (!vfb_enable)
	return -ENXIO;

    /*
     * For real video cards we use ioremap.
     */
    if (!(videomemory = vmalloc(videomemorysize)))
	return -ENOMEM;

    /*
     * VFB must clear memory to prevent kernel info
     * leakage into userspace
     * VGA-based drivers MUST NOT clear memory if
     * they want to be able to take over vgacon
     */
    memset(videomemory, 0, videomemorysize);
 
    fb_info.screen_base = fb_info.disp->screen_base = videomemory;
    strcpy(fb_info.modename, vfb_fix.id);
    fb_info.changevar = NULL;
    fb_info.node = -1;
    fb_info.fbops = &vfb_ops;

#ifndef MODULE
    if (!fb_find_mode(&fb_info.var, &fb_info, NULL,
            NULL, 0, &default_mode, 8)) {
    	fb_info.var = vfb_default;
    }
#else
    fb_info.var = vfb_default;
#endif
    fb_info.fix = vfb_fix;
    fb_info.disp = &disp;
    fb_info.switch_con = &vfbcon_switch;
    fb_info.updatevar = &vfbcon_updatevar;
    fb_info.blank = &vfbcon_blank;
    fb_info.pseudo_palette = &vfb_pseudo_palette;	
    fb_info.flags = FBINFO_FLAG_DEFAULT;
  
    /* Alloc but do not set the default color map.
       vfb_set_var will do this for us */
    fb_info.cmap.len = 1<<fb_info.var.bits_per_pixel;
    fb_alloc_cmap(&fb_info.cmap, fb_info.cmap.len, 0);	 

    vfb_set_var(&vfb_default, -1, &fb_info);	

    if (register_framebuffer(&fb_info) < 0) {
	vfree(videomemory);
	return -EINVAL;
    }

    printk(KERN_INFO "fb%d: Virtual frame buffer device, using %ldK of video memory\n", GET_FB_IDX(fb_info.node), videomemorysize>>10);
    return 0;
}

static int vfbcon_switch(int con, struct fb_info *info)
{
    /* Save the colormap and video mode */
    fb_copy_cmap(&info->cmap, &fb_display[last_console].cmap, 0);
    
    memcpy(&fb_display[last_console].var, &info->var,
		sizeof(struct fb_var_screeninfo));
    
    /* Install a new colormap and change the video mode. By default fbcon
     * sets all the colormaps and video modes to the default values at 
     * bootup.
     */
    fb_copy_cmap(&fb_display[con].cmap, &info->cmap, 0);    
    fb_set_cmap(&info->cmap, 1, vfb_setcolreg, info);
    
    memcpy(&info->var, &fb_display[con].var, 
    	    	        sizeof(struct fb_var_screeninfo));
    vfbcon_updatevar(con, info);	
    vfb_set_var(&info->var, con, &info);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int vfbcon_updatevar(int con, struct fb_info *info)
{
    /* If you have hardware and you support xoffset/yoffset, you have
     * to reprogram panning hardware */
    int err;

    if (info->fbops->fb_pan_display) {
	if ((err = info->fbops->fb_pan_display(&info->var, con, info)))
	     return err;
    }
    return 0;
}

    /*
     *  Blank the display.
     */

static void vfbcon_blank(int blank, struct fb_info *info)
{
    /* Nothing */
}

static u_long get_line_length(int xres_virtual, int bpp)
{
    u_long length;
    
    length = xres_virtual * bpp;
    length = (length+31)&~31;
    length >>= 3;
    return(length);
}
    
    /*
     * Yet another function that has become useless with the new API.	
     */

static int vfb_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info)
{
    return 0;
}

    /*
     *  Set a single color register. The values supplied are already
     *  rounded down to the hardware's capabilities (according to the
     *  entries in the var structure). Return != 0 for invalid regno.
     */

static int vfb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info)
{
    if (regno >= 256)  /* no. of hw registers */
       return 1;
    /*
     * Program hardware... do anything you want with transp
     */

    /* grayscale works only partially under directcolor */    
    if (info->var.grayscale) { 
       /* grayscale = 0.30*R + 0.59*G + 0.11*B */
       red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
    }	
    
    /* Directcolor:
     *   var->{color}.offset contains start of bitfield
     *   var->{color}.length contains length of bitfield
     *   {hardwarespecific} contains width of DAC
     *   cmap[X] is programmed to (X << red.offset) | (X << green.offset) | (X << blue.offset)
     *   DAC[X] is programmed to (red, green, blue)
     * 
     * Pseudocolor:
     *    uses offset = 0 && length = DAC register width.
     *    var->{color}.offset is 0
     *    var->{color}.length contains widht of DAC
     *    cmap is not used
     *    DAC[X] is programmed to (red, green, blue)
     * Truecolor:
     *    does not use DAC.
     *    var->{color}.offset contains start of bitfield
     *    var->{color}.length contains length of bitfield
     *    cmap is programmed to (red << red.offset) | (green << green.offset) |
     *                      (blue << blue.offset) | (transp << transp.offset)
     *    DAC does not exist
     */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
    switch (info->fix.visual) {
       case FB_VISUAL_TRUECOLOR:
       case FB_VISUAL_PSEUDOCOLOR:
               red = CNVT_TOHW(red, info->var.red.length);
               green = CNVT_TOHW(green, info->var.green.length);
               blue = CNVT_TOHW(blue, info->var.blue.length);
               transp = CNVT_TOHW(transp, info->var.transp.length);
               break;
       case FB_VISUAL_DIRECTCOLOR:
               red = CNVT_TOHW(red, 8);        /* expect 8 bit DAC */
               green = CNVT_TOHW(green, 8);
               blue = CNVT_TOHW(blue, 8);
	       /* hey, there is bug in transp handling... */
               transp = CNVT_TOHW(transp, 8);
               break;
    }
#undef CNVT_TOHW
    /* Truecolor has hardware independent palette */
    if (info->fix.visual == FB_VISUAL_TRUECOLOR) {
       u32 v;

       if (regno >= 16)
           return 1;

       v = (red << info->var.red.offset) |
           (green << info->var.green.offset) |
           (blue << info->var.blue.offset) |
           (transp << info->var.transp.offset);
       if (info->var.bits_per_pixel == 16)
           ((u16*)(info->pseudo_palette))[regno] = v;
       else
           ((u32*)(info->pseudo_palette))[regno] = v;
       return 0;
    }
    return 0;
}

#ifdef MODULE
int init_module(void)
{
    return vfb_init();
}

void cleanup_module(void)
{
    unregister_framebuffer(&fb_info);
    vfree(videomemory);
}

#endif /* MODULE */
