/*
 *  linux/include/linux/fbvid.h -- linux FrameBuffer VIDeo Interface
 *  
 *  Copyright (C) 2001 Romain Dolbeau <dolbeau@irisa.fr>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 *
 *  $Header$
 *
 */

/*
 * This header describe the interface to add video overlay capability
 * to a framebuffer driver.
 *
 *               *********************************************
 *               ** it's preliminary and not yet approved   **
 *               ** any comments and criticisms are welcome **
 *               ** please use the linux-console list at :  **
 *               **                                         **
 *               ** linuxconsole-dev@lists.sourceforge.net  **
 *               *********************************************
 *
 * The driver must implement all 6 IOCTLS below.
 *
 * An app using this interface should :
 * 1) open the FB device (/dev/fbX), using open(). it must be Read/Write !
 * 2) get the FB capability via FBIOGET_VSCREENINFO/FBIOGET_FSCREENINFO (see linux/fb.h)
 * 3) get the FB overlay capability via first FBIOGET_VIDOVERLAY_CAP, then
 *    FBIOGET_VIDOVERLAY_FOURCC
 *    It's the responsability of the app to convert data to acceptable format.
 *    If the app and driver can't find a common ground, the app should fail.
 * 4) get a proper buffer via FBIOGET_VIDOVERLAY_ALLOCATEBUF. note that
 *    the requested xsize/ysize may be changed by the driver for internal reason,
 *    only the output values should be used.
 *    WARNING: the returned adress is physical, like the smem_start field
 *    returned by FBIOGET_FSCREENINFO. An app should map the entire FB memory
 *    via mmap and use (omem_start - smem_start) as the offset to access
 *    the overlay buffer. On hardware with two different memory mapping, we're
 *    in trouble...
 * 5) then, the app can fill the buffer and use FBIOPUT_VIDOVERLAY_START/
 *    FBIOPUT_VIDOVERLAY_STOP.O f course, the content of the buffer can be
 *    changed on-the-fly. The pair of ioctl can be used as often as the app like,
 *    using the same buffer _with the same kind of data_.
 * 6) the app will then free the buffer via FBIOGET_VIDOVERLAY_FREEBUF, and close
 *
 * NOTE: it's possible to allocate multiple buffers at once, but only one
 *       can be active at any moment. The buffers are not required to be of
 *       the same kind.
 */

#ifndef _LINUX_FBVID_H
#define LINUX_FBVID_H

#include <linux/fb.h>
#include <asm/types.h>
#include <asm/ioctl.h>

/* FOURCC #define, from the MPlayer list (see <http://mplayer.sourceforge.net/> */
/* RGB/BGR Formats */
#define FB_VIDOVERLAY_FOURCC_RGB (('R'<<24)|('G'<<16)|('B'<<8))
#define FB_VIDOVERLAY_FOURCC_BGR (('B'<<24)|('G'<<16)|('R'<<8))

/* Planar YUV Formats */
#define FB_VIDOVERLAY_FOURCC_YVU9 0x39555659
#define FB_VIDOVERLAY_FOURCC_IF09 0x39304649
#define FB_VIDOVERLAY_FOURCC_YV12 0x32315659
#define FB_VIDOVERLAY_FOURCC_I420 0x30323449
#define FB_VIDOVERLAY_FOURCC_IYUV 0x56555949
#define FB_VIDOVERLAY_FOURCC_CLPL 0x4C504C43

/* Packed YUV Formats */
#define FB_VIDOVERLAY_FOURCC_IYU1 0x31555949
#define FB_VIDOVERLAY_FOURCC_IYU2 0x32555949
#define FB_VIDOVERLAY_FOURCC_UYVY 0x59565955
#define FB_VIDOVERLAY_FOURCC_UYNV 0x564E5955
#define FB_VIDOVERLAY_FOURCC_cyuv 0x76757963
#define FB_VIDOVERLAY_FOURCC_YUY2 0x32595559
#define FB_VIDOVERLAY_FOURCC_YUNV 0x564E5559
#define FB_VIDOVERLAY_FOURCC_YVYU 0x55595659
#define FB_VIDOVERLAY_FOURCC_Y41P 0x50313459
#define FB_VIDOVERLAY_FOURCC_Y211 0x31313259
#define FB_VIDOVERLAY_FOURCC_Y41T 0x54313459
#define FB_VIDOVERLAY_FOURCC_Y42T 0x54323459
#define FB_VIDOVERLAY_FOURCC_V422 0x32323456
#define FB_VIDOVERLAY_FOURCC_V655 0x35353656
#define FB_VIDOVERLAY_FOURCC_CLJR 0x524A4C43
#define FB_VIDOVERLAY_FOURCC_YUVP 0x50565559
#define FB_VIDOVERLAY_FOURCC_UYVP 0x50565955

/* for depth_avail */
#define FB_VIDOVERLAY_DEPTH_1BPP               0x0001
#define FB_VIDOVERLAY_DEPTH_2BPP               0x0002
#define FB_VIDOVERLAY_DEPTH_4BPP               0x0004
#define FB_VIDOVERLAY_DEPTH_8BPP               0x0008
#define FB_VIDOVERLAY_DEPTH_12BPP              0x0010
#define FB_VIDOVERLAY_DEPTH_15BPP              0x0020
#define FB_VIDOVERLAY_DEPTH_16BPP              0x0040
#define FB_VIDOVERLAY_DEPTH_24BPP              0x0080
#define FB_VIDOVERLAY_DEPTH_32BPP              0x0100

