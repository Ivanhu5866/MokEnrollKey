MokEnrollKey
An UEFI application for helping enroll key to MOK without user password

mok_enroll_key.sh
for helping to run MokEnrollKey automatically
1.need to disable secureboot on bios menu, because MokEnrollKey has not been signed.
2.put the script and uefi application together
3.run the script with root privilge
  for example,
  # sudo ./mok_enroll_key.sh /dev/sdb
4.after reboot, you could enable secureboot
