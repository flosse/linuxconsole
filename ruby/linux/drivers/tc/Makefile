#
# Makefile for the linux kernel.
#

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs	:= tc.o

# Object file lists.

obj-$(CONFIG_TC) += tc.o
obj-$(CONFIG_ZS) += zs.o

include $(TOPDIR)/Rules.make
