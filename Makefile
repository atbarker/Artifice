KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Extra flags.
ccflags-y += -I$(src)/include/

# Kernel module object name.
obj-m += dm_mks.o
dm_mks-y := src/dm_mks.o

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

######################################################################
# Hack for easier loading and unloading
debug:
	@sudo insmod dm_mks.ko dm_mks_debug_mode=1
	@echo 0 1024 mks pass test_drive | sudo dmsetup create matryoshka

debug_end:
	@sudo dmsetup remove matryoshka
	@sudo rmmod dm_mks
######################################################################