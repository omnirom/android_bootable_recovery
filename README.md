The Recovery Image
==================

Quick turn-around testing
-------------------------

    mm -j && m ramdisk-nodeps && m recoveryimage-nodeps

    # To boot into the new recovery image
    # without flashing the recovery partition:
    adb reboot bootloader
    fastboot boot $ANDROID_PRODUCT_OUT/recovery.img

Running the tests
-----------------
    # After setting up environment and lunch.
    mmma -j bootable/recovery

    # Running the tests on device.
    adb root
    adb sync data

    # 32-bit device
    adb shell /data/nativetest/recovery_unit_test/recovery_unit_test
    adb shell /data/nativetest/recovery_component_test/recovery_component_test

    # Or 64-bit device
    adb shell /data/nativetest64/recovery_unit_test/recovery_unit_test
    adb shell /data/nativetest64/recovery_component_test/recovery_component_test
