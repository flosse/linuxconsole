#
# Makefile for the I-Force driver
#

O_TARGET := iforce-drv.o

# I-Force may need both USB and RS-232

CONFIG_JOYSTICK_IFORCE := n

# Enable iforce if at least one of iforce-rs232 or iforce-usb is enabled
ifeq ($(CONFIG_JOYSTICK_IFORCE_232),y)
	CONFIG_JOYSTICK_IFORCE := y
endif

ifeq ($(CONFIG_JOYSTICK_IFORCE_USB),y)
	CONFIG_JOYSTICK_IFORCE := y
endif

# Compile as module if at least one of the choices is m
ifeq ($(CONFIG_JOYSTICK_IFORCE_232),m)
	CONFIG_JOYSTICK_IFORCE := m
endif

ifeq ($(CONFIG_JOYSTICK_IFORCE_USB),m)
	CONFIG_JOYSTICK_IFORCE := m
endif

obj-$(CONFIG_JOYSTICK_IFORCE)	+= iforce.o 

include $(TOPDIR)/Rules.make

IFORCEOBJS = iforce-ff.o iforce-main.o iforce-packets.o

ifneq ($(CONFIG_JOYSTICK_IFORCE_232),n)
	IFORCEOBJS += iforce-serio.o 
endif

ifneq ($(CONFIG_JOYSTICK_IFORCE_USB),n)
	IFORCEOBJS += iforce-usb.o 
endif

iforce.o: $(IFORCEOBJS) 
	$(LD) -i $(IFORCEOBJS) -o $@

