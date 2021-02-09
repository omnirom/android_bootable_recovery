if [ -d "/dev/usb-ffs" ]; then
   echo "/dev/usb-fss directory found"
   mkdir /dev/usb-ffs 0770 shell shell
   mkdir /dev/usb-ffs/adb 0770 shell shell
   mount functionfs adb /dev/usb-ffs/adb uid=2000,gid=2000
   write /sys/class/android_usb/android0/f_ffs/aliases adb
fi
