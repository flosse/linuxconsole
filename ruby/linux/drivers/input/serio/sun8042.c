/* $Id$
 * pcikbd.c: Ultra/AX PC keyboard support.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 * JavaStation support by Pete A. Zaitcev.
 *
 * This code is mainly put together from various places in
 * drivers/char, please refer to these sources for credits
 * to the original authors.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/random.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/uaccess.h>

/*
 * Different platforms provide different permutations of names.
 * AXi - kb_ps2, kdmouse.
 * MrCoffee - keyboard, mouse.
 * Espresso - keyboard, kdmouse.
 */
#define	PCI_KB_NAME1	"kb_ps2"
#define PCI_KB_NAME2	"keyboard"
#define PCI_MS_NAME1	"kdmouse"
#define PCI_MS_NAME2	"mouse"

#include "pcikbd.h"
#include "sunserial.h"

#ifndef __sparc_v9__
static int pcikbd_mrcoffee = 0;
#else
#define pcikbd_mrcoffee 0
#endif

static unsigned long pcikbd_iobase = 0;
static unsigned int pcikbd_irq = 0;

unsigned char pckbd_read_mask = KBD_STAT_OBF;

#ifdef __sparc_v9__

static unsigned long pcibeep_iobase = 0;

/* Timer routine to turn off the beep after the interval expires. */
static void pcikbd_kd_nosound(unsigned long __unused)
{
	outl(0, pcibeep_iobase);
}

/*
 * Initiate a keyboard beep. If the frequency is zero, then we stop
 * the beep. Any other frequency will start a monotone beep. The beep
 * will be stopped by a timer after "ticks" jiffies. If ticks is 0,
 * then we do not start a timer.
 */
static void pcikbd_kd_mksound(unsigned int hz, unsigned int ticks)
{
	unsigned long flags;
	static struct timer_list sound_timer = { NULL, NULL, 0, 0,
						 pcikbd_kd_nosound };

	save_flags(flags); cli();
	del_timer(&sound_timer);
	if (hz) {
		outl(1, pcibeep_iobase);
		if (ticks) {
			sound_timer.expires = jiffies + ticks;
			add_timer(&sound_timer);
		}
	} else
		outl(0, pcibeep_iobase);
	restore_flags(flags);
}
#endif

extern void (*kd_mksound)(unsigned int hz, unsigned int ticks);

static char * __init do_pcikbd_init_hw(void)
{

	while(pcikbd_wait_for_input() != -1);

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_SELF_TEST);
	if(pcikbd_wait_for_input() != 0x55)
		return "Keyboard failed self test";

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_KBD_TEST);
	if(pcikbd_wait_for_input() != 0x00)
		return "Keyboard interface failed self test";

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_KBD_ENABLE);
	pcikbd_write(KBD_DATA_REG, KBD_CMD_RESET);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Keyboard reset failed, no ACK";
	if(pcikbd_wait_for_input() != KBD_REPLY_POR)
		return "Keyboard reset failed, no ACK";

	pcikbd_write(KBD_DATA_REG, KBD_CMD_DISABLE);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Disable keyboard: no ACK";

	pcikbd_write(KBD_CNTL_REG, KBD_CCMD_WRITE_MODE);
	pcikbd_write(KBD_DATA_REG,
		     (KBD_MODE_KBD_INT | KBD_MODE_SYS |
		      KBD_MODE_DISABLE_MOUSE | KBD_MODE_KCC));
	pcikbd_write(KBD_DATA_REG, KBD_CMD_ENABLE);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Enable keyboard: no ACK";

	pcikbd_write(KBD_DATA_REG, KBD_CMD_SET_RATE);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Set rate: no ACK";
	pcikbd_write(KBD_DATA_REG, 0x00);
	if(pcikbd_wait_for_input() != KBD_REPLY_ACK)
		return "Set rate: no ACK";

	return NULL; /* success */
}

