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
vt_show_##field (struct device *dev, char *buf)				\
{									\
	struct vt_struct *vt;						\
									\
	vt = to_vt_struct (dev);					\
	return sprintf (buf, format_string, vt->field);		\
}									\
static DEVICE_ATTR(field, S_IRUGO, vt_show_##field, NULL);

struct device vt_parent_dev;

vt_config_attr(display_desc, "%s\n");
vt_config_attr(first_vc, "%d\n");
vt_config_attr(vc_count, "%d\n");


static ssize_t
vt_show_keyboard (struct device *dev, char *buf)
{
	struct vt_struct *vt;
	struct input_handle *handle;

	vt = to_vt_struct (dev);
	handle = vt->keyboard;
	if (handle && handle->dev->phys)
		return sprintf (buf, "%s\n", handle->dev->phys);
	return sprintf (buf, "%s\n", "");
}
static DEVICE_ATTR(keyboard, S_IRUGO, vt_show_keyboard, NULL);

/**
 * vt_dev_get - increments the reference count of the pci device structure
 * @vt: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for VT devices should normally record such references in
 * their probe() methods, when they bind to a device, and release
 * them by calling vt_dev_put(), in their disconnect() methods.
 *
 * A pointer to the device with the incremented reference counter is returned.
 */
struct vt_struct *vt_dev_get(struct vt_struct *vt)
{
	struct device *tmp;

	if (!vt)
		return NULL;

	tmp = get_device(&vt->dev);
	if (tmp)        
		return to_vt_struct(tmp);
	else
		return NULL;
}

/**
 * vt_dev_put - release a use of the pci device structure
 * @vt: device that's been disconnected
 *
 * Must be called when a user of a device is finished with it.  When the last
 * user of the device calls this function, the memory of the device is freed.
 */
void vt_dev_put(struct vt_struct *vt)
{
	if (vt)
		put_device(&vt->dev);
}

struct bus_type vt_bus_type = {
	.name		= "vt",
};


int __init vt_create_sysfs_dev_files (struct vt_struct *vt)
{
	struct device *dev = &vt->dev;

	bus_register(&vt_bus_type);
	dev->parent = &vt_parent_dev;
	dev->bus = &vt_bus_type;
	device_initialize(dev);
	dev->release = NULL; /* release_vt */
	vt_dev_get(vt);
	sprintf (dev->bus_id, "%02x", vt->vt_num);
	device_add(dev);

	/* current configuration's attributes */
	device_create_file (dev, &dev_attr_display_desc);
	device_create_file (dev, &dev_attr_first_vc);
	device_create_file (dev, &dev_attr_vc_count);
	device_create_file (dev, &dev_attr_keyboard);

	return 0;
}

void __init vt_sysfs_init(void)
{
	struct device *dev=&vt_parent_dev;
	struct vt_struct *vt;

	memset(dev, 0, sizeof(*dev));
	dev->parent = NULL;
	sprintf(dev->bus_id, "wagadubu");
	device_register(dev);
        list_for_each_entry (vt, &vt_list, node) {
                vt_create_sysfs_dev_files(vt);
        }
}
