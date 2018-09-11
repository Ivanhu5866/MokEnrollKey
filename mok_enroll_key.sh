#!/bin/bash

BIN=MokEnrollKey.efi
TARBIN=/boot/efi/EFI/ubuntu/MokEnrollKey.efi

if [ "$(id -u)" -ne 0 ]; then
  echo "Need root privilege."
  exit 1
fi

if [ ! -f $BIN ]; then
	echo "Cannot find $BIN"
	exit 1
fi

cp -f $BIN $TARBIN || exit $?


#set uefi variable with Mok.der with uefiop application 
git clone https://github.com/Ivanhu5866/uefiop > /dev/null 2>&1
cd uefiop
make
cd ./bin
./uefivarset -g e22021f7-3a03-4aea-8b4c-65881a2b8881 -n MokKeyEnroll -f /var/lib/shim-signed/mok/MOK.der > /dev/null 2>&1
cd ../../
rm -rf uefiop

# Check and delete if mok_enroll_key already
BOOTNUM=`efibootmgr -v | grep 'mok_enroll_key' | cut -d ' ' -f1 | tr -d [BootOOT*]`
if [ "$BOOTNUM" != "" ]; then
	echo "delete existing mok_enroll_key path"
	efibootmgr -B $BOOTNUM -b $BOOTNUM
fi

BOOTORDER=`efibootmgr -v | grep 'BootOrder' | cut -d ' ' -f2`

if [ "${1}" != "" ]; then
	echo "set boot path with device ${1}"
	efibootmgr -c -d ${1} -L mok_enroll_key -l "\EFI\Ubuntu\MokEnrollKey.efi" > /dev/null 2>&1
else
	efibootmgr -c -L mok_enroll_key -l "\EFI\Ubuntu\MokEnrollKey.efi" > /dev/null 2>&1
fi

efibootmgr -o $BOOTORDER

# Check and set bootnext
BOOTNUM=`efibootmgr -v | grep 'mok_enroll_key' | cut -d ' ' -f1 | tr -d [BootOOT*]`

efibootmgr -n $BOOTNUM | grep "BootNext: $BOOTNUM" > /dev/null 2>&1 || exit $?


reboot
