/*
 * linux/drivers/char/vc_screen.c
 *
 * Provide access to virtual console memory.
 * /dev/vcs0: the screen as it is being viewed right now (possibly scrolled)
 * /dev/vcsN: the screen of /dev/ttyN (1 <= N <= 63)
 *            [minor: N]
 *
 * /dev/vcsaN: idem, but including attributes, and prefixed with
 *	the 4 bytes lines,columns,x,y (as screendump used to give).
 *	Attribute/character pair is in native endianity.
 *            [minor: N+128]
 *
 * This replaces screendump and part of selection, so that the system
 * administrator can control access using file system permissions.
 *
 * aeb@cwi.nl - efter Friedas begravelse - 950211
 *
 * machek@k332.feld.cvut.cz - modified not to send characters to wrong console
 *	 - fixed some fatal off-by-one bugs (0-- no longer == -1 -> looping and looping and looping...)
 *	 - making it shorter - scr_readw are macros which expand in PRETTY long code
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/console.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/unaligned.h>

#undef attr
#undef org
#undef addr
#define HEADER_SIZE	4

/* note the word offset */
unsigned short *screen_pos(struct vc_data *vc, int w_offset, int viewed)
{
        return screenpos(vc, 2 * w_offset, viewed);
}

void getconsxy(struct vc_data *vc, char *p)
{
        p[0] = vc->vc_x;
        p[1] = vc->vc_y;
}

void putconsxy(struct vc_data *vc, char *p)
{
        gotoxy(vc, p[0], p[1]);
        set_cursor(vc);
}

u16 vcs_scr_readw(struct vc_data *vc, const u16 *org)
{
        if ((unsigned long)org == vc->vc_pos && vc->display_fg->cursor_original != -1)
                return vc->display_fg->cursor_original;
        return scr_readw(org);
}

void vcs_scr_writew(struct vc_data *vc, u16 val, u16 *org)
{
        scr_writew(val, org);
        if ((unsigned long)org == vc->vc_pos) {
                vc->display_fg->cursor_original = -1;
                add_softcursor(vc);
        }
}

static loff_t vcs_lseek(struct file *file, loff_t offset, int orig)
{
	struct vc_data *vc = (struct vc_data *) file->private_data;
	struct inode *inode = file->f_dentry->d_inode;
	int size;

	if (!vc) {
		/* Impossible ? */
		if (!vt_cons->fg_console)
			return -ENXIO;
		vc = vt_cons->fg_console;
	}
	
	size = vc->vc_rows * vc->vc_cols;

	if (MINOR(inode->i_rdev) & 128)
                size = 2*size + HEADER_SIZE;

	switch (orig) {
		default:
			return -EINVAL;
		case 2:
			offset += size;
			break;
		case 1:
			offset += file->f_pos;
		case 0:
			break;
	}
	if (offset < 0 || offset > size)
		return -EINVAL;
	file->f_pos = offset;
	return file->f_pos;
}

#define CON_BUF_SIZE	PAGE_SIZE

