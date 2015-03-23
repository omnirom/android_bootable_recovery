The Recovery Image
==================

Quick turn-around testing
-------------------------

    mm -j
    m ramdisk-nodeps
    m recoveryimage-nodeps
    adb reboot bootloader

    # To boot into the new recovery image
    # without flashing the recovery partition:
    fastboot boot $ANDROID_PRODUCT_OUT/recovery.img
