/* $Header$ */
/*
 * Copyright (C) 2001 Romain Dolbeau <dolbeau@irisa.fr>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 *
 */

#include <stdio.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
extern char *optarg;
extern int optind, opterr, optopt;

#include <errno.h>
int errno;

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <asm/page.h>
#include <asm/byteorder.h>

#include <linux/fb.h>
#include <linux/fbvid.h>



int
main(int argc, char **argv)
{
	char fbdevice[256];
	int fbnum = 0, opnu = 0, sx = 128, sy = 128, wi = 64, he = 64, color = 0xFFFFFFFF, nsx, nsy, count = 0;
	unsigned char *fb_mem = NULL;
	int fb_mem_offset = 0, fbd, r;
	char *tmpoptarg;
	struct fb_var_screeninfo fb_var;
	struct fb_fix_screeninfo fb_fix;
	struct fb_fillrect fbfr;
	struct fb_copyarea fbca;
	
	while ((opnu = getopt(argc, argv, "f:x:y:w:h:c:")) != EOF)
	{
		switch(opnu)
		{
		case 'f':
			tmpoptarg = optarg;
			if (!(strncmp(tmpoptarg, "/dev/", 5)))
				tmpoptarg += 5;
			if (!(strncmp(tmpoptarg, "fb", 2)))
				tmpoptarg += 2;
			fbnum = atoi(tmpoptarg);
			break;
		case 'x':
			sx = atoi(optarg);
			break;
		case 'y':
			sy = atoi(optarg);
			break;
		case 'w':
			wi = atoi(optarg);
			break;
		case 'h':
			he = atoi(optarg);
			break;
		case 'c':
			color = atoi(optarg);
			break
		default:
			fprintf(stderr, "Warning: Unknown option \"%c\"\n", opnu);
		}
	}

	fprintf(stderr, "Opening /dev/fb%d\n", fbnum);
	
	sprintf(fbdevice, "/dev/fb%d", fbnum);
	fbd = open(fbdevice, O_RDWR);
	
	if (fbd < 0)
	{
		fprintf(stderr, "Couldn't open /dev/fb%d; errno: %d (%s)\n", fbnum, errno, strerror(errno));
		exit(1);
	}
	r = ioctl(fbd, FBIOGET_VSCREENINFO, &fb_var);
	if (r < 0)
	{
		fprintf(stderr, "IOCTL FBIOGET_VSCREENINFO error: %d errno: %d (%s)\n", r, errno, strerror(errno));
		exit(1);
	}
	r = ioctl(fbd, FBIOGET_FSCREENINFO, &fb_fix);
	if (r < 0)
	{
		fprintf(stderr, "IOCTL FBIOGET_FSCREENINFO error: %d errno: %d (%s)\n", r, errno, strerror(errno));
		exit(1);
	}
	/* map all FB memory */
	fb_mem_offset = (unsigned long)(fb_fix.smem_start) & (~PAGE_MASK);
	fb_mem = mmap(NULL,fb_fix.smem_len+fb_mem_offset,PROT_WRITE,MAP_SHARED,fbd,0);
	if (!fb_mem)
	{
		fprintf(stderr, "MMap of /dev/fb%d failed\n", fbnum);
		exit(1);
	}
	if (((sx + wi) > fb_var.xres) || ((sy + he) > fb_var.yres))
	{
		fprintf(stderr, "Rectangle too big for given offset (%dx%d+%d+%d, fb is %dx%d)",
			he, wi, sx, sy,
			fb_var.xres, fb_var.xres);
		exit(1);
	}
	fprintf(stderr, "Filling rect with color 0x%x\n", color);
	fbfr.x1 = sx;
	fbfr.y1 = sy;
	fbfr.width = wi;
	fbfr.height = he;
	fbfr.color = color;
	fbfr.rop = ROP_COPY;
	r = ioctl(fbd, FBIOPUT_FILLRECT, &fbfr);
	if (r < 0)
	{
		fprintf(stderr, "IOCTL FBIOPUT_FILLRECT error: %d errno: %d (%s)\n", r, errno, strerror(errno));
		exit(1);
	}
	if ((sx == 0) || (sy == 0))
	{
		fprintf(stderr, "Can't CopyArea, stop\n");
		return(0);
	}	
	nsx = sx; nsy = sy;
	fbca.width = width + 1;
	fbca.height = height + 1;
	while (((nsx + wi) < fb_var.xres) ||
	       ((nsy + he) < fb_var.yres))
	{
		count++;
		if ((nsx + wi) < fb_var.xres)
		{
			sx = nsx;
			nsx++;
		}
		if ((nsy + he) < fb_var.yres)
		{
			sy = nsy;
			nsy++;
		}
		fbca.sx = sx - 1;
		fbca.sy = sy - 1;
		fbca.dx = nsx;
		fbca.dy = nsy;
		r = ioctl(fbd, FBIOPUT_COPYAREA, &fbca);
		if (r < 0)
		{
			fprintf(stderr, "IOCTL FBIOPUT_FILLRECT #%d error: %d errno: %d (%s)\n",
				count, r, errno, strerror(errno));
			exit(1);
		}
	}
	return(0);
}
