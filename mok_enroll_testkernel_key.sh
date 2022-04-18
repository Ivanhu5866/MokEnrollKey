#!/bin/bash


#
# signed kernel and enroll two keys(DKSM and testkernel) to MOK
#
#
# aug1: kernel which would like to be signed
# aug2: disk (Null defaults to /dev/sda) containing loader for efibootmgr 
#
# ex:
# ./mok_testkernel_key.sh /boot/vmlinuz-4.18.0-25-generic /dev/nvme0n1


BIN=MokEnrollKey.efi
TARBIN=/boot/efi/EFI/ubuntu/MokEnrollKey.efi
EFIVAR_KER=/sys/firmware/efi/efivars/MokKeyTestKer-161a47b3-c116-4942-ae30-cde31ecae242
DER_KER=/var/lib/test_ker/TestKer.der

LOADBIN=UbuntuSecBoot.efi
TARLOADBIN=/boot/efi/EFI/ubuntu/UbuntuSecBoot.efi
EFIVAR=/sys/firmware/efi/efivars/MokKeyEnroll-e22021f7-3a03-4aea-8b4c-65881a2b8881
DER=/var/lib/shim-signed/mok/MOK.der
EFIVAR_SB=/sys/firmware/efi/efivars/MokSBEnable-21e2d0b5-ea3a-4222-85e6-8106ad766df0

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

if [ ! -f $DER ]; then
	echo "Cannot find $DER"
	exit 1
fi

printf "\x07\x00\x00\x00" > temp.der
cat $DER >> temp.der
cp -f temp.der $EFIVAR || exit $?
rm -f temp.der

# check or create test kernel key
if [ ! -f $DER_KER ]; then
	echo "Cannot find $DER_KER, create one."
	mkdir -p /var/lib/test_ker/
	openssl genrsa -out /var/lib/test_ker/TestKer.priv 2048
	openssl req -new -x509 -sha256 -subj '/CN=TestKer-key' -key /var/lib/test_ker/TestKer.priv -out /var/lib/test_ker/TestKer.pem
	openssl x509 -in /var/lib/test_ker/TestKer.pem -inform PEM -out /var/lib/test_ker/TestKer.der -outform DER
fi

# sign kernel
if [ ! -f "$1" ]; then
	echo "No kernel to be signed"
	exit 1
fi
sbsign --key /var/lib/test_ker/TestKer.priv --cert /var/lib/test_ker/TestKer.pem --output ${1}.signed ${1}

# sign kernel
if [ ! -f "$1".signed ]; then
	echo "No kernel has been signed"
	exit 1
fi

mv ${1} ${1}.unsigned
cp ${1}.signed ${1}

#set uefi variable with testker.der
if [ -f $EFIVAR_KER ]; then
  echo "find $EFIVAR_KER, remove it"
  chattr -i $EFIVAR_KER || exit $?
  rm -f $EFIVAR_KER 
fi

printf "\x07\x00\x00\x00" > temp.der
cat $DER_KER >> temp.der
cp -f temp.der $EFIVAR_KER || exit $?
rm -f temp.der

#set uefi variable with sb enable
if [ -f $EFIVAR_SB ]; then
  echo "find $EFIVAR_SB, remove it"
  chattr -i $EFIVAR_SB || exit $?
  rm -f $EFIVAR_SB 
fi

if [ -f $LOADBIN ]; then
	echo "find $LOADBIN, and set MokSBEnable variable"
	
	cp -f $LOADBIN $TARLOADBIN || exit $?

	printf "\x07\x00\x00\x00\x01" > temp.der
	cp -f temp.der $EFIVAR_SB || exit $?
	rm -f temp.der
fi

# Check and delete if mok_enroll_key already
BOOTNUM=`efibootmgr -v | grep 'mok_enroll_key' | cut -d ' ' -f1 | tr -d [BootOOT*]`
if [ "$BOOTNUM" != "" ]; then
	echo "delete existing mok_enroll_key path"
	efibootmgr -B $BOOTNUM -b $BOOTNUM
fi

BOOTORDER=`efibootmgr -v | grep 'BootOrder' | cut -d ' ' -f2`

if [ "${2}" != "" ]; then
	echo "set boot path with device ${2}"
	efibootmgr -c -d ${2} -L mok_enroll_key -l "\EFI\Ubuntu\MokEnrollKey.efi" > /dev/null 2>&1
else
	efibootmgr -c -L mok_enroll_key -l "\EFI\Ubuntu\MokEnrollKey.efi" > /dev/null 2>&1
fi

efibootmgr -o $BOOTORDER

# Check and set bootnext
BOOTNUM=`efibootmgr -v | grep 'mok_enroll_key' | cut -d ' ' -f1 | tr -d [BootOOT*]`

efibootmgr -n $BOOTNUM | grep "BootNext: $BOOTNUM" > /dev/null 2>&1 || exit $?

reboot
