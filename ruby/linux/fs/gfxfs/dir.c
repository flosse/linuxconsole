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

static int gfxfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *new_dentry)
{
	if (S_ISDIR(old_dentry->d_inode->i_mode))
		return -EPERM;
	
	old_dentry->d_inode->i_nlink++;
	atomic_inc(&old_dentry->d_inode->i_count);
	dget(new_dentry);
	d_instantiate(new_dentry, old_dentry->d_inode);

	return 0;
}

static int gfxfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct list_head *list;

	spin_lock(&dcache_lock);

	list_for_each(list, &dentry->d_subdirs) {
		struct dentry *de = list_entry(list, struct dentry, d_child);

		if (de->d_inode && !d_unhashed(de)) {
			spin_unlock(&dcache_lock);
			return -ENOTEMPTY;
		}
	}

	spin_unlock(&dcache_lock);
	dentry->d_inode->i_nlink--;
	dput(dentry);

	return 0;
}

static struct dentry *gfxfs_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

int gfxfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int dev)
{
	struct inode *inode = gfxfs_get_inode(dir->i_sb, mode, dev);

	if (!inode)
		return -ENOSPC;
	
	d_instantiate(dentry, inode);
	dget(dentry);

	return 0;
}

static int gfxfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	return gfxfs_mknod(dir, dentry, mode | S_IFDIR, 0);
}

static int gfxfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	return gfxfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

/* FIXME: Keep things happy for now, add our own later */
struct file_operations gfxfs_dir_ops = {
	read:		generic_read_dir,
	readdir:	dcache_readdir,
};

struct inode_operations gfxfs_dir_inode_ops = {
	create:		gfxfs_create,
	link:		gfxfs_link,
	unlink:		gfxfs_unlink,
	mkdir:		gfxfs_mkdir,
	rmdir:		gfxfs_unlink,
	mknod:		gfxfs_mknod,
	lookup:		gfxfs_lookup,
};

