# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@transvirtual.com>
# Rewritten to use lists instead of if-statements.

O_TARGET := video.o

obj-y	:= 

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    := fbmem.o fbgen.o fbcmap.o fbmon.o modedb.o vga.o cyber2000fb.o sa1100fb.o

# Each configuration option enables a list of files.

obj-$(CONFIG_SGI_NEWPORT_CONSOLE) += newport_con.o
obj-$(CONFIG_PROM_CONSOLE)        += promcon.o promcon_tbl.o
obj-$(CONFIG_STI_CONSOLE)	  += sticon.o sticon-bmode.o sticore.o
obj-$(CONFIG_VGA_CONSOLE)         += vgacon.o vga.o
obj-$(CONFIG_MDA_CONSOLE)         += mdacon.o
obj-$(CONFIG_NVIDIA_CONSOLE)	  += nvvgacon.o
obj-$(CONFIG_FRAMEBUFFER_CONSOLE) += fbcon.o
obj-$(CONFIG_DUMMY_CONSOLE)	  += dummycon.o

subdir-$(CONFIG_FONTS)            += fonts 
ifeq ($(CONFIG_FONTS),y)
obj-y                             += fonts/fonts.o
endif

# Add fbmon.o back into obj-$(CONFIG_FB) in 2.5.x
obj-$(CONFIG_FB)                  += fbmem.o fbcmap.o modedb.o fbgen.o fbmon.o
# Only include macmodes.o if we have FB support and are PPC
ifeq ($(CONFIG_FB),y)
obj-$(CONFIG_PPC)                 += macmodes.o
endif

obj-$(CONFIG_FB_ACORN)            += acornfb.o
obj-$(CONFIG_FB_AMIGA)            += amifb.o
obj-$(CONFIG_FB_PM2)              += pm2fb.o
obj-$(CONFIG_FB_PM3)              += pm3fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_APOLLO)           += dnfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_Q40)              += q40fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_ATARI)            += atafb.o
obj-$(CONFIG_FB_ATY128)           += aty128fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_RADEON)           += radeonfb.o
obj-$(CONFIG_FB_NEOMAGIC)	  += neofb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_IGA)              += igafb.o
obj-$(CONFIG_FB_CONTROL)          += controlfb.o
obj-$(CONFIG_FB_PLATINUM)         += platinumfb.o
obj-$(CONFIG_FB_VALKYRIE)         += valkyriefb.o
obj-$(CONFIG_FB_CT65550)          += chipsfb.o
obj-$(CONFIG_FB_ANAKIN)		  += anakinfb.o	cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CLPS711X)         += clps711xfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CYBER)            += cyberfb.o 
obj-$(CONFIG_FB_CYBER2000)        += cyber2000fb.o
obj-$(CONFIG_FB_SGIVW)            += sgivwfb.o
obj-$(CONFIG_FB_MAC)              += macfb.o macmodes.o
obj-$(CONFIG_FB_HP300)            += hpfb.o cfbfillrect.o cfbimgblt.o
obj-$(CONFIG_FB_OF)               += offb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_IMSTT)            += imsttfb.o
obj-$(CONFIG_FB_RETINAZ3)         += retz3fb.o
obj-$(CONFIG_FB_CLGEN)            += clgenfb.o
obj-$(CONFIG_FB_S3TRIO)           += S3triofb.o
obj-$(CONFIG_FB_TGA)              += tgafb.o
obj-$(CONFIG_FB_VESA)             += vesafb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_VGA16)            += vga16fb.o vga.o
obj-$(CONFIG_FB_VIRGE)            += virgefb.o
obj-$(CONFIG_FB_G364)             += g364fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_FM2)              += fm2fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_CREATOR)          += creatorfb.o sbusfb.o
obj-$(CONFIG_FB_CGSIX)            += cgsixfb.o sbusfb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o sbusfb.o
obj-$(CONFIG_FB_CGTHREE)          += cgthreefb.o sbusfb.o
obj-$(CONFIG_FB_TCX)              += tcxfb.o sbusfb.o
obj-$(CONFIG_FB_CGFOURTEEN)       += cgfourteenfb.o sbusfb.o
obj-$(CONFIG_FB_P9100)            += p9100fb.o sbusfb.o
obj-$(CONFIG_FB_LEO)              += leofb.o sbusfb.o
obj-$(CONFIG_FB_STI)             += stifb.o sticore.o
obj-$(CONFIG_FB_PMAG_BA)          += pmag-ba-fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_PMAGB_B)          += pmagb-b-fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_MAXINE)           += maxinefb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_TX3912)           += tx3912fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o

obj-$(CONFIG_FB_MATROX)		  += matrox/
obj-$(CONFIG_FB_RIVA)		  += riva/
obj-$(CONFIG_FB_SIS)		  += sis/
obj-$(CONFIG_FB_ATY)		  += aty/
obj-$(CONFIG_FB_TDFX)		  += tdfx/
	
obj-$(CONFIG_FB_SUN3)             += sun3fb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o
obj-$(CONFIG_FB_HGA)              += hgafb.o  
obj-$(CONFIG_FB_SA1100)           += sa1100fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_VIRTUAL)          += vfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o 
obj-$(CONFIG_FB_HIT)              += hitfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_E1355)            += epson1355fb.o
obj-$(CONFIG_FB_PVR2)             += pvr2fb.o
obj-$(CONFIG_FB_VOODOO1)          += sstfb.o
obj-$(CONFIG_FB_ANAKIN)           += anakinfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_SIMPLE)		  += sfb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_SED1345)	  += sed1345fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_R3912)		  += r3912fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_VR4181)           += vr4181fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o
obj-$(CONFIG_FB_VRC4171)          += vrc4171fb.o cfbfillrect.o cfbcopyarea.o cfbimgblt.o

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

host-progs := ../conmakehash

promcon_tbl.c: prom.uni ../char/conmakehash
	../char/conmakehash prom.uni | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > promcon_tbl.c

promcon_tbl.o: promcon_tbl.c $(TOPDIR)/include/linux/types.h

