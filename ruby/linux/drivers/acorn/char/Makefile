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

# Object file lists.

obj-y   	:=
obj-m		:=
obj-n		:=
obj-		:=

obj-clps7500	:= defkeymap-acorn.o

obj-$(CONFIG_ATOMWIDE_SERIAL)	+= serial-atomwide.o
obj-$(CONFIG_DUALSP_SERIAL)	+= serial-dualsp.o
obj-$(CONFIG_ARCH_ACORN)	+= defkeymap-acorn.o i2c.o pcf8583.o

# Do the i2c and rtc last
obj-y		+= $(obj-$(MACHINE)) 

include $(TOPDIR)/Rules.make