static ssize_t
vcs_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	struct vc_data *vc = (struct vc_data *) file->private_data;
	struct inode *inode = file->f_dentry->d_inode;
	unsigned int currcons = MINOR(inode->i_rdev);
	long pos = *ppos;
	long viewed, attr, read;
	int col, maxcol;
	unsigned short *org = NULL;
	ssize_t ret;

	down(&vc->display_fg->lock);

	/* Select the proper current console and verify
	 * sanity of the situation under the console lock.
	 */
	down(&vc->vc_tty->driver.tty_lock);

	attr = (currcons & 128);
	currcons = (currcons & 127);
	if (currcons == 0) {
                vc = vt_cons->fg_console;
		viewed = 1;
        } else {
                vc = (struct vc_data *) file->private_data;
		viewed = 0;
        }

	ret = -ENXIO;
	if (!vc)
		goto unlock_out;

	ret = -EINVAL;
	if (pos < 0)
		goto unlock_out;
	read = 0;
	ret = 0;
	while (count) {
		char *con_buf0, *con_buf_start;
		long this_round, size;
		ssize_t orig_count;
		long p = pos;

		/* Check whether we are above size each round,
		 * as copy_to_user at the end of this loop
		 * could sleep.
		 */
		size = vc->vc_rows * vc->vc_cols;
		if (MINOR(inode->i_rdev) & 128)
	                size = 2*size + HEADER_SIZE;

		if (pos >= size)
			break;
		if (count > size - pos)
			count = size - pos;

		this_round = count;
		if (this_round > CON_BUF_SIZE)
			this_round = CON_BUF_SIZE;

		/* Perform the whole read into the current VT's con_buf.
		 * Then we can drop the console spinlock and safely
		 * attempt to move it to userspace.
		 */

		con_buf_start = con_buf0 = vc->display_fg->con_buf;
		orig_count = this_round;
		maxcol = vc->vc_cols;
		if (!attr) {
			org = screen_pos(vc, p, viewed);
			col = p % maxcol;
			p += maxcol - col;
			while (this_round-- > 0) {
				*con_buf0++ = (vcs_scr_readw(vc, org++) & 0xff);
				if (++col == maxcol) {
					org = screen_pos(vc, p, viewed);
					col = 0;
					p += maxcol;
				}
			}
		} else {
			if (p < HEADER_SIZE) {
				size_t tmp_count;

				con_buf0[0] = (char) vc->vc_rows;
				con_buf0[1] = (char) vc->vc_cols;
				getconsxy(vc, con_buf0 + 2);

				con_buf_start += p;
				this_round += p;
				if (this_round > CON_BUF_SIZE) {
					this_round = CON_BUF_SIZE;
					orig_count = this_round - p;
				}

				tmp_count = HEADER_SIZE;
				if (tmp_count > this_round)
					tmp_count = this_round;

				/* Advance state pointers and move on. */
				this_round -= tmp_count;
				p = HEADER_SIZE;
				con_buf0 = vc->display_fg->con_buf+HEADER_SIZE;
				/* If this_round >= 0, then p is even... */
			} else if (p & 1) {
				/* Skip first byte for output if start address is odd
				 * Update region sizes up/down depending on free
				 * space in buffer.
				 */
				con_buf_start++;
				if (this_round < CON_BUF_SIZE)
					this_round++;
				else
					orig_count--;
			}
			if (this_round > 0) {
				unsigned short *tmp_buf = (unsigned short *)con_buf0;

				p -= HEADER_SIZE;
				p /= 2;
				col = p % maxcol;

				org = screen_pos(vc, p, viewed);
				p += maxcol - col;

				/* Buffer has even length, so we can always copy
				 * character + attribute. We do not copy last byte
				 * to userspace if this_round is odd.
				 */
				this_round = (this_round + 1) >> 1;

				while (this_round) {
					*tmp_buf++ = vcs_scr_readw(vc, org++);
					this_round --;
					if (++col == maxcol) {
						org = screen_pos(vc, p, viewed);
						col = 0;
						p += maxcol;
					}
				}
			}
		}

		/* Finally, temporarily drop the console lock and push
		 * all the data to userspace from our temporary buffer.
		 */

		up(&vc->vc_tty->driver.tty_lock);
		ret = copy_to_user(buf, con_buf_start, orig_count);
		down(&vc->vc_tty->driver.tty_lock);

		if (ret) {
			read += (orig_count - ret);
			ret = -EFAULT;
			break;
		}
		buf += orig_count;
		pos += orig_count;
		read += orig_count;
		count -= orig_count;
	}
	*ppos += read;
	if (read)
		ret = read;
unlock_out:
	up(&vc->vc_tty->driver.tty_lock);
	up(&vc->display_fg->lock);
	return ret;
}

static ssize_t
vcs_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct vc_data *vc = (struct vc_data *) file->private_data;
	struct inode *inode = file->f_dentry->d_inode;
	unsigned int currcons = MINOR(inode->i_rdev);
	long pos = *ppos;
	long viewed, attr, size, written;
	char *con_buf0;
	int col, maxcol;
	u16 *org0 = NULL, *org = NULL;
	size_t ret;

	down(&vc->display_fg->lock);

	/* Select the proper current console and verify
	 * sanity of the situation under the console lock.
	 */
	down(&vc->vc_tty->driver.tty_lock);

	attr = (currcons & 128);
	currcons = (currcons & 127);

	if (currcons == 0) {
                vc = vt_cons->fg_console;
		viewed = 1;
        } else {
                vc = (struct vc_data *) file->private_data;
		viewed = 0;
	} 
	
	ret = -ENXIO;
	if (!vc)
		goto unlock_out;

	size = vc->vc_rows * vc->vc_cols;
	if (MINOR(inode->i_rdev) & 128)
                size = 2*size + HEADER_SIZE;

	ret = -EINVAL;
	if (pos < 0 || pos > size)
		goto unlock_out;
	if (count > size - pos)
		count = size - pos;
	written = 0;
	while (count) {
		long this_round = count;
		size_t orig_count;
		long p;

		if (this_round > CON_BUF_SIZE)
			this_round = CON_BUF_SIZE;

		/* Temporarily drop the console lock so that we can read
		 * in the write data from userspace safely.
		 */
		up(&vc->vc_tty->driver.tty_lock);
		ret = copy_from_user(&vc->display_fg->con_buf, buf, this_round);
		down(&vc->vc_tty->driver.tty_lock);

		if (ret) {
			this_round -= ret;
			if (!this_round) {
				/* Abort loop if no data were copied. Otherwise
				 * fail with -EFAULT.
				 */
				if (written)
					break;
				ret = -EFAULT;
				goto unlock_out;
			}
		}

		/* The vcs size might have changed while we slept to grab
		 * the user buffer, so recheck.
		 * Return data written up to now on failure.
		 */
		size = vc->vc_rows * vc->vc_cols;
		if (MINOR(inode->i_rdev) & 128)
	                size = 2*size + HEADER_SIZE;

		if (pos >= size)
			break;
		if (this_round > size - pos)
			this_round = size - pos;

		/* OK, now actually push the write to the console
		 * under the lock using the local kernel buffer.
		 */

		con_buf0 = vc->display_fg->con_buf;
		orig_count = this_round;
		maxcol = vc->vc_cols;
		p = pos;
		if (!attr) {
			org0 = org = screen_pos(vc, p, viewed);
			col = p % maxcol;
			p += maxcol - col;

			while (this_round > 0) {
				unsigned char c = *con_buf0++;

				this_round--;
				vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff00) | c, org);
				org++;
				if (++col == maxcol) {
					org = screen_pos(vc, p, viewed);
					col = 0;
					p += maxcol;
				}
			}
		} else {
			if (p < HEADER_SIZE) {
				char header[HEADER_SIZE];

				getconsxy(vc, header + 2);
				while (p < HEADER_SIZE && this_round > 0) {
					this_round--;
					header[p++] = *con_buf0++;
				}
				if (!viewed)
					putconsxy(vc, header + 2);
			}
			p -= HEADER_SIZE;
			col = (p/2) % maxcol;
			if (this_round > 0) {
				org0 = org = screen_pos(vc, p/2, viewed);
				if ((p & 1) && this_round > 0) {
					char c;

					this_round--;
					c = *con_buf0++;
#ifdef __BIG_ENDIAN
					vcs_scr_writew(vc, c | (vcs_scr_readw(vc, org) & 0xff00), org);
#else
					vcs_scr_writew(vc, (c << 8) |
					     (vcs_scr_readw(vc, org) & 0xff), org);
#endif
					org++;
					p++;
					if (++col == maxcol) {
						org = screen_pos(vc, p/2, viewed);
						col = 0;
					}
				}
				p /= 2;
				p += maxcol - col;
			}
			while (this_round > 1) {
				unsigned short w;

				w = get_unaligned(((const unsigned short *)con_buf0));
				vcs_scr_writew(vc, w, org++);
				con_buf0 += 2;
				this_round -= 2;
				if (++col == maxcol) {
					org = screen_pos(vc, p, viewed);
					col = 0;
					p += maxcol;
				}
			}
			if (this_round > 0) {
				unsigned char c;

				c = *con_buf0++;
#ifdef __BIG_ENDIAN
				vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff) | (c << 8), org);
