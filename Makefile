KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Compile flags.
# TODO need some way to check for the presence of msse3 or other SIMD instructions
# AVX2 is nice but not really needed
# If those aren't present then fall back to the normal rs version, will have to have ifdef's in the code
ccflags-y += -I$(src)/include/ -g -msse3 -msse4.1 -mpreferred-stack-boundary=3

# Modules.
AFS_MODULES :=	src/modules/dm_afs_fat32.o	\
				src/modules/dm_afs_ext4.o	\
				src/modules/dm_afs_ntfs.o       \
                                src/modules/dm_afs_shadow.o

# Libraries
AFS_LIBRARIES := src/lib/bit_vector.o src/lib/cauchy_rs.o src/lib/libgfshare.o src/lib/city.o src/lib/aont.o src/lib/speck.o src/lib/sha3.o

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
#   8388608 = 4GB Artifice instance
#   16777216 = 8GB Artifice Instance
#   33554432 = 16GB Instance

#create an artifice instance with debug mode enabled
debug_create:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 524288 artifice 0 pass /dev/sdb1 --entropy /home/Documents/ | sudo dmsetup create artifice

#mount an existing artifice instance with debug mode enabled
debug_mount:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 1048576 artifice 1 pass /dev/sdb1 | sudo dmsetup create artifice

#run a pass of the bench script as a sanity check
debug_bench: build_bench
	(cd scripts/bench; sudo python bench.py -i 1)

#run test for read, write, and combined
debug_bench_full: build_bench
	(cd scripts/bench; sudo python bench.py -i 30; sudo python bench.py -i 30 -o r; sudo python bench.py -i 30 -o rw)

#perform a single block write
debug_write:
	sudo dd if=README.md of=/dev/mapper/artifice bs=4096 count=1 oflag=direct

#performa a single block read, save output to a file
debug_read:
	touch read_test_output
	sudo dd if=/dev/mapper/artifice of=test_output bs=4096 count=1 oflag=direct

#unmount the artifice instance
debug_end:
	@sudo dmsetup remove artifice || true
	@sudo rmmod dm_afs || true

#For this target make sure that 100GB of free space is available, 4GB of RAM in the system
#pilot and bonnie++ must be installed
#These two tests are for benchmarking. There is an effective artifice size of 16GB
#Performance results for papers were generated using these targets
debug_bonnie:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 524288 artifice 0 pass /dev/sdb1 --entropy /home/movies/ | sudo dmsetup create artifice
	rm -rf test
	mkdir test
	@sudo mkfs.ext4 /dev/mapper/artifice
	@sudo mount /dev/mapper/artifice test
	@sudo bonnie++ -d test -u root
	@sudo umount test
	@sudo fsck /dev/mapper/artifice
	@sudo dmsetup remove artifice || true
	@sudo rmmod dm_afs || true

debug_pilot:
	@sudo insmod dm_afs.ko afs_debug_mode=1
	@echo 0 33554432 artifice 0 pass /dev/sdb1 --entropy /home/movies/ | sudo dmsetup create artifice
	rm -rf test
	mkdir test
	@sudo mkfs.ext4 /dev/mapper/artifice
	@sudo mount /dev/mapper/artifice test
	#(cd scripts/benchmarks; sudo ./pilot.sh ../../test 4096 root w; sudo ./pilot.sh ../../test 4096 root r)
	(cd scripts/benchmarks; sudo ./pilot.sh ../../test 4096 root r)
	@sudo umount test
	@sudo dmsetup remove artifice || true
	@sudo rmmod dm_afs || true

debug_base:
	@rm -rf test
	@mkdir test
	@sudo mkfs.ext4 /dev/sdb1
	@sudo mount /dev/sdb1 test 
	(cd scripts/benchmarks; sudo ./pilot.sh ../../test 4096 root w; sudo ./pilot.sh ../../test 4096 root r)
	@sudo umount test

######################################################################
