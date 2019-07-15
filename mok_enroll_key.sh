#!/bin/bash

BIN=MokEnrollKey.efi
TARBIN=/boot/efi/EFI/ubuntu/MokEnrollKey.efi
EFIVAR=/sys/firmware/efi/efivars/MokKeyEnroll-e22021f7-3a03-4aea-8b4c-65881a2b8881
DER=/var/lib/shim-signed/mok/MOK.der

if [ "$(id -u)" -ne 0 ]; then
  echo "Need root privilege."
  exit 1
fi

if [ ! -f $BIN ]; then
	echo "Cannot find $BIN"
	exit 1
fi

cp -f $BIN $TARBIN || exit $?

#set uefi variable with Mok.der
if [ -f $EFIVAR ]; then
  echo "find $EFIVAR, remove it"
  chattr -i $EFIVAR || exit $?
  rm -f $EFIVAR 
fi

printf "\x07\x00\x00\x00" > temp.der
cat $DER >> temp.der
cp -f temp.der $EFIVAR || exit $?
rm -f temp.der

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
