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
#include <linux/slab.h>
#include <linux/string.h>

static struct vfsmount *gfxfs_mnt;

struct gfxfs_dentry *gfxfs_register_entry(const char *name, int mode,
					  struct gfxfs_dentry *oparent)
{
	struct gfxfs_dentry *g_dentry;
	struct gfxfs_dentry *parent = oparent;

	if (!parent && gfxfs_mnt->mnt_sb) {
		parent = gfxfs_mnt->mnt_sb->s_root;

		/*
		 * Should hopefully never get here.
		 */
		if (!parent) {
			printk(KERN_WARNING "gfxfs: Valid super mount, "
			       "but no valid root inode??\n");
			return NULL;
		}
	}

	g_dentry = kmalloc(sizeof(struct gfxfs_dentry), GFP_KERNEL);

	if (!g_dentry) {
		printk(KERN_WARNING "gfxfs: Unable to allocate memory\n");
		return NULL;
	}

	/* FIXME: f_dentry needs fixing */
	g_dentry->f_dentry->d_name.name = name;
	g_dentry->f_dentry->d_name.len  = strlen(name);
	g_dentry->f_dentry->d_name.hash = full_name_hash(name, strlen(name));

	gfxfs_mknod(parent->f_dentry->d_inode, g_dentry->f_dentry, mode, 0);

	return g_dentry;
}

static int gfxfs_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type    = GFXFS_SUPER_MAGIC;
	buf->f_bsize   = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;

	return 0;
}

static struct super_operations gfxfs_super_ops = {
	statfs:		gfxfs_statfs,
	put_inode:	force_delete,
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

MODULE_AUTHOR("Paul Mundt <pmundt@mvista.com>");
MODULE_DESCRIPTION("graphics file system");
MODULE_LICENSE("GPL");

module_init(init_gfxfs_fs);
module_exit(exit_gfxfs_fs);

EXPORT_SYMBOL(gfxfs_register_entry);

