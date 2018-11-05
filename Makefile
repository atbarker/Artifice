KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Compile flags.
ccflags-y += -I$(src)/include/

# Modules.
AFS_MODULES :=	src/modules/dm_afs_fat32.o	\
				src/modules/dm_afs_ext4.o	\
				src/modules/dm_afs_ntfs.o

# Libraries
AFS_LIBRARIES := src/lib/bit_vector.o

# Kernel module.
obj-m 	 := dm_afs.o
dm_afs-y := src/dm_afs.o src/dm_afs_common.o $(AFS_LIBRARIES) $(AFS_MODULES)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f src/*.rc src/modules/*.rc src/lib/*.rc

load:
	@make
	@sudo insmod dm_afs.ko afs_debug_mode=1

unload:
	@sudo rmmod dm_afs
	@make clean

reload:
	@make unload || true
	@make load || true

######################################################################
# Hack for easier loading and unloading
# Make sure your VM has another disk and its mounted at /dev/sdb, with
# a partition at /dev/sdb1.
debug_create:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 65536 artifice 0 pass /dev/sdb --entropy /home/movies/ | sudo dmsetup create artifice

debug_mount:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 65536 artifice 1 pass /dev/sdb | sudo dmsetup create artifice

debug_end:
	@sudo dmsetup remove artifice || true
	@sudo rmmod dm_afs || true
######################################################################