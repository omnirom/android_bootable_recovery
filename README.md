The Recovery Image
==================

Quick turn-around testing
-------------------------

* Devices using recovery-as-boot (e.g. Pixels, which set BOARD\_USES\_RECOVERY\_AS\_BOOT)

      # After setting up environment and lunch.
      m -j bootimage
      adb reboot bootloader

      # Pixel devices don't support booting into recovery mode with `fastboot boot`.
      fastboot flash boot

      # Manually choose `Recovery mode` from bootloader menu.

* Devices with a separate recovery image (e.g. Nexus)

      # After setting up environment and lunch.
      mm -j && m ramdisk-nodeps && m recoveryimage-nodeps
      adb reboot bootloader

      # To boot into the new recovery image without flashing the recovery partition:
      fastboot boot $ANDROID_PRODUCT_OUT/recovery.img

Running the tests
-----------------

    # After setting up environment and lunch.
    mmma -j bootable/recovery

    # Running the tests on device (under normal boot).
    adb root
    adb sync data

    # 32-bit device
    adb shell /data/nativetest/recovery_unit_test/recovery_unit_test

    # Or 64-bit device
    adb shell /data/nativetest64/recovery_unit_test/recovery_unit_test

Running the manual tests
------------------------

`recovery-refresh` and `recovery-persist` executables exist only on systems without
/cache partition. And we need to follow special steps to run tests for them.

- Execute the test on an A/B device first. The test should fail but it will log
  some contents to pmsg.

- Reboot the device immediately and run the test again. The test should save the
  contents of pmsg buffer into /data/misc/recovery/inject.txt. Test will pass if
  this file has expected contents.

Using `adb` under recovery
--------------------------

When running recovery image from debuggable builds (i.e. `-eng` or `-userdebug` build variants, or
`ro.debuggable=1` in `/prop.default`), `adbd` service is enabled and started by default, which
allows `adb` communication. A device should be listed under `adb devices`, either in `recovery` or
`sideload` state.

    $ adb devices
    List of devices attached
    1234567890abcdef    recovery

Although `/system/bin/adbd` is built from the same code base as the one in the normal boot, only a
subset of `adb` commands are meaningful under recovery, such as `adb root`, `adb shell`, `adb push`,
`adb pull` etc. Since Android Q, `adb shell` no longer requires manually mounting `/system` from
recovery menu.

## Troubleshooting

### `adb devices` doesn't show the device.

    $ adb devices
    List of devices attached

 * Ensure `adbd` is built and running.

By default, `adbd` is always included into recovery image, as `/system/bin/adbd`. `init` starts
`adbd` service automatically only in debuggable builds. This behavior is controlled by the recovery
specific `/init.rc`, whose source code is at `bootable/recovery/etc/init.rc`.

The best way to confirm a running `adbd` is by checking the serial output, which shows a service
start log as below.

    [   18.961986] c1      1 init: starting service 'adbd'...

 * Ensure USB gadget has been enabled.

If `adbd` service has been started but device not shown under `adb devices`, use `lsusb(8)` (on
host) to check if the device is visible to the host.

`bootable/recovery/etc/init.rc` disables Android USB gadget (via sysfs) as part of the `fs` action
trigger, and will only re-enable it in debuggable builds (the `on property` rule will always run
_after_ `on fs`).

    on fs
        write /sys/class/android_usb/android0/enable 0

    # Always start adbd on userdebug and eng builds
    on property:ro.debuggable=1
        write /sys/class/android_usb/android0/enable 1
        start adbd

If device is using [configfs](https://www.kernel.org/doc/Documentation/usb/gadget_configfs.txt),
check if configfs has been properly set up in init rc scripts. See the [example
configuration](https://android.googlesource.com/device/google/wahoo/+/master/init.recovery.hardware.rc)
for Pixel 2 devices. Note that the flag set via sysfs (i.e. the one above) is no-op when using
configfs.

### `adb devices` shows the device, but in `unauthorized` state.

    $ adb devices
    List of devices attached
    1234567890abcdef    unauthorized

recovery image doesn't honor the USB debugging toggle and the authorizations added under normal boot
(because such authorization data stays in /data, which recovery doesn't mount), nor does it support
authorizing a host device under recovery. We can use one of the following options instead.

 * **Option 1 (Recommended):** Authorize a host device with adb vendor keys.

For debuggable builds, an RSA keypair can be used to authorize a host device that has the private
key. The public key, defined via `PRODUCT_ADB_KEYS`, will be copied to `/adb_keys`. When starting
the host-side `adbd`, make sure the filename (or the directory) of the matching private key has been
added to `$ADB_VENDOR_KEYS`.

    $ export ADB_VENDOR_KEYS=/path/to/adb/private/key
    $ adb kill-server
    $ adb devices

`-user` builds filter out `PRODUCT_ADB_KEYS`, so no `/adb_keys` will be included there.

Note that this mechanism applies to both of normal boot and recovery modes.

 * **Option 2:** Allow `adbd` to connect without authentication.
   * `adbd` is compiled with `ALLOW_ADBD_NO_AUTH` (only on debuggable builds).
   * `ro.adb.secure` has a value of `0`.

Both of the two conditions need to be satisfied. Although `ro.adb.secure` is a runtime property, its
value is set at build time (written into `/prop.default`). It defaults to `1` on `-user` builds, and
`0` for other build variants. The value is overridable via `PRODUCT_DEFAULT_PROPERTY_OVERRIDES`.
