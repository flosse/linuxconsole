#
# Makefile for the I-Force driver
#

O_TARGET := iforce-drv.o

obj-$(CONFIG_JOYSTICK_IFORCE)	+= iforce.o 

include $(TOPDIR)/Rules.make

IFORCEOBJS = iforce-ff.o iforce-main.o iforce-packets.o

ifeq ($(CONFIG_JOYSTICK_IFORCE_232),y)
	IFORCEOBJS += iforce-serio.o 
endif

ifeq ($(CONFIG_JOYSTICK_IFORCE_USB),y)
	IFORCEOBJS += iforce-usb.o 
endif

iforce.o: $(IFORCEOBJS) 
	$(LD) -i $(IFORCEOBJS) -o $@

# The global Rules.make.

include $(TOPDIR)/Rules.make

