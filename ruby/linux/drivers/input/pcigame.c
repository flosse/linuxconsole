/*
 *  pcigame.c  Version 0.5
 *
 *  Copyright (c) 2000 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Raymond Ingles
 *
 *  Sponsored by SuSE
 */

/*
 * Trident 4DWave and Aureal Vortex gameport driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/init.h>
#include <linux/gameport.h>

struct pcigame;

struct pcigame_data {
	int size;	/* Memory / IO region size */
	int lcr;	/* Aureal Legacy Control Register */
	int gcr;	/* Gameport control register */
	int buttons;	/* Buttons location */
	int axes;	/* Axes start */
	int axsize;	/* Axis field size */
	int axmax;	/* Axis field max value */
	void (*init)(struct pcigame *);	
	void (*cleanup)(struct pcigame *);	
};

struct pcigame {
	struct gameport gameport;
	struct pcigame_data *data;
	struct pci_dev *dev;
        unsigned char *base;
	__u32 lcr;
};

static void pcigame_4dwave_init(struct pcigame *pcigame)
{
	pcigame->base = ioremap(BASE_ADDRESS(pcigame->dev, 1), pcigame->data->size);
	pci_read_config_word(pcigame->dev, pcigame->data->lcr, (unsigned short *)&pcigame->lcr);
	pci_write_config_word(pcigame->dev, pcigame->data->lcr, pcigame->lcr & ~0x20);
	writeb(0x80, pcigame->base + pcigame->data->gcr);
}

static void pcigame_vortex_init(struct pcigame *pcigame)
{
	pcigame->base = ioremap(BASE_ADDRESS(pcigame->dev, 0), pcigame->data->size);
	pcigame->lcr = readl(pcigame->base + pcigame->data->lcr);
	writel(pcigame->lcr & ~0x8, pcigame->base + pcigame->data->lcr);
	writel(0x40, pcigame->base + pcigame->data->gcr);
}

static void pcigame_4dwave_cleanup(struct pcigame *pcigame)
{
	pci_write_config_word(pcigame->dev, pcigame->data->lcr, pcigame->lcr);
	writeb(0x00, pcigame->base + pcigame->data->gcr);
	iounmap(pcigame->base);
}

static void pcigame_vortex_cleanup(struct pcigame *pcigame)
{
	writel(pcigame->lcr, pcigame->base + pcigame->data->lcr);
	writel(0x00, pcigame->base + pcigame->data->gcr);
	iounmap(pcigame->base);
}

static int pcigame_open(struct gameport *gameport, int mode)
{
	struct pcigame *pcigame = gameport->driver;

	if (mode != GAMEPORT_MODE_COOKED)
		return -1;

	return 0;
}

static int pcigame_cooked_read(struct gameport *gameport, int *axes, int *buttons)
{
        struct pcigame *pcigame = gameport->driver;
	int i;

	*buttons = ~readb(pcigame->base + pcigame->data->buttons) >> 4;
	for (i = 0; i < 4; i++)
		axes[i] = readw(pcigame->base + pcigame->data->axes + i * pcigame->data->axsize);
        
        return 0;
}

static int __devinit pcigame_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	if (!(pcigame = kmalloc(sizeof(struct pcigame), GFP_KERNEL)))
		return -1;
        memset(pcigame, 0, sizeof(struct pcigame));

	pcigame->data = (void *) id->driver_data;

	pcigame->dev = dev;
	dev->driver_data = pcigame;

	pcigame->gameport.driver = pcigame;
	pcigame->gameport.type = GAMEPORT_EXT;
	pcigame->gameport.pci = dev;
	
	pcigame->gameport.cooked_read = pcigame_cooked_read;
	pcigame->gameport.open = pcigame_open;

	data->init(pcigame);

	sleep_ms(10);

	register_gameport(&pcigame.gameport);
	
	printk(KERN_INFO "gameport%d: %s at pci%02x.%x\n",
		gameport.number, data->name, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

}

static void __devexit pcigame_remove(struct pci_dev *dev)
{
	stryct pcigame *pcigame = dev->driver_data;
	pcigame->data->cleanup(pcigame);
}

static struct pcigame_data pcigame_data[] __devinitdata =
{{ 0x10000, 0x00044 ,0x00030, 0x00031, 0x00034, 2, 0xffff, pcigame_4dwave_init, pcigame_4dwave_exit },
 { 0x40000, 0x1280c, 0x1100c, 0x11008, 0x11010, 4, 0x1fff, pcigame_vortex_init, pcigame_vortex_exit },
 { 0x40000, 0x2a00c, 0x2880c, 0x28808, 0x28810, 4, 0x1fff, pcigame_vortex_init, pcigame_vortex_exit },
 { 0 }};

static struct pci_device_id pcigame_id_table[] __devinitdata =
{{ PCI_VENDOR_ID_TRIDENT, 0x2000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long)(pcigame_data + 0) },
 { PCI_VENDOR_ID_TRIDENT, 0x2001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long)(pcigame_data + 0) },
 { PCI_VENDOR_ID_AUREAL,  0x0001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long)(pcigame_data + 1) },
 { PCI_VENDOR_ID_AUREAL,  0x0002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (long)(pcigame_data + 2) },
 { 0 }};

static struct pci_driver tulip_driver = {
	name:		"pcigame",
	id_table:	pcigame_id_table,
	probe:		pcigame_probe,
	remove:		pcigame_remove,
	suspend:	pcigame_suspend,
	resume:		pcigame_resume,
};

int __init pcigame_init(void)
{
	return pci_module_init(&pcigame_driver);
}

void __exit pcigame_exit(void)
{
	pci_unregister_driver(&pcigame_driver);
}

module_init(pcigame_init);
module_exit(pcigame_exit);
