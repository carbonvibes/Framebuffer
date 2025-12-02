obj-m += atomic_commit_blocker.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.order *.symvers

install: all
	sudo insmod atomic_commit_blocker.ko

uninstall:
	sudo rmmod atomic_commit_blocker

test: install
	@echo "Testing atomic commit blocker..."
	@echo "Setting 3 second freeze duration..."
	@echo 3 | sudo tee /sys/module/atomic_commit_blocker/parameters/freeze_duration
	@echo "Checking initial status..."
	@cat /sys/kernel/atomic_freezer/status
	@echo "Triggering freeze in 3 seconds..."
	@sleep 1 && echo "3..." && sleep 1 && echo "2..." && sleep 1 && echo "1..."
	@echo 1 | sudo tee /sys/kernel/atomic_freezer/trigger
	@echo "Freeze triggered! Monitor dmesg for recovery..."
	@sleep 4
	@echo "Final status:"
	@cat /sys/kernel/atomic_freezer/status

.PHONY: all clean install uninstall test
