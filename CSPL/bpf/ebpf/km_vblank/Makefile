obj-m += vblank_realtime_extractor.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install:
	sudo insmod vblank_realtime_extractor.ko

uninstall:
	sudo rmmod vblank_realtime_extractor

reload: uninstall clean all install

test:
	@echo "Testing VBlank realtime capture module..."
	@echo "Checking proc files:"
	@ls -la /proc/vblank_realtime_capture /proc/vblank_realtime_raw 2>/dev/null || echo "Proc files not found"
	@echo "Recent kernel messages:"
	@dmesg | tail -10
	@echo "Module capture status:"
	@cat /proc/vblank_realtime_capture 2>/dev/null || echo "Cannot read capture status"

.PHONY: all clean install uninstall reload test
