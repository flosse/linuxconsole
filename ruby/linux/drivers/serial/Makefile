#
# Makefile for the kernel serial port device drivers.
#
# We place all the "new" serial drivers to the new serial core.
#
# 21 June 2001, James Simmons <jsimmons@transvirtual.com>
# Rewritten to use lists instead of if-statements.
#

O_TARGET := uart.o

export-objs	:= serial_core.o

obj-$(CONFIG_UART)		+= serial_core.o
serial-8250-y :=
serial-8250-$(CONFIG_PCI) 	+= serial_8250_pci.o
serial-8250-$(CONFIG_ISAPNP) 	+= serial_8250_pnp.o
obj-$(CONFIG_SERIAL_8250)	+= serial_8250.o $(serial-8250-y)
obj-$(CONFIG_SERIAL_AMBA) 	+= serial_amba.o
obj-$(CONFIG_SERIAL_CLPS711X) 	+= serial_clps711x.o
obj-$(CONFIG_SERIAL_ANAKIN) 	+= serial_anakin.o
obj-$(CONFIG_SERIAL_SA1100) 	+= serial_sa1100.o

include $(TOPDIR)/Rules.make