void __init pcikbd_init_hw(void)
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	struct linux_ebus_child *child;
	char *msg;

	if (pcikbd_mrcoffee) {
		if ((pcikbd_iobase = (unsigned long) ioremap(0x71300060, 8)) == 0) {
			prom_printf("pcikbd_init_hw: cannot map\n");
			return;
		}
		pcikbd_irq = 13 | 0x20;
		if (request_irq(pcikbd_irq, &pcikbd_interrupt,
				SA_SHIRQ, "keyboard", (void *)pcikbd_iobase)) {
			printk("8042: cannot register IRQ %x\n", pcikbd_irq);
			return;
		}
		printk("8042(kbd): iobase[%x] irq[%x]\n",
		    (unsigned)pcikbd_iobase, pcikbd_irq);
	} else {
		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if(!strcmp(edev->prom_name, "8042")) {
					for_each_edevchild(edev, child) {
                                                if (strcmp(child->prom_name, PCI_KB_NAME1) == 0 ||
						    strcmp(child->prom_name, PCI_KB_NAME2) == 0)
							goto found;
					}
				}
			}
		}
		printk("pcikbd_init_hw: no 8042 found\n");
		return;

found:
		pcikbd_iobase = child->resource[0].start;
		pcikbd_irq = child->irqs[0];
		if (request_irq(pcikbd_irq, &pcikbd_interrupt,
				SA_SHIRQ, "keyboard", (void *)pcikbd_iobase)) {
			printk("8042: cannot register IRQ %s\n",
			       __irq_itoa(pcikbd_irq));
			return;
		}

		printk("8042(kbd) at 0x%lx (irq %s)\n", pcikbd_iobase,
		       __irq_itoa(pcikbd_irq));
	}

	kd_mksound = nop_kd_mksound;

#ifdef __sparc_v9__
	edev = 0;
	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if(!strcmp(edev->prom_name, "beeper"))
				goto ebus_done;
		}
	}
ebus_done:

	/*
	 * XXX: my 3.1.3 PROM does not give me the beeper node for the audio
	 *      auxio register, though I know it is there... (ecd)
	 *
	 * JavaStations appear not to have beeper. --zaitcev
	 */
	if (!edev)
		pcibeep_iobase = (pcikbd_iobase & ~(0xffffff)) | 0x722000;
	else
		pcibeep_iobase = edev->resource[0].start;

	kd_mksound = pcikbd_kd_mksound;
	printk("8042(speaker): iobase[%016lx]%s\n", pcibeep_iobase,
	       edev ? "" : " (forced)");
#endif

	disable_irq(pcikbd_irq);
	msg = do_pcikbd_init_hw();
	enable_irq(pcikbd_irq);

	if(msg)
		printk("8042: keyboard init failure [%s]\n", msg);
}

int __init pcimouse_init(void)
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	struct linux_ebus_child *child;

	if (pcikbd_mrcoffee) {
		if ((pcimouse_iobase = pcikbd_iobase) == 0) {
			printk("pcimouse_init: no 8042 given\n");
			goto do_enodev;
		}
		pcimouse_irq = pcikbd_irq;
	} else {
		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if(!strcmp(edev->prom_name, "8042")) {
					for_each_edevchild(edev, child) {
							if (strcmp(child->prom_name, PCI_MS_NAME1) == 0 ||
							    strcmp(child->prom_name, PCI_MS_NAME2) == 0)
							goto found;
					}
				}
			}
		}
		printk("pcimouse_init: no 8042 found\n");
		goto do_enodev;

found:
		pcimouse_iobase = child->resource[0].start;
		pcimouse_irq = child->irqs[0];
	}

	queue = (struct aux_queue *) kmalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue) {
		printk("pcimouse_init: kmalloc(aux_queue) failed.\n");
		return -ENOMEM;
	}
	memset(queue, 0, sizeof(*queue));

	init_waitqueue_head(&queue->proc_list);

	if (request_irq(pcimouse_irq, &pcimouse_interrupt,
		        SA_SHIRQ, "mouse", (void *)pcimouse_iobase)) {
		printk("8042: Cannot register IRQ %s\n",
		       __irq_itoa(pcimouse_irq));
		goto do_enodev;
	}

	printk("8042(mouse) at %lx (irq %s)\n", pcimouse_iobase,
	       __irq_itoa(pcimouse_irq));

	printk("8042: PS/2 auxiliary pointing device detected.\n");
	aux_present = 1;
	pckbd_read_mask = AUX_STAT_OBF;

	misc_register(&psaux_mouse);
	aux_start_atomic();
	pcimouse_outb(KBD_CCMD_MOUSE_ENABLE, pcimouse_iobase + KBD_CNTL_REG);
	aux_write_ack(AUX_RESET);
	aux_write_ack(AUX_SET_SAMPLE);
	aux_write_ack(100);
	aux_write_ack(AUX_SET_RES);
	aux_write_ack(3);
	aux_write_ack(AUX_SET_SCALE21);
	poll_aux_status();
	pcimouse_outb(KBD_CCMD_MOUSE_DISABLE, pcimouse_iobase + KBD_CNTL_REG);
	poll_aux_status();
	pcimouse_outb(KBD_CCMD_WRITE_MODE, pcimouse_iobase + KBD_CNTL_REG);
	poll_aux_status();
	pcimouse_outb(AUX_INTS_OFF, pcimouse_iobase + KBD_DATA_REG);
	poll_aux_status();
	aux_end_atomic();

	return 0;

