/*
 *  pcigame.c  Version 0.4.0
 *
 *  Copyright (c) 1999 Raymond Ingles
 *  Copyright (c) 1999 Vojtech Pavlik
 *
 *  Sponsored by SuSE
 */

/*
 * This is a module for the Linux input driver, supporting the
 * gameports on Trident 4DWave and Aureal Vortex soundcards.
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
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
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
	int vendor;	/* PCI Vendor ID */
	int model;	/* PCI Model ID */
	int size;	/* Memory / IO region size */
	int lcr;	/* Aureal Legacy Control Register */
	int gcr;	/* Gameport control register */
	int buttons;	/* Buttons location */
	int axes;	/* Axes start */
	int axsize;	/* Axis field size */
	int axmax;	/* Axis field max value */
	void (*init)(struct pcigame *);	
	void (*cleanup)(struct pcigame *);	
	char *name;
};

struct pcigame {
	struct gameport gameport;
	struct pcigame_data *data;
	struct pci_dev *pci_p;
        unsigned char *base;
	__u32 lcr;
};

/*
 * pcigame_*_init() sets the pcigame->base field, disables legacy gameports,
 * and enables the enhanced ones.
 */

static void pcigame_4dwave_init(struct pcigame *pcigame)
{
	pcigame->base = ioremap(BASE_ADDRESS(pcigame->pci_p, 1), pcigame->data->size);
	pci_read_config_word(pcigame->pci_p, pcigame->data->lcr, (unsigned short *)&pcigame->lcr);
	pci_write_config_word(pcigame->pci_p, pcigame->data->lcr, pcigame->lcr & ~0x20);
	writeb(0x80, pcigame->base + pcigame->data->gcr);
}

static void pcigame_vortex_init(struct pcigame *pcigame)
{
	pcigame->base = ioremap(BASE_ADDRESS(pcigame->pci_p, 0), pcigame->data->size);
	pcigame->lcr = readl(pcigame->base + pcigame->data->lcr);
	writel(pcigame->lcr & ~0x8, pcigame->base + pcigame->data->lcr);
	writel(0x40, pcigame->base + pcigame->data->gcr);
}

/*
 * pcigame_*_cleanup does the opposite of the above functions.
 */

static void pcigame_4dwave_cleanup(struct pcigame *pcigame)
{
	pci_write_config_word(pcigame->pci_p, pcigame->data->lcr, pcigame->lcr);
	writeb(0x00, pcigame->base + pcigame->data->gcr);
	iounmap(pcigame->base);
}

static void pcigame_vortex_cleanup(struct pcigame *pcigame)
{
	writel(pcigame->lcr, pcigame->base + pcigame->data->lcr);
	writel(0x00, pcigame->base + pcigame->data->gcr);
	iounmap(pcigame->base);
}

static struct pcigame_data pcigame_data[] =
{{ PCI_VENDOR_ID_TRIDENT, 0x2000, 0x10000, 0x00044 ,0x00030, 0x00031, 0x00034, 2, 0xffff,
	pcigame_4dwave_init, pcigame_4dwave_cleanup, "Trident 4DWave DX" },
 { PCI_VENDOR_ID_TRIDENT, 0x2001, 0x10000, 0x00044, 0x00030, 0x00031, 0x00034, 2, 0xffff,
	pcigame_4dwave_init, pcigame_4dwave_cleanup, "Trident 4DWave NX" },
 { PCI_VENDOR_ID_AUREAL,  0x0001, 0x40000, 0x1280c, 0x1100c, 0x11008, 0x11010, 4, 0x1fff,
	pcigame_vortex_init, pcigame_vortex_cleanup, "Aureal Vortex1" },
 { PCI_VENDOR_ID_AUREAL,  0x0002, 0x40000, 0x2a00c, 0x2880c, 0x28808, 0x28810, 4, 0x1fff,
	pcigame_vortex_init, pcigame_vortex_cleanup, "Aureal Vortex2" },
 { 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, NULL }};

/*
 * pcigame_read() reads data from a PCI gameport.
 */

static int pcigame_read(void *xpcigame, int **axes, int **buttons)
{
        struct pcigame *pcigame = xpcigame;
	int i;

	*buttons = ~readb(pcigame->base + pcigame->data->buttons) >> 4;
	for (i = 0; i < 4; i++)
		axes[i] = readw(pcigame->base + pcigame->data->axes + i * pcigame->data->axsize);
        
        return 0;
}
	

static int pcigame_init(struct pci_dev *pci_p, struct pcigame *data)
{

	if (!(pcigame = kmalloc(sizeof(struct pcigame), GFP_KERNEL)))
		return -1;
        memset(pcigame, 0, sizeof(struct pcigame));

	pcigame->data = data;
	pcigame->pci_p = pci_p;
	data->init(pcigame);
	sleep_ms(10);
	register_gameport(&pcigame.gameport);
	
	printk(KERN_INFO "gameport%d: %s at pci%02x.%x\n",
		gameport.number, data->name, PCI_SLOT(pci_p->devfn), PCI_FUNC(pci_p->devfn));

}

int __init pcigame_init(void)
{
        struct pci_dev *pci_p = NULL;
	int ports = 0;
        int i;

	for (i = 0; pcigame_data[i].vendor; i++)
		for (; (pci_p = pci_find_device(pcigame_data[i].vendor, pcigame_data[i].model, pci_p));)
			ports += !pcigame_init(pci_p, pcigame_data + i);

        if (!ports) {
                printk(KERN_WARNING "pcigame: no gameports found\n");
                return -ENODEV;
        }

        return 0;
}

void __exit pcigame_exit(void)
{
        int i;
        struct pcigame *pcigame;

        while (pcigame_port) {
                for (i = 0; i < pcigame_port->ndevs; i++)
                        if (pcigame_port->devs[i])
                                js_unregister_device(pcigame_port->devs[i]);
                pcigame = pcigame_port->pcigame;
		pcigame->data->cleanup(pcigame);
                pcigame_port = js_unregister_port(pcigame_port);
        }
}

module_init(pcigame_init);
module_exit(pcigame_exit);
