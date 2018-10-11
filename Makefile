KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Compile flags.
ccflags-y += -I$(src)/include/

# Kernel module.
obj-m 	 := dm_afs.o
dm_afs-y := src/dm_afs.o 				\
			src/dm_afs_utilities.o 		\
			src/modules/dm_afs_fat32.o	\
			src/modules/dm_afs_ext4.o	\
			src/modules/dm_afs_ntfs.o

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f src/*.rc src/modules/*.rc

######################################################################
# Hack for easier loading and unloading
# Make sure your VM has another disk and its mounted at /dev/sdb, with
# a partition at /dev/sdb1.
debug:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 1024 afs pass /dev/sdb1 | sudo dmsetup create matryoshka

debug1:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 1024 afs pass /dev/sde1 | sudo dmsetup create matryoshka

debug_end:
	@sudo dmsetup remove matryoshka
	@sudo rmmod dm_afs
######################################################################
