/*
 * include/linux/gfxfs_fs.h
 *
 * Primary GfxFS header.
 *
 * Copyright (C) 2001 Paul Mundt <pmundt@mvista.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#ifndef __GFXFS_FS_H
#define __GFXFS_FS_H

#include <linux/fs.h>
#include <linux/config.h>

#define GFXFS_SUPER_MAGIC	0x66786667	/* "gfxfs" */

#ifdef CONFIG_GFXFS_FS_DEBUG
  #define gfxfs_debug(x...)	printk(KERN_DEBUG "gfxfs: " \
  				       __FUNCTION__ "(): " ##x)
#else
  #define gfxfs_debug(x...)	do { } while (0)
#endif /* CONFIG_GFXFS_FS_DEBUG */

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

/* fs/gfxfs/inode.c */
extern void gfxfs_read_inode(struct inode *inode);
extern struct inode *gfxfs_get_inode(struct super_block *sb, int mode, int dev);

/* fs/gfxfs/file.c */
extern struct file_operations gfxfs_file_ops;

/* fs/gfxfs/dir.c */
extern struct file_operations gfxfs_dir_ops;
extern struct inode_operations gfxfs_dir_inode_ops;

extern int gfxfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev);

#endif /* __GFXFS_FS_H */

