module_name = keyboard_driver

src-test = test.c
obj-test = $(src-test:.c=.o)

src-m += scan_code_sets.c \
	 ps2_keyboard_state.c \
	main.c

obj-m += $(module_name).o
$(module_name)-objs += $(src-m:.c=.o)
#module-obj = $(obj-m:.o=.ko)

KERNELRELEASE=$(shell uname -r)
KDIR=/lib/modules/$(shell uname -r)/build
EXTRAFLAGS= -Wall  -Werror -g -DDEBUG -O -I$(PWD)
FLAGS= $(EXTRAFLAGS)
ccflags-y= $(EXTRAFLAGS)

all:
	make -j4 -C $(KDIR) M=$(PWD) modules

modules_install: all
	make -C $(KDIR) M=$(PWD) modules_install
#	cp $(module-obj) /lib/modules/$(shell uname -r)/extra
	sudo depmod -a

clean:
	make -C $(KDIR) M=$(PWD) clean

test: $(obj-test)
	gcc $^ $(FLAGS) -o test
	./test

%.o: %.c
	gcc $< $(FLAGS) -c
