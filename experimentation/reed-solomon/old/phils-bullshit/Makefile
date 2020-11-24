PWD := $(shell pwd)

ccflags-y += -I$(src)/include/

RStest-objs := lkm_template.o rs.o
obj-m += RStest.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

test:
	sudo modprobe reed-solomon 
	sudo insmod RStest.ko
	sleep 3
	sudo rmmod RStest.ko

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
