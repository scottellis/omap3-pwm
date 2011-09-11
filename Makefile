# cross-compile module makefile

ifneq ($(KERNELRELEASE),)
    obj-m := pwm.o
else
    PWD := $(shell pwd)

default:
ifeq ($(strip $(KERNELDIR)),)
	$(error "KERNELDIR is undefined!")
else
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules 
endif

install:
	scp pwm.ko root@192.168.10.106:/home/root


clean:
	rm -rf *~ *.ko *.o *.mod.c modules.order Module.symvers .pwm* .tmp_versions

endif

