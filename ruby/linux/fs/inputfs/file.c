/*
 * fs/inputfs/file.c
 *
 * inputfs file operations
 *
 * Copyright (C) 2001 James Simmons <jsimmons@transvirtual.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#include <linux/inputfs_fs.h>

/*
 * FIXME: Use generic routines for now, add our own later.
 */
struct file_operations inputfs_file_ops = {
	mmap:		generic_file_mmap,
	llseek:		generic_file_llseek,
};

