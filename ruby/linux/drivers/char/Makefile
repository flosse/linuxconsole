#
# Makefile for the kernel character device drivers.
#

#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

O_TARGET := char.o

obj-y	 += mem.o tty_io.o n_tty.o tty_ioctl.o raw.o pty.o misc.o random.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=	vt.o keyboard.o sysrq.o \
			misc.o pty.o random.o selection.o serial.o \
			sonypi.o tty_io.o tty_ioctl.o generic_serial.o rtc.o \
			ip2main.o

mod-subdirs	:=	ftape drm pcmcia

ifeq ($(CONFIG_DECSTATION),y)
  SERIAL   = decserial.o
endif

ifeq ($(CONFIG_QTRONIX_KEYBOARD),y)
  KEYBD    = qtronix.o
  KEYMAP   = qtronixmap.o
endif

obj-$(CONFIG_VT) += vt.o vt_ioctl.o decvte.o vc_screen.o consolemap.o consolemap_deftbl.o selection.o keyboard.o defkeymap.o
obj-$(CONFIG_SERIAL) += $(SERIAL)
obj-$(CONFIG_SERIAL_ACPI) += acpi_serial.o

obj-$(CONFIG_MAGIC_SYSRQ) += sysrq.o
obj-$(CONFIG_ATARI_DSP56K) += dsp56k.o
obj-$(CONFIG_ROCKETPORT) += rocket.o
obj-$(CONFIG_MOXA_SMARTIO) += mxser.o
obj-$(CONFIG_MOXA_INTELLIO) += moxa.o
obj-$(CONFIG_DIGI) += pcxx.o
obj-$(CONFIG_DIGIEPCA) += epca.o
obj-$(CONFIG_CYCLADES) += cyclades.o
obj-$(CONFIG_STALLION) += stallion.o
obj-$(CONFIG_ISTALLION) += istallion.o
obj-$(CONFIG_COMPUTONE) += ip2.o ip2main.o
obj-$(CONFIG_RISCOM8) += riscom8.o
obj-$(CONFIG_ISI) += isicom.o
obj-$(CONFIG_ESPSERIAL) += esp.o
obj-$(CONFIG_SYNCLINK) += synclink.o
obj-$(CONFIG_SYNCLINKMP) += synclinkmp.o
obj-$(CONFIG_N_HDLC) += n_hdlc.o
obj-$(CONFIG_SPECIALIX) += specialix.o
obj-$(CONFIG_AMIGA_BUILTIN_SERIAL) += amiserial.o
obj-$(CONFIG_A2232) += ser_a2232.o generic_serial.o
obj-$(CONFIG_SX) += sx.o generic_serial.o
obj-$(CONFIG_RIO) += rio/rio.o generic_serial.o
obj-$(CONFIG_SH_SCI) += sh-sci.o generic_serial.o
obj-$(CONFIG_SERIAL167) += serial167.o
obj-$(CONFIG_MVME147_SCC) += generic_serial.o vme_scc.o
obj-$(CONFIG_MVME162_SCC) += generic_serial.o vme_scc.o
obj-$(CONFIG_BVME6000_SCC) += generic_serial.o vme_scc.o
obj-$(CONFIG_SERIAL_TX3912) += generic_serial.o serial_tx3912.o

subdir-$(CONFIG_RIO) += rio

obj-$(CONFIG_PRINTER) += lp.o
obj-$(CONFIG_DTLK) += dtlk.o
obj-$(CONFIG_R3964) += n_r3964.o
obj-$(CONFIG_APPLICOM) += applicom.o
obj-$(CONFIG_SONYPI) += sonypi.o
obj-$(CONFIG_RTC) += rtc.o
obj-$(CONFIG_EFI_RTC) += efirtc.o
ifeq ($(CONFIG_PPC),)
  obj-$(CONFIG_NVRAM) += nvram.o
endif
obj-$(CONFIG_TOSHIBA) += toshiba.o
obj-$(CONFIG_I8K) += i8k.o
obj-$(CONFIG_DS1620) += ds1620.o
obj-$(CONFIG_INTEL_RNG) += i810_rng.o

obj-$(CONFIG_QIC02_TAPE) += tpqic02.o

subdir-$(CONFIG_FTAPE) += ftape
subdir-$(CONFIG_DRM) += drm
subdir-$(CONFIG_PCMCIA) += pcmcia
subdir-$(CONFIG_AGP) += agp

ifeq ($(CONFIG_FTAPE),y)
obj-y       += ftape/ftape.o
endif

obj-$(CONFIG_H8) += h8.o
obj-$(CONFIG_PPDEV) += ppdev.o
obj-$(CONFIG_DZ) += dz.o
obj-$(CONFIG_NWBUTTON) += nwbutton.o
obj-$(CONFIG_NWFLASH) += nwflash.o

# Only one watchdog can succeed. We probe the hardware watchdog
# drivers first, then the softdog driver.  This means if your hardware
# watchdog dies or is 'borrowed' for some reason the software watchdog
# still gives you some cover.

obj-$(CONFIG_PCWATCHDOG) += pcwd.o
obj-$(CONFIG_ACQUIRE_WDT) += acquirewdt.o
obj-$(CONFIG_ADVANTECH_WDT) += advantechwdt.o
obj-$(CONFIG_IB700_WDT) += ib700wdt.o
obj-$(CONFIG_MIXCOMWD) += mixcomwd.o
obj-$(CONFIG_60XX_WDT) += sbc60xxwdt.o
obj-$(CONFIG_WDT) += wdt.o
obj-$(CONFIG_WDTPCI) += wdt_pci.o
obj-$(CONFIG_21285_WATCHDOG) += wdt285.o
obj-$(CONFIG_977_WATCHDOG) += wdt977.o
obj-$(CONFIG_I810_TCO) += i810-tco.o
obj-$(CONFIG_MACHZ_WDT) += machzwd.o
obj-$(CONFIG_SH_WDT) += shwdt.o
obj-$(CONFIG_EUROTECH_WDT) += eurotechwdt.o
obj-$(CONFIG_SOFT_WATCHDOG) += softdog.o

subdir-$(CONFIG_MWAVE) += mwave
ifeq ($(CONFIG_MWAVE),y)
  obj-y += mwave/mwave.o
endif

include $(TOPDIR)/Rules.make

fastdep:

conmakehash: conmakehash.c
	$(HOSTCC) $(HOSTCFLAGS) -o conmakehash conmakehash.c

consolemap_deftbl.c: $(FONTMAPFILE) conmakehash
	./conmakehash $(FONTMAPFILE) > consolemap_deftbl.c

consolemap_deftbl.o: consolemap_deftbl.c $(TOPDIR)/include/linux/types.h

.DELETE_ON_ERROR:

defkeymap.c: defkeymap.map
	set -e ; loadkeys --mktable $< | sed -e 's/^static *//' > $@

qtronixmap.c: qtronixmap.map
	set -e ; loadkeys --mktable $< | sed -e 's/^static *//' > $@
