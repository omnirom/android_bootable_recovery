The Recovery Image
==================

Quick turn-around testing
-------------------------

    mm -j && m ramdisk-nodeps && m recoveryimage-nodeps

    # To boot into the new recovery image
    # without flashing the recovery partition:
    adb reboot bootloader
    fastboot boot $ANDROID_PRODUCT_OUT/recovery.img
