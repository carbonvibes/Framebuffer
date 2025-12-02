# Makefile for DRM Frame Blocker Kernel Module (Minimal Version)
obj-m += drm_frame_blocker_minimal.o

# Map the source file to the module object
drm_frame_blocker_minimal-objs := kernel_minimal.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.order *.symvers

install: all
	sudo insmod drm_frame_blocker_minimal.ko

uninstall:
	sudo rmmod drm_frame_blocker_minimal

.PHONY: all clean install uninstall
