/*
 * linux/video/encode_gb.c
 *
 * Copyright (C) 1999		Christopher Li, Jim Chen
 *				GNU/Linux Research Center
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 *
 */


#include <linux/module.h>
#include <linux/fb_doublebyte.h>
#include "font_gb16.h"
#define min1 0xa1
#define max1 0xfe
int index_gb(int left, int right)
{
	return ((left-min1)*94+right-min1) << 5;
}
int is_hz(int c)
{
	return ( c >= min1 && c<=max1);
}
struct double_byte db_gb =
{
	0,
	"GB",
	is_hz,
	is_hz,
	index_gb,
	16,16,
	max_gb16,
	font_gb16
};

int init_module(void)
{
	if (doublebyte_default) return 1;
	doublebyte_default = &db_gb;
	return 0;
}

void cleanup_module(void)
{
	doublebyte_default = (void*) 0;
}	
