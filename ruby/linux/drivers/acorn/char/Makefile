#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_TARGET 	:= acorn-char.o

O_OBJS		:=
OX_OBJS		:=
M_OBJS		:=
MX_OBJS		:=

# Object file lists.

obj-y   	:= i2c.o pcf8583.o
obj-m		:=
obj-n		:=
obj-		:=

obj-$(CONFIG_ATOMWIDE_SERIAL)	+= serial-atomwide.o
obj-$(CONFIG_DUALSP_SERIAL)	+= serial-dualsp.o

obj-y		+= $(obj-$(MACHINE))

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
