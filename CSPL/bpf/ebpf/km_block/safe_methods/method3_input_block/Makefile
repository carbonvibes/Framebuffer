obj-m += input_blocker.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install: all
	sudo insmod input_blocker.ko

uninstall:
	sudo rmmod input_blocker

test: install
	@echo "Testing input blocker (safest method)..."
	@echo 3 | sudo tee /sys/module/input_blocker/parameters/freeze_duration
	@cat /sys/kernel/input_freezer/status
	@echo "Freezing input for 3 seconds..."
	@echo "Try moving mouse/typing - it should be blocked!"
	@echo 1 | sudo tee /sys/kernel/input_freezer/freeze
	@sleep 4
	@echo "Input should be restored now"
	@cat /sys/kernel/input_freezer/status

emergency: 
	@echo "Emergency unblock activated!"
	@echo 1 | sudo tee /sys/kernel/input_freezer/emergency_unblock

.PHONY: all clean install uninstall test emergency
