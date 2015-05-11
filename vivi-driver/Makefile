
KERNEL_PATH := /home/suxin/work/nvidia/jetson-tk1/linux4tegra_r21.3/Source_Packages/kernel

TARGET_MOD := vivi

obj-m := vivi.o

CFLAGS_MODULE = -fno-pic

all:
	@echo "--Compiling '$(TARGET_MOD)'..."
	make -C $(KERNEL_PATH) M=$(PWD) modules

clean: 
	@make -C $(KERNEL_PATH) M=$(PWD) clean 

