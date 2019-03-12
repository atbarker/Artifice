KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Compile flags.
ccflags-y += -I$(src)/include/

# Modules.
AFS_MODULES :=	src/modules/dm_afs_fat32.o	\
				src/modules/dm_afs_ext4.o	\
				src/modules/dm_afs_ntfs.o

# Libraries
AFS_LIBRARIES := src/lib/bit_vector.o src/lib/rs.o src/lib/libgfshare.o

# Kernel module.
obj-m 	 := dm_afs.o
dm_afs-y := src/dm_afs.o            \
			src/dm_afs_metadata.o   \
			src/dm_afs_engine.o     \
			src/dm_afs_allocation.o \
			src/dm_afs_crypto.o     \
			src/dm_afs_io.o         \
			$(AFS_LIBRARIES)        \
			$(AFS_MODULES)

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
# Make sure your VM has another disk and its mounted at /dev/sdb.
#
# MY CONFIGURATION: /dev/sdb is 4GB.
# Sectors:
#	196608  = 096MB Artifice instance.
#	524288  = 256MB Artifice instance.
#   1048576 = 512MB Artifice instance.

debug_create:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 1048576 artifice 0 pass /dev/sdb --entropy /home/movies/ | sudo dmsetup create artifice

debug_mount:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 1048576 artifice 1 pass /dev/sdb | sudo dmsetup create artifice

debug_end:
	@sudo dmsetup remove artifice || true
	@sudo rmmod dm_afs || true
######################################################################
