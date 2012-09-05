#!/sbin/sh

um
if [ $? != "0" ]; then
	echo "Unable to unmount!"
	exit 1
fi


case $1 in
    repair)
	if [ -e /dev/block/mmcblk0p2 ]; then

		e2fsck -yf /dev/block/mmcblk0p2
	else
		echo "No ext partition found!"
		exit 1
	fi
	exit 0
        ;;
    ext3)
	if [ -e /dev/block/mmcblk0p2 ]; then

		e2fsck -yf /dev/block/mmcblk0p2
		tune2fs -c0 -i0 -j /dev/block/mmcblk0p2
	else
		echo "No ext partition found!"
		exit 1
	fi
	exit 0
	;;
    ext4)
	if [ -e /dev/block/mmcblk0p2 ]; then

		tune2fs -O extents,uninit_bg,dir_index /dev/block/mmcblk0p2
		e2fsck -fpDC0 /dev/block/mmcblk0p2
	else
		echo "No ext partition found!"
		exit 1
	fi
	exit 0
        ;;
    --)
	exit 0
        ;;
esac
