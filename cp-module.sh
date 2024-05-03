#!/bin/sh

. ./script-params

./mount-img.sh

cp ${MODULE} ${MNT}/root/
cp pid_test ${MNT}/root/

./umount-img.sh
