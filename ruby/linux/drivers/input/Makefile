#
# Makefile for the input core drivers.
#

# The target object and module list name.

O_TARGET	:= inputdrv.o

# Objects that export symbols.

mod-subdirs	:= joystick keyboard mouse touchscreen 
export-objs	:= input.o

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT)		+= input.o
obj-$(CONFIG_INPUT_KEYBDEV)	+= keybdev.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o
obj-$(CONFIG_INPUT_TSDEV)	+= tsdev.o
obj-$(CONFIG_INPUT_POWER)	+= power.o
obj-$(CONFIG_INPUT_EVBUG)	+= evbug.o

subdir-$(CONFIG_INPUT_JOYSTICK)	+= joystick
subdir-$(CONFIG_INPUT_KEYBOARD)	+= keyboard
subdir-$(CONFIG_INPUT_MOUSE)	+= mouse
subdir-$(CONFIG_INPUT_TOUCHSCREEN) += touchscreen

ifeq ($(CONFIG_INPUT_KEYBOARD),y)
	obj-y += keyboard/keybdrv.o
endif

ifeq ($(CONFIG_INPUT_MOUSE),y)
	obj-y += mouse/mousedrv.o
endif

ifeq ($(CONFIG_INPUT_JOYSTICK),y)
	obj-y += joystick/joydrv.o
endif

ifeq ($(CONFIG_INPUT_TOUCHSCREEN),y)
	obj-y += touchscreen/tsdrv.o
endif

# The global Rules.make.

include $(TOPDIR)/Rules.make