do_enodev:
	misc_register(&psaux_no_mouse);
	return -ENODEV;
}

int __init ps2kbd_probe(void)
{
	int pnode, enode, node, dnode, xnode;
	int kbnode = 0, msnode = 0, bnode = 0;
	int devices = 0;
	char prop[128];
	int len;

#ifndef __sparc_v9__
	/*
	 * MrCoffee has hardware but has no PROM nodes whatsoever.
	 */
	len = prom_getproperty(prom_root_node, "name", prop, sizeof(prop));
	if (len < 0) {
		printk("ps2kbd_probe: no name of root node\n");
		goto do_enodev;
	}
	if (strncmp(prop, "SUNW,JavaStation-1", len) == 0) {
		pcikbd_mrcoffee = 1;	/* Brain damage detected */
		goto found;
	}
#endif
	/*
	 * Get the nodes for keyboard and mouse from aliases on normal systems.
	 */
        node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "aliases");
	if (!node)
		goto do_enodev;

	len = prom_getproperty(node, "keyboard", prop, sizeof(prop));
	if (len > 0) {
		prop[len] = 0;
		kbnode = prom_finddevice(prop);
	}
	if (!kbnode)
		goto do_enodev;

	len = prom_getproperty(node, "mouse", prop, sizeof(prop));
	if (len > 0) {
		prop[len] = 0;
		msnode = prom_finddevice(prop);
	}
	if (!msnode)
		goto do_enodev;

	/*
	 * Find matching EBus nodes...
	 */
        node = prom_getchild(prom_root_node);
	pnode = prom_searchsiblings(node, "pci");

	/*
	 * Check for SUNW,sabre on Ultra5/10/AXi.
	 */
	len = prom_getproperty(pnode, "model", prop, sizeof(prop));
	if ((len > 0) && !strncmp(prop, "SUNW,sabre", len)) {
		pnode = prom_getchild(pnode);
		pnode = prom_searchsiblings(pnode, "pci");
	}

	/*
	 * For each PCI bus...
	 */
	while (pnode) {
		enode = prom_getchild(pnode);
		enode = prom_searchsiblings(enode, "ebus");

		/*
		 * For each EBus on this PCI...
		 */
		while (enode) {
			node = prom_getchild(enode);
			bnode = prom_searchsiblings(node, "beeper");

			node = prom_getchild(enode);
			node = prom_searchsiblings(node, "8042");

			/*
			 * For each '8042' on this EBus...
			 */
			while (node) {
				dnode = prom_getchild(node);

				/*
				 * Does it match?
				 */
				if ((xnode = prom_searchsiblings(dnode, PCI_KB_NAME1)) == kbnode) {
					++devices;
				} else if ((xnode = prom_searchsiblings(dnode, PCI_KB_NAME2)) == kbnode) {
					++devices;
				}

				if ((xnode = prom_searchsiblings(dnode, PCI_MS_NAME1)) == msnode) {
					++devices;
				} else if ((xnode = prom_searchsiblings(dnode, PCI_MS_NAME2)) == msnode) {
					++devices;
				}

				/*
				 * Found everything we need?
				 */
				if (devices == 2)
					goto found;

				node = prom_getsibling(node);
				node = prom_searchsiblings(node, "8042");
			}
			enode = prom_getsibling(enode);
			enode = prom_searchsiblings(enode, "ebus");
		}
		pnode = prom_getsibling(pnode);
		pnode = prom_searchsiblings(pnode, "pci");
	}
do_enodev:
	sunkbd_setinitfunc(pcimouse_no_init);
	return -ENODEV;

found:
        sunkbd_setinitfunc(pcimouse_init);
        sunkbd_setinitfunc(pcikbd_init);
	kbd_ops.compute_shiftstate = pci_compute_shiftstate;
	kbd_ops.setledstate = pci_setledstate;
	kbd_ops.getledstate = pci_getledstate;
	kbd_ops.setkeycode = pci_setkeycode;
	kbd_ops.getkeycode = pci_getkeycode;
	return 0;
}
