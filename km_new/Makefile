# Makefile for DRM Framebuffer Pixel Extractor Kernel Module with Intel Detiling
obj-m += drm_fb_pixel_extractor.o

# Map the source file to the module object
drm_fb_pixel_extractor-objs := kernel.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.order *.symvers

install: all
	sudo insmod drm_fb_pixel_extractor.ko

uninstall:
	sudo rmmod drm_fb_pixel_extractor

reload: uninstall clean all install

# Test targets
test:
	@echo "=== Framebuffer Capture Info ==="
	@cat /proc/drm_fb_pixels
	@echo "=== End Info ==="

extract:
	@echo "Extracting linear framebuffer data..."
	sudo dd if=/proc/drm_fb_raw of=framebuffer_linear.raw bs=1 2>/dev/null || true
	@if [ -f framebuffer_linear.raw ]; then \
		echo "Extracted to framebuffer_linear.raw (size: $$(stat -c%s framebuffer_linear.raw) bytes)"; \
	else \
		echo "No framebuffer data available"; \
	fi

# Debug target to show information
info:
	@echo "Kernel build directory: $(KDIR)"
	@echo "Module directory: $(PWD)"
	@echo "Source file: kernel.c"
	@echo "Module object: drm_fb_pixel_extractor.ko"
	@echo "Features: Intel X/Y-tiling detiling support"

# Check if kernel headers are installed
check:
	@if [ ! -d "$(KDIR)" ]; then \
		echo "Error: Kernel headers not found at $(KDIR)"; \
		echo "Install with: sudo apt-get install linux-headers-$(shell uname -r)"; \
		exit 1; \
	else \
		echo "Kernel headers found at $(KDIR)"; \
	fi

.PHONY: all clean install uninstall reload test extract info check
