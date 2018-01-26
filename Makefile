KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Extra flags.
ccflags-y += -I$(src)/include/

# Kernel module object name.
obj-m := dm_mks.o
dm_mks-y := src/dm_mks.o src/dm_mks_utilities.o src/lib/dm_mks_fat32.o

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean

######################################################################
# Hack for easier loading and unloading
# Make sure your VM has another disk and its mounted at /dev/sdb, with
# a partition at /dev/sdb1.
debug:
	@sudo insmod dm_mks.ko mks_debug_mode=1
	@echo 0 1024 mks pass /dev/sdb1 | sudo dmsetup create matryoshka

debug_end:
	@sudo dmsetup remove matryoshka
	@sudo rmmod dm_mks
######################################################################