/*
 * linux/drivers/input/q40kbd.c
 */

static void keyboard_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	unsigned char status;

	spin_lock_irqsave(&kbd_controller_lock, flags);
	kbd_pt_regs = regs;
	if (IRQ_KEYB_MASK & master_inb(INTERRUPT_REG))
		output_char(master_inb(KEYCODE_REG));
	}
	spin_unlock_irqrestore(&kbd_controller_lock, flags);

	master_outb(-1, KEYBOARD_UNLOCK_REG);
}

static void __init kbd_clear_input(void)
{
	int maxread = 100;	/* Random number */
	while (maxread && (IRQ_KEYB_MASK & master_inb(INTERRUPT_REG))) {
		maxread--;
		master_inb(KEYCODE_REG);
	}
	master_outb(-1,KEYBOARD_UNLOCK_REG);
}

void __init q40kbd_init(void)
{
	/* Get the keyboard controller registers (incomplete decode) */
	request_region(0x60, 16, "keyboard");

	/* Flush any pending input. */
	kbd_clear_input();

	/* Ok, finally allocate the IRQ, and off we go.. */
	request_irq(Q40_IRQ_KEYBOARD, keyboard_interrupt, 0, "keyboard", NULL);
	master_outb(-1,KEYBOARD_UNLOCK_REG);
	master_outb(1,KEY_IRQ_ENABLE_REG);
}

void __exit q40kbd_exit(void)
{
}

module_init(q40kbd_init);
module_exit(q40kbd_exit);
