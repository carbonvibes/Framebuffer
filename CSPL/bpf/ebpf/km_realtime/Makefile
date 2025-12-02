obj-m := vblank_capture.o

KERNEL_DIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

install: all
	sudo insmod realtime_fb_extractor.ko

uninstall:
	sudo rmmod realtime_fb_extractor || true

reload: uninstall install

test:
	@echo "Testing real-time framebuffer capture..."
	@echo "Capturing info:"
	@sudo cat /proc/drm_fb_realtime || echo "Module not loaded"
	@echo "Getting pixel data sample:"
	@sudo dd if=/proc/drm_fb_realtime_raw bs=1 count=1024 of=sample.raw 2>/dev/null || echo "No pixel data available"
	@if [ -f sample.raw ]; then echo "Sample captured: $$(wc -c < sample.raw) bytes"; fi

monitor:
	@echo "Monitoring real-time captures (Ctrl+C to stop)..."
	@while true; do \
		echo "=== $$(date) ==="; \
		sudo cat /proc/drm_fb_realtime 2>/dev/null | head -10; \
		sleep 2; \
	done

benchmark:
	@echo "Benchmarking capture performance..."
	@sudo cat /proc/drm_fb_realtime | grep -E "(Total|Successful|VBlank|Atomic)"

.PHONY: all clean install uninstall reload test monitor benchmark
