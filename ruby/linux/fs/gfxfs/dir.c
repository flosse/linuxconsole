/*
 * fs/gfxfs/dir.c
 *
 * gfxfs directory operations
 *
 * Copyright (C) 2001 Paul Mundt <pmundt@mvista.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#include <linux/gfxfs_fs.h>
#include <linux/dcache.h>

/* FIXME: Keep things happy for now, add our own later */
struct file_operations gfxfs_dir_ops = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
};

/* Ditto */
struct inode_operations gfxfs_dir_inode_ops = {
	/* Empty for now */
};

