#
# Makefile for the acorn character device drivers.
#

# All the objects that export symbols.

obj-$(CONFIG_ARCH_ACORN)	+= i2c.o pcf8583.o
obj-y				+= $(obj-$(MACHINE))

include $(TOPDIR)/Rules.make

