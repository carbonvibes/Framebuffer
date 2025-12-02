obj-m += pageflip_blocker.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod pageflip_blocker.ko

uninstall:
	sudo rmmod pageflip_blocker

test: install
	@echo "Testing page flip blocker..."
	@echo 4 | sudo tee /sys/module/pageflip_blocker/parameters/freeze_duration
	@cat /sys/kernel/pageflip_freezer/status
	@echo "Triggering 4-second freeze (cursor should still move)..."
	@echo 1 | sudo tee /sys/kernel/pageflip_freezer/freeze
	@sleep 5
	@cat /sys/kernel/pageflip_freezer/status

.PHONY: all clean install uninstall test
