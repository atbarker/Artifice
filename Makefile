KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Extra flags.
ccflags-y += -I$(src)/include/

# Kernel module object name.
obj-m += dm_mks.o

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean