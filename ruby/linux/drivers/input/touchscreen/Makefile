#
# Makefile for the mouse drivers.
#

# The target object and module list name.

O_TARGET	:= tsdrv.o

# Each configuration option enables a list of files.

obj-$(CONFIG_TS_BITSY)	+= h3600_ts_input.o
obj-$(CONFIG_TS_GUNZE)	+= gunze.o

# The global Rules.make.

include $(TOPDIR)/Rules.make
