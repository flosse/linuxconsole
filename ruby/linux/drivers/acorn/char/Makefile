#
# Makefile for the acorn character device drivers.
#

obj-$(CONFIG_ATOMWIDE_SERIAL)	+= serial-atomwide.o
obj-$(CONFIG_DUALSP_SERIAL)	+= serial-dualsp.o
obj-$(CONFIG_ARCH_ACORN)	+= i2c.o pcf8583.o

# Do the i2c and rtc last
obj-y		+= $(obj-$(MACHINE))

include $(TOPDIR)/Rules.make