/* for capability_avail */
#define FB_VIDOVERLAY_CAPABILITY_EXPAND   0x0001 /* if overlay can be bigger than source */
#define FB_VIDOVERLAY_CAPABILITY_SHRINK   0x0002 /* if overlay can be smaller than source */
#define FB_VIDOVERLAY_CAPABILITY_BLEND    0x0004 /* if overlay can be blended with framebuffer */
#define FB_VIDOVERLAY_CAPABILITY_COLORKEY 0x0008 /* if overlay can be restricted to a colorkey */
#define FB_VIDOVERLAY_CAPABILITY_ALPHAKEY 0x0010 /* if overlay can be restricted to an alpha channel */
#define FB_VIDOVERLAY_CAPABILITY_COLORKEY_ISRANGE    0x0020  /* if the colorkey can be a range */
#define FB_VIDOVERLAY_CAPABILITY_ALPHAKEY_ISRANGE    0x0040  /* if the alphakey can be a range */
#define FB_VIDOVERLAY_CAPABILITY_COLORKEY_ISMAIN     0x0080  /* colorkey is checked against framebuffer */
#define FB_VIDOVERLAY_CAPABILITY_COLORKEY_ISOVERLAY  0x0100  /* colorkey is checked against overlay */
#define FB_VIDOVERLAY_CAPABILITY_ALPHAKEY_ISMAIN     0x0200  /* alphakey is checked against framebuffer */
#define FB_VIDOVERLAY_CAPABILITY_ALPHAKEY_ISOVERLAY  0x0400  /* alphakey is checked against overlay */

/* below, for ioctl, <- is driver-to-app, -> is app-to-driver, <-> is bidirectional (*can* be changed by driver) */

struct fb_vidoverlay_avail {
        __u32 max_buf_size;             /* <- maximum size of buffer */
	__u32 xmax;                     /* <- max width of a buffer in _pixels_ */
	__u32 ymax;                     /* <- max height of a buffer in line */
	__u8  xalign;                   /* <- required alignement of width of buffer in _byte_ */
	__u8  yalign;                   /* <- required alignement of height of buffer in line */
	__u8  min_n_buf;                /* <- minimum number of available buffer */
        __u8  num_fourcc;               /* <- number of available FOURCC */
};

struct fb_vidoverlay_fourcc {
	__u32 id;                       /* <- FOURCC id (see <http://www.webartz.com/fourcc/>) */
        __u16 depth_avail;              /* <- available depth(s) for this FOURCC */
	__u16 capability_avail;         /* <- available capability(s) for this FOURCC */
};

struct fb_vidoverlay_buf {
	unsigned long omem_start;       /* <- Start of overlay frame buffer mem (phys add) or 0 (failure) */
        __u32 omem_len;                 /* <- length of overlay frame buffer mem or 0 (failure) */
        __u32 xsize;                    /* <-> X size of buffer */
        __u32 ysize;                    /* <-> Y size of buffer */
	__u32 stride;                   /* <- line lenght in byte (can be longer than X*pixelsize due to hardware restriction */
	__u32 fourcc_id;                /* -> FOURCC that'll go in the buffer  */
	__u16 depth;                    /* -> depth that'll go in the buffer */
	__u8  n_buf;                    /* <-> number of the buffer to use (or -1 for any) */
};

struct fb_vidoverlay_set {
	__u32 oxsize;                   /* -> overlay size in pixels */
	__u32 oysize;
	__u32 dxbase;                   /* -> destination base in pixels */
	__u32 dybase;
	__u32 dxsize;                   /* -> destination size in pixels */
	__u32 dysize;
	__u32 fourcc_id;                /* -> FOURCC to use */
	__u16 depth;                    /* -> depth to use */
	__u16 capability;               /* -> what capability to use */
	__u16 blend_factor;             /* -> blending factor */
	__u16 r_key[2];                 /* -> red component of color key */
 	__u16 g_key[2];                 /* -> green component of color key */
	__u16 b_key[2];                 /* -> blue component of color key */
                                        /* note: alpha should be put in all component
					   alpha cannot be used if there's no alpha channel,
					   i.e. CI8 in 8Bpp or RGBA5650 in 16bpp
					   [0] is low-end of range
					   [1] is high-end of range
					   for single value, [0] shouldbe equal to [1] */
	__u8  n_buf;                    /* -> number of the buffer to use, must have been allocated before */
};

/* get the hardware capability. get a 'struct fb_vidoverlay_avail' as output from the driver */ 
#define FBIOGET_VIDOVERLAY_CAP           _IOR( 'F', 0xF0, struct fb_vidoverlay_avail)
/* get the list of available FOURCC. variable-length data, use FBIOGET_VIDOVERLAY_CAP before */
#define FBIOGET_VIDOVERLAY_FOURCC        _IOR( 'F', 0xF1, struct fb_vidoverlay_fourcc)
/* get an offscreen memory buffer. 'struct fb_vidoverlay_buf' as input/output */
#define FBIOGET_VIDOVERLAY_ALLOCATEBUF   _IOWR('F', 0xF2, struct fb_vidoverlay_buf)
/* free the memory buffer. '__u8' number of buffer to free */
#define FBIOGET_VIDOVERLAY_FREEBUF       _IOW( 'F', 0xF3, __u8)
/* start the overlay. 'struct fb_vidoverlay_set' as input */
#define FBIOPUT_VIDOVERLAY_START         _IOW( 'F', 0xFE, struct fb_vidoverlay_set)
/* stop the overlay */
#define FBIOPUT_VIDOVERLAY_STOP          _IO(  'F', 0xFF)

#endif /* LINUX_FBVID_H */
