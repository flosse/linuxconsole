
static int usb_make_path(struct usb_device *dev, char *buf, int maxlen)
{
	struct usb_device *pdev = dev->parent;
	char *tmp, *port;
	int i;

	if (!(port = kmalloc(maxlen, GFP_KERNEL)))
                return -1;
	if (!(tmp = kmalloc(maxlen, GFP_KERNEL)))
                return -1;

	*port = 0;

	while (pdev) {

		for (i = 0; i < pdev->maxchild; i++)
			if (pdev->children[i] == dev)
				break;

		if (pdev->children[i] != dev)
				return -1;

		strcpy(tmp, port);
		
		sprintf(port, strlen(port) ? "%d.%s" : "%d", i + 1, tmp);

		dev = pdev;
		pdev = dev->parent;
	}

	sprintf(buf, "usb%d:%s", dev->bus->busnum, port);
	return 0;
}

