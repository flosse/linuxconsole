/*
 * fs/inputfs/dir.c
 *
 * inputfs directory operations
 *
 * Copyright (C) 2001 James Simmons <jsimmons@transvirtual.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#include <linux/inputfs_fs.h>
#include <linux/dcache.h>

/* FIXME: Keep things happy for now, add our own later */
struct file_operations inputfs_dir_ops = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
};

/* Ditto */
struct inode_operations inputfs_dir_inode_ops = {
	/* Empty for now */
};

