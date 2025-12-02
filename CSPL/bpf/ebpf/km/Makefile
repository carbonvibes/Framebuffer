obj-m += drm_fb_extractor.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.order *.symvers

install: all
	sudo insmod drm_fb_extractor.ko

uninstall:
	sudo rmmod drm_fb_extractor

.PHONY: all clean install uninstall
