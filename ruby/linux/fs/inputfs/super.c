/*
 * fs/inputfs/super.c
 *
 * inputfs superblock operations
 *
 * Copyright (C) 2001 James Simmons <jsimmons@transvirtual.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/inputfs_fs.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>

static struct vfsmount *inputfs_mnt;

static int inputfs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type    = INPUTFS_SUPER_MAGIC;
	buf->f_bsize   = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;

	return 0;
}

static struct super_operations inputfs_super_ops = {
	statfs:		inputfs_statfs,
};

static struct super_block *inputfs_read_super(struct super_block *sb,
					    void *data, int silent)
{
	struct inode *root_inode;

	sb->s_blocksize      = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic          = INPUTFS_SUPER_MAGIC;
	sb->s_op             = &inputfs_super_ops;

	root_inode = inputfs_get_inode(sb, S_IFDIR | 0755, 0);

	if (!root_inode) {
		printk(KERN_WARNING "inputfs: Unable to get root inode\n");
		return NULL;
	}

	sb->s_root = d_alloc_root(root_inode);

	if (!sb->s_root) {
		printk(KERN_WARNING "inputfs: Unable to get root dentry\n");
		iput(root_inode);
		return NULL;
	}

	return sb;
}

static DECLARE_FSTYPE(inputfs_fs_type, "inputfs", inputfs_read_super, FS_SINGLE);

static int __init init_inputfs_fs(void)
{
	int ret = register_filesystem(&inputfs_fs_type);

	if (!ret) {
		inputfs_mnt = kern_mount(&inputfs_fs_type);
		ret = PTR_ERR(inputfs_mnt);

		if (!IS_ERR(inputfs_mnt)) {
			ret = 0;
		}
	}

	return ret;
}

static void __exit exit_inputfs_fs(void)
{
	kern_umount(inputfs_mnt);
	unregister_filesystem(&inputfs_fs_type);
}

EXPORT_NO_SYMBOLS;

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION("input file system");
MODULE_LICENSE("GPL");

module_init(init_inputfs_fs);
module_exit(exit_inputfs_fs);

