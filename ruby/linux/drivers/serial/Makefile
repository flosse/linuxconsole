#
# Makefile for the kernel Parallel port device drivers.
#
# Note! Parport is the Borg.  We have assimilated some other
# drivers in the `char', `net' and `scsi' directories,
# but left them there to allay suspicion.
#
# 7 October 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#

O_TARGET := uart.o

export-objs	:= serial_core.o

obj-$(CONFIG_UART)		+= serial_core.o

include $(TOPDIR)/Rules.make
