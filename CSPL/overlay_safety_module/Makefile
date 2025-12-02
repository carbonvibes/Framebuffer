obj-m += userspace_overlay_test.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod.c *.mod *.order *.symvers

install: all
	sudo insmod direct_overlay_test.ko

uninstall:
	sudo rmmod direct_overlay_test

status:
	cat /proc/direct_overlay_test

dmesg:
	dmesg | tail -20

.PHONY: all clean install uninstall status dmesg