#else
				vcs_scr_writew(vc, (vcs_scr_readw(vc, org) & 0xff00) | c, org);
#endif
			}
		}
		count -= orig_count;
		written += orig_count;
		buf += orig_count;
		pos += orig_count;
		if (org0)
			update_region(vc, (unsigned long)(org0), org-org0);
	}
	*ppos += written;
	ret = written;

unlock_out:
	up(&vc->vc_tty->driver.tty_lock);
	up(&vc->display_fg->lock);
	return ret;
}

static int
vcs_open(struct inode *inode, struct file *filp)
{
	unsigned int currcons = (MINOR(inode->i_rdev) & 127);
	struct vc_data *vc;

	if (currcons) {
		vc = find_vc(currcons-1);
		if (vc)
			filp->private_data = vc;
		else
			return -ENXIO;
	}	
	return 0;
}

static struct file_operations vcs_fops = {
	llseek:		vcs_lseek,
	read:		vcs_read,
	write:		vcs_write,
	open:		vcs_open,
};

static devfs_handle_t devfs_handle;

void vcs_make_devfs (unsigned int index, int unregister)
{
#ifdef CONFIG_DEVFS_FS
    char name[8];

    sprintf (name, "a%u", index + 1);
    if (unregister) {
	devfs_unregister ( devfs_find_handle (devfs_handle, name + 1, 0, 0, 
					      DEVFS_SPECIAL_CHR, 0) );
	devfs_unregister ( devfs_find_handle (devfs_handle, name, 0, 0, 
					      DEVFS_SPECIAL_CHR, 0) );
    } else {
	devfs_register (devfs_handle, name + 1, DEVFS_FL_DEFAULT,
			VCS_MAJOR, index + 1,
			S_IFCHR | S_IRUSR | S_IWUSR, &vcs_fops, NULL);
	devfs_register (devfs_handle, name, DEVFS_FL_DEFAULT,
			VCS_MAJOR, index + 129,
			S_IFCHR | S_IRUSR | S_IWUSR, &vcs_fops, NULL);
    }
#endif /* CONFIG_DEVFS_FS */
}

int __init vcs_init(void)
{
	int error;

	error = devfs_register_chrdev(VCS_MAJOR, "vcs", &vcs_fops);

	if (error)
		printk("unable to get major %d for vcs device", VCS_MAJOR);

	devfs_handle = devfs_mk_dir (NULL, "vcc", NULL);
	devfs_register (devfs_handle, "0", DEVFS_FL_DEFAULT,
			VCS_MAJOR, 0,
			S_IFCHR | S_IRUSR | S_IWUSR, &vcs_fops, NULL);
	devfs_register (devfs_handle, "a", DEVFS_FL_DEFAULT,
			VCS_MAJOR, 128,
			S_IFCHR | S_IRUSR | S_IWUSR, &vcs_fops, NULL);

	return error;
}
