/*
 * drivers/char/vt_userspace.c - basic VirtualTerminal class support
 * A very simple (and naive) implementation by Aivils Stoss
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/vt_kern.h>
#include <linux/input.h>

/* show configuration fields */
#define vt_config_attr(field, format_string)				\
static ssize_t								\
vt_show_##field (struct class_device *dev, char *buf)				\
{									\
	struct vt_struct *vt;						\
									\
	vt = to_vt_struct (dev);					\
	return sprintf (buf, format_string, vt->field);		\
}									\
static CLASS_DEVICE_ATTR(field, S_IRUGO, vt_show_##field, NULL);

struct class vt_class = {
	.name = "vt",
};

vt_config_attr(display_desc, "%s\n");
vt_config_attr(first_vc, "%d\n");
vt_config_attr(vc_count, "%d\n");


static ssize_t
vt_show_keyboard (struct class_device *dev, char *buf)
{
	struct vt_struct *vt;
	struct input_handle *handle;

	vt = to_vt_struct (dev);
	handle = vt->keyboard;
	if (handle && handle->dev->phys)
		return sprintf (buf, "%s\n", handle->dev->phys);
	return sprintf (buf, "%s\n", "");
}
static CLASS_DEVICE_ATTR(keyboard, S_IRUGO, vt_show_keyboard, NULL);

int __init vt_create_sysfs_dev_files (struct vt_struct *vt)
{
	struct class_device *dev = &vt->dev;

	dev->class = &vt_class;
	sprintf (dev->class_id, "%02x", vt->vt_num);
	class_device_register(dev);

	/* current configuration's attributes */
	class_device_create_file (dev, &class_device_attr_display_desc);
	class_device_create_file (dev, &class_device_attr_first_vc);
	class_device_create_file (dev, &class_device_attr_vc_count);
	class_device_create_file (dev, &class_device_attr_keyboard);

	return 0;
}

void __init vt_sysfs_init(void)
{
	/* we have only one boot time console - admin_vt*/
	class_register(&vt_class);
	vt_create_sysfs_dev_files(admin_vt);
}
