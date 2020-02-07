KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Compile flags.
# TODO need some way to check for the presence of msse3 or other SIMD instructions
# AVX2 is nice but not really needed
# If those aren't present then fall back to the normal rs version, will have to have ifdef's in the code
ccflags-y += -I$(src)/include/ -g -msse3 -msse4.1 -mavx2 -mpreferred-stack-boundary=4

# Modules.
AFS_MODULES :=	src/modules/dm_afs_fat32.o	\
				src/modules/dm_afs_ext4.o	\
				src/modules/dm_afs_ntfs.o

# Libraries
AFS_LIBRARIES := src/lib/bit_vector.o src/lib/cauchy_rs.o src/lib/libgfshare.o src/lib/city.o

# Kernel module.
obj-m 	 := dm_afs.o
dm_afs-y := src/dm_afs.o            \
			src/dm_afs_metadata.o   \
			src/dm_afs_engine.o     \
			src/dm_afs_allocation.o \
			src/dm_afs_crypto.o     \
			src/dm_afs_io.o         \
			src/dm_afs_entropy.o    \
			$(AFS_LIBRARIES)        \
			$(AFS_MODULES)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) clean
	rm -f src/*.rc src/modules/*.rc src/lib/*.rc

build_bench:
	(cd scripts/bench; make)

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
# MY CONFIGURATION: /dev/sdb is 10GB.
# Sectors:
#	196608  = 096MB Artifice instance.
#	524288  = 256MB Artifice instance.
#   1048576 = 512MB Artifice instance.
#   4194304 = 2GB Artifice instance.
#   16777216 = 8GB Artifice Instance
#   33554432 = 16GB Instance

debug_create:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 16777216 artifice 0 pass /dev/sdb1 --entropy /home/movies/ | sudo dmsetup create artifice

debug_mount:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 1048576 artifice 1 pass /dev/sdb1 | sudo dmsetup create artifice

debug_bench: build_bench
	(cd scripts/bench; sudo python bench.py -i 1)

debug_bench_full: build_bench
	(cd scripts/bench; sudo python bench.py -i 30; sudo python bench.py -i 30 -o r; sudo python bench.py -i 30 -o rw)

debug_write:
	sudo dd if=/dev/zero of=/dev/mapper/artifice bs=4096 count=1 oflag=direct

debug_read:
	touch test_output
	sudo dd if=/dev/mapper/artifice of=test_output bs=4096 count=1 oflag=direct

debug_end:
	@sudo dmsetup remove artifice || true
	@sudo rmmod dm_afs || true
######################################################################
