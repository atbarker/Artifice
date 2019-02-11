PWD := $(shell pwd)

ccflags-y += -I$(src)/include/

RStest-objs := lkm_template.o dm_afs_entropy.o
obj-m += EntropyTest.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

test:
	sudo insmod EntropyTest.ko
	sleep 3
	sudo rmmod EntropyTest.ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
