#!/bin/sh

. ./script-params

./mount-img.sh

sudo cp ${MODULE} ${MNT}/root/
sudo cp pid_test ${MNT}/root/

./umount-img.sh
