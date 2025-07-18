KDIR = /lib/modules/`uname -r`/build

obj-m := gpiosignal_module.o

default:
	$(MAKE) -C $(KDIR) M=$$PWD modules

clean:
	$(MAKE) -C $(KDIR) M=$$PWD clean