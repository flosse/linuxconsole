#
# Makefile for the kernel Input device drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
MOD_IN_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS)

# The target object and module list name.

O_TARGET	:= inputdrv.o
M_OBJS		:=
O_OBJS		:=

# Objects that export symbols.

export-objs	:= input.o serio.o gameport.o
common-objs	:= input.o serio.o gameport.o

# Multipart objects.

hid-objs	:= hid-core.o hid-input.o

ifeq ($(CONFIG_USB_HIDDEV),y)
	hid-objs	+= hiddev.o
endif

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT)		+= input.o

obj-$(CONFIG_INPUT_I8042)	+= i8042.o serio.o
obj-$(CONFIG_INPUT_CT82C710)	+= ct82c710.o serio.o
obj-$(CONFIG_INPUT_Q40KBD)	+= q40kbd.o serio.o
#obj-$(CONFIG_INPUT_SUN8042)	+= sun8042.o serio.o
obj-$(CONFIG_INPUT_RPCKBD)	+= rpckbd.o serio.o
#obj-$(CONFIG_INPUT_JAZZKBD)	+= jazzkbd.o serio.o
#obj-$(CONFIG_INPUT_MIPSKBD)	+= mipskbd.o serio.o
obj-$(CONFIG_INPUT_PARKBD)	+= parkbd.o serio.o

obj-$(CONFIG_INPUT_SERPORT_OLD)	+= serport_old.o serio.o

obj-$(CONFIG_INPUT_NS558)	+= ns558.o gameport.o
obj-$(CONFIG_INPUT_LIGHTNING)	+= lightning.o gameport.o
obj-$(CONFIG_INPUT_PCIGAME)	+= pcigame.o gameport.o
obj-$(CONFIG_INPUT_CS461X)	+= cs461x.o gameport.o
obj-$(CONFIG_INPUT_EMU10K1)	+= emu10k1-gp.o gameport.o

obj-$(CONFIG_INPUT_ATKBD)	+= atkbd.o serio.o
obj-$(CONFIG_INPUT_XTKBD)	+= xtkbd.o serio.o
obj-$(CONFIG_INPUT_PSMOUSE)	+= psmouse.o serio.o
obj-$(CONFIG_INPUT_NEWTON)	+= newtonkbd.o serio.o

obj-$(CONFIG_INPUT_SERMOUSE)	+= sermouse.o serio.o
obj-$(CONFIG_INPUT_PS2SERIAL)	+= ps2serkbd.o serio.o
obj-$(CONFIG_INPUT_TWIDSERIAL)	+= twidjoy.o serio.o
obj-$(CONFIG_INPUT_SUNKBD)	+= sunkbd.o serio.o
obj-$(CONFIG_INPUT_WARRIOR)	+= warrior.o serio.o
obj-$(CONFIG_INPUT_MAGELLAN)	+= magellan.o serio.o
obj-$(CONFIG_INPUT_SPACEORB)	+= spaceorb.o serio.o
obj-$(CONFIG_INPUT_SPACEBALL)	+= spaceball.o serio.o
obj-$(CONFIG_INPUT_STINGER)	+= stinger.o serio.o
obj-$(CONFIG_INPUT_IFORCE_232)	+= iforce.o serio.o
obj-$(CONFIG_INPUT_GUNZE)	+= gunze.o serio.o
obj-$(CONFIG_TOUCHSCREEN_INPUT_BITSY) += h3600_ts_input.o serio.o

obj-$(CONFIG_INPUT_ANALOG)	+= analog.o gameport.o
obj-$(CONFIG_INPUT_A3D)		+= a3d.o gameport.o
obj-$(CONFIG_INPUT_ADI)		+= adi.o gameport.o
obj-$(CONFIG_INPUT_COBRA)	+= cobra.o gameport.o
obj-$(CONFIG_INPUT_GF2K)	+= gf2k.o gameport.o
obj-$(CONFIG_INPUT_GRIP)	+= grip.o gameport.o
obj-$(CONFIG_INPUT_GUILLEMOT)	+= guillemot.o gameport.o
obj-$(CONFIG_INPUT_INTERACT)	+= interact.o gameport.o
obj-$(CONFIG_INPUT_TMDC)	+= tmdc.o gameport.o
obj-$(CONFIG_INPUT_SIDEWINDER)	+= sidewinder.o gameport.o
obj-$(CONFIG_INPUT_JOYDUMP)	+= joydump.o gameport.o

obj-$(CONFIG_INPUT_DB9)		+= db9.o
obj-$(CONFIG_INPUT_GAMECON)	+= gamecon.o
obj-$(CONFIG_INPUT_TURBOGRAFX)	+= turbografx.o

obj-$(CONFIG_INPUT_HID)		+= hid.o
obj-$(CONFIG_INPUT_USBMOUSE)	+= usbmouse.o
obj-$(CONFIG_INPUT_USBKBD)	+= usbkbd.o
obj-$(CONFIG_INPUT_WACOMUSB)	+= wacom.o
obj-$(CONFIG_INPUT_IFORCE_USB)	+= iforce.o

obj-$(CONFIG_INPUT_ADBHID)	+= adbhid.o
obj-$(CONFIG_MAC_EMUMOUSEBTN)	+= mac_hid.o

obj-$(CONFIG_INPUT_KEYBDEV)	+= keybdev.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o
obj-$(CONFIG_INPUT_TOUCHSCREEN) += tsdev.o
obj-$(CONFIG_PM)		+= power.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o
obj-$(CONFIG_INPUT_EVBUG)	+= evbug.o

obj-$(CONFIG_INPUT_INPORT)	+= inport.o
obj-$(CONFIG_INPUT_LOGIBM)	+= logibm.o
obj-$(CONFIG_INPUT_PC110PAD)	+= pc110pad.o
#obj-$(CONIG_INPUT_BTTV)	+= bttv.o
obj-$(CONFIG_INPUT_AMIKEYB)	+= amikeyb.o
obj-$(CONFIG_INPUT_AMIMOUSE)	+= amimouse.o
obj-$(CONFIG_INPUT_AMIJOY)	+= amijoy.o
#obj-$(CONFIG_INPUT_ATARIKBD)	+= atarikbd.o
#obj-$(CONFIG_INPUT_ATARIMOUSE) += atarimouse.o
#obj-$(CONFIG_INPUT_ATARIJOY)	+= atarijoy.o
obj-$(CONFIG_INPUT_RPCMOUSE)	+= rpcmouse.o
#obj-$(CONFIG_INPUT_ARCKBD)	+= arckbd.o
#obj-$(CONFIG_INPUT_APOLLOKBD)	+= apollokbd.o
#obj-$(CONFIG_INPUT_HPHIL)	+= hphil.o

# Remove duplicates.
  
common-y	:= $(filter $(obj-y),$(common-obj))
obj-y		:= $(filter-out $(common-y), $(obj-y)) $(common-y)

# Files that are both resident and modular: remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Translate to Rules.make lists.

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

# The global Rules.make.

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

hid.o: $(hid-objs)
	$(LD) -r -o $@ $(hid-objs)
