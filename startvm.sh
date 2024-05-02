#!/bin/bash

. ./script-params

# try to generate a unique GDB port
GDBPORT=$(((5000 + `id -u`) % 25000))


while getopts g arg;
do
    case $arg in
	g)
	    DEBUG="-S"
	    echo "gdb is listening on port $GDBPORT"
	    ;;
    esac
done

qemu-system-x86_64 -machine q35 -cpu Nehalem -m 4G -smp 2 -serial mon:stdio \
		   -gdb tcp::${GDBPORT} ${DEBUG} \
		   -append "console=ttyS0 root=/dev/vda rw"  \
		   -kernel ./linux-6.8.7/arch/x86/boot/bzImage \
		   -drive file=./images/jammy.raw,format=raw,if=virtio
