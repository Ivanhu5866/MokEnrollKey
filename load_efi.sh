#!/bin/bash

BIN=loadefi.efi
LOADBIN=UbuntuSecBoot.efi
TARBIN=/boot/efi/EFI/ubuntu/loadefi.efi
TARLOADBIN=/boot/efi/EFI/ubuntu/UbuntuSecBoot.efi

if [ "$(id -u)" -ne 0 ]; then
  echo "Need root privilege."
  exit 1
fi

if [ ! -f $BIN ]; then
	echo "Cannot find $BIN"
	exit 1
fi

cp -f $BIN $TARBIN || exit $?

if [ ! -f $LOADBIN ]; then
	echo "Cannot find $LOADBIN"
	exit 1
fi

cp -f $LOADBIN $TARLOADBIN || exit $?

# Check and delete if load_efi already
BOOTNUM=`efibootmgr -v | grep 'load_efi' | cut -d ' ' -f1 | tr -d [BootOOT*]`
if [ "$BOOTNUM" != "" ]; then
	echo "delete existing load_efi path"
	efibootmgr -B $BOOTNUM -b $BOOTNUM
fi

BOOTORDER=`efibootmgr -v | grep 'BootOrder' | cut -d ' ' -f2`

if [ "${1}" != "" ]; then
	echo "set boot path with device ${1}"
	efibootmgr -c -d ${1} -L load_efi -l "\EFI\Ubuntu\loadefi.efi" > /dev/null 2>&1
else
	efibootmgr -c -L load_efi -l "\EFI\Ubuntu\loadefi.efi" > /dev/null 2>&1
fi

efibootmgr -o $BOOTORDER

# Check and set bootnext
BOOTNUM=`efibootmgr -v | grep 'load_efi' | cut -d ' ' -f1 | tr -d [BootOOT*]`

efibootmgr -n $BOOTNUM | grep "BootNext: $BOOTNUM" > /dev/null 2>&1 || exit $?


reboot
