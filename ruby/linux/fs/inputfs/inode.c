/*
 * fs/inputfs/inode.c
 *
 * inputfs inode operations
 *
 * Copyright (C) 2001 James Simmons <jsimmons@transvirtual.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 */
#include <linux/locks.h>
#include <linux/inputfs_fs.h>
#include <asm/uaccess.h>

void inputfs_read_inode(struct inode *inode)
{
	inode->i_atime = inode->i_ctime = inode->i_mtime = CURRENT_TIME;
}

struct inode *inputfs_get_inode(struct super_block *sb, int mode, int dev)
{
	struct inode *inode = new_inode(sb);

	if (!inode) {
		printk(KERN_WARNING "inputfs: Unable to get new inode\n");
		return NULL;
	}

	inode->i_mode    = mode;
	inode->i_uid     = current->fsuid;
	inode->i_gid     = current->fsgid;
	inode->i_blksize = PAGE_CACHE_SIZE;
	inode->i_blocks  = 0;
	inode->i_rdev    = NODEV;
	
	inputfs_read_inode(inode);

	switch (mode & S_IFMT) {
		case S_IFREG:
			inode->i_fop = &inputfs_file_ops;
			break;
		case S_IFDIR:
			inode->i_op = &inputfs_dir_inode_ops;
			inode->i_fop = &inputfs_dir_ops;
			break;
		default:
			init_special_inode(inode, mode, dev);
			break;
	}

	return inode;
}

