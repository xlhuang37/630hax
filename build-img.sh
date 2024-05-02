#!/bin/sh

set -e

. ./script-params

if ! lsmod | grep nbd; then
   echo "Please ask an administator to load the nbd kernel extension.\n"
   exit 0
fi

mkdir -p images/mnt

qemu-img create -f qcow2 ${IMAGE}.img 4G

sudo qemu-nbd -f qcow2 -c ${DEV} ${IMAGE}.img
sudo mkfs.ext4 ${DEV}

sudo mount ${DEV} ${MNT}

sudo debootstrap --variant buildd --include=linux-image-generic,vim,systemd-sysv,rsyslog jammy ${MNT}

# remove the first 'x' after root so you can log in and run sudo without a password
sudo sed -i -e 's/^root:x:/root::/g' ${MNT}/etc/passwd

sudo umount ${MNT}
sudo qemu-nbd -d ${DEV}

# We believe that one can only provide the kernel outside the image
# via a raw image, not qcow2
qemu-img convert -f qcow2 -O raw ${IMAGE}.img ${IMAGE}.raw

# XXX ttyS0 not working...
