/*
 * fs/gfxfs/super.c
 *
 * gfxfs superblock operations
 *
 * Copyright (C) 2001 Paul Mundt <pmundt@mvista.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/gfxfs_fs.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>

static struct vfsmount *gfxfs_mnt;

static int gfxfs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type    = GFXFS_SUPER_MAGIC;
	buf->f_bsize   = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;

	return 0;
}

static struct super_operations gfxfs_super_ops = {
	statfs:		gfxfs_statfs,
};

static struct super_block *gfxfs_read_super(struct super_block *sb,
					    void *data, int silent)
{
	struct inode *root_inode;

	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic          = GFXFS_SUPER_MAGIC;
	sb->s_op             = &gfxfs_super_ops;

	root_inode = gfxfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!root_inode) {
		printk(KERN_WARNING "gfxfs: Unable to get root inode\n");
		return NULL;
	}

	sb->s_root = d_alloc_root(root_inode);

	if (!sb->s_root) {
		printk(KERN_WARNING "gfxfs: Unable to get root dentry\n");
		iput(root_inode);
		return NULL;
	}

	return sb;
}

static DECLARE_FSTYPE(gfxfs_fs_type, "gfxfs", gfxfs_read_super, FS_SINGLE);

static int __init init_gfxfs_fs(void)
{
	int ret = register_filesystem(&gfxfs_fs_type);

	if (!ret) {
		gfxfs_mnt = kern_mount(&gfxfs_fs_type);
		ret = PTR_ERR(gfxfs_mnt);

		if (!IS_ERR(gfxfs_mnt)) {
			ret = 0;
		}
	}

	return ret;
}

static void __exit exit_gfxfs_fs(void)
{
	kern_umount(gfxfs_mnt);
	unregister_filesystem(&gfxfs_fs_type);
}

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("Paul Mundt <pmundt@mvista.com>");
MODULE_DESCRIPTION("graphics file system");
MODULE_LICENSE("GPL");

module_init(init_gfxfs_fs);
module_exit(exit_gfxfs_fs);

