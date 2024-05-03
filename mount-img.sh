#!/bin/sh

. ./script-params

guestmount -a ${IMAGE}.raw -m /dev/sda ${MNT}
