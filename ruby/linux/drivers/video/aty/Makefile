
O_TARGET := atyfb.o

export-objs    :=  atyfb_base.o mach64_accel.o

obj-$(CONFIG_FB_ATY)		+= atyfb_base.o mach64_accel.o
obj-$(CONFIG_FB_ATY_GX)		+= mach64_gx.o
obj-$(CONFIG_FB_ATY_CT)		+= mach64_ct.o mach64_cursor.o

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.[oas]
