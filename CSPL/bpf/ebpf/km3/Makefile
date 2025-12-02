# Makefile for DRM Framebuffer Pixel Extractor v3 Kernel Module
obj-m += drm_fb_pixel_extractor_v3.o

# Map the source file to the module object
drm_fb_pixel_extractor_v3-objs := kernel.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.order *.symvers

install: all
	sudo insmod drm_fb_pixel_extractor_v3.ko

uninstall:
	sudo rmmod drm_fb_pixel_extractor_v3

# Debug target to show information
info:
	@echo "Kernel build directory: $(KDIR)"
	@echo "Module directory: $(PWD)"
	@echo "Source file: kernel.c"
	@echo "Module object: drm_fb_pixel_extractor_v3.ko"

# Check if kernel headers are installed
check:
	@if [ ! -d "$(KDIR)" ]; then \
		echo "Error: Kernel headers not found at $(KDIR)"; \
		echo "Install with: sudo apt-get install linux-headers-$(shell uname -r)"; \
		exit 1; \
	else \
		echo "Kernel headers found at $(KDIR)"; \
	fi

# Test build without installing
test: all
	@echo "Module built successfully: drm_fb_pixel_extractor_v3.ko"
	@ls -la *.ko

.PHONY: all clean install uninstall info check test
