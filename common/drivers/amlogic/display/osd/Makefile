#
# Makefile for the AML Apollo framebuffer device driver
#


obj-$(CONFIG_FB_AM) += fb.o 

fb-objs =  osd_hw.o osd_main.o osd_dev.o  osd_antiflicker.o osd_clone.o

ifeq ($(CONFIG_ARCH_MESON8),y)
fb-objs += osd_prot.o
endif

obj-$(CONFIG_VSYNC_RDMA) += osd_rdma.o

obj-$(CONFIG_HIBERNATION) +=  osd_progressbar.o

EXTRA_CFLAGS += -Iinclude/linux/osd/ -Idrivers/staging/android/




