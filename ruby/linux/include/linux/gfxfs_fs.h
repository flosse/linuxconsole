/*
 * include/linux/gfxfs_fs.h
 *
 * Primary GfxFS header.
 *
 * Copyright (C) 2001 Paul Mundt <pmundt@mvista.com>
 *
 */
#ifndef __GFXFS_FS_H
#define __GFXFS_FS_H

#include <linux/fs.h>

#define GFXFS_SUPER_MAGIC	0x8048494	/* "gfxfs" */

/*
 * struct gfxfs_dentry - gfxfs dentry
 *
 * @f_dentry: Pointer to underlying dentry.
 * @f_ops: File operations associated with given dentry.
 *
 * Each gfxfs dentry has the ability for callbacks to be
 * registered with it upon allocation. In the event a
 * callback is registered, it gets executed in place of
 * the generic dcache routines. For all other operations,
 * we rely on generic routines provided by the dcache.
 *
 * Notably, the registered file operations callbacks
 * only apply to regular (S_IFREG) files.
 */
struct gfxfs_dentry {
	struct dentry *f_dentry;
	struct file_operations *f_ops;
};

#endif /* __GFXFS_FS_H */

