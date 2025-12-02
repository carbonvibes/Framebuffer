obj-m += vsync_blocker.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod vsync_blocker.ko

uninstall:
	sudo rmmod vsync_blocker

test: install
	@echo "Testing VSync blocker..."
	@echo 2 | sudo tee /sys/module/vsync_blocker/parameters/freeze_duration
	@cat /sys/kernel/controlled_freezer/status
	@echo "Triggering 2-second freeze..."
	@echo 1 | sudo tee /sys/kernel/controlled_freezer/freeze
	@sleep 3
	@cat /sys/kernel/controlled_freezer/status

.PHONY: all clean install uninstall test
