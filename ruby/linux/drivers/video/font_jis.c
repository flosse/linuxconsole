/*
 * linux/drivers/video/font_jis.c
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
#include <linux/init.h>
#include "jis16.h"
#define min1 0xa1
#define max1 0xf9


int is_left(int c)
{
	return ( c >= min1 && c<=max1);
}
int is_right(int c)
{
	return ( c >= 0x40 && c<=0x7e || c>=0xa1 && c <= 0xfe);
}
int index_jis (int ch1, int ch2)
{
	if (!is_left(ch1) || !is_right(ch2)) return -512;
	if (ch1 > 0x2A)
		return ((ch2 - 0x41 + (ch1 - 0x26) * 96) << 5);
	else
		return ((ch2 - 0x21 + (ch1 - 0x21) * 96) << 5);
}
struct double_byte db_gb =
{
	0,
	"JIS5",
	is_left,
	is_right,
	index_jis,
	16,16,
	max_jis16,
	font_jis16
};

int __init jis_init(void)
{
	if (doublebyte_default) return 1;
	doublebyte_default = &db_gb;
	return 0;
}

void __exit jis_exit(void)
{
	doublebyte_default = (void*) 0;
}	

module_init(jis_init);
module_exit(jis_exit);

