/*
 * fs/gfxfs/file.c
 *
 * gfxfs file operations
 *
 * Copyright (C) 2001 Paul Mundt <pmundt@mvista.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#include <linux/gfxfs_fs.h>

/*
 * FIXME: Use generic routines for now, add our own later.
 */
struct file_operations gfxfs_file_ops = {
	mmap:		generic_file_mmap,
	llseek:		generic_file_llseek,
};

