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

Running the manual tests
------------------------

`recovery-refresh` and `recovery-persist` executables exist only on systems without
/cache partition. And we need to follow special steps to run tests for them.

- Execute the test on an A/B device first. The test should fail but it will log
  some contents to pmsg.

- Reboot the device immediately and run the test again. The test should save the
  contents of pmsg buffer into /data/misc/recovery/inject.txt. Test will pass if
  this file has expected contents.

`ResourceTest` validates whether the png files are qualified as background text
image under recovery.

    1. `adb sync data` to make sure the test-dir has the images to test.
    2. The test will automatically pickup and verify all `_text.png` files in
       the test dir.
