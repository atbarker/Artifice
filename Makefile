KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m := src/dm_mks.o

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean