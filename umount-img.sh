#!/bin/sh

. ./script-params

sudo umount  ${MNT}/dev/pts
sleep 1
sudo umount  ${MNT}/dev
sleep 1
sudo umount  ${MNT}/sys
sleep 1
sudo umount  ${MNT}/proc
sudo umount  ${MNT}

sudo qemu-nbd -d ${DEV}
