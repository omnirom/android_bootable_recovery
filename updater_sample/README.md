# SystemUpdaterSample

This app demonstrates how to use Android system updates APIs to install
[OTA updates](https://source.android.com/devices/tech/ota/). It contains a sample
client for `update_engine` to install A/B (seamless) updates and a sample of
applying non-A/B updates using `recovery`.

A/B (seamless) update is available since Android Nougat (API 24), but this sample
targets the latest android.


## Workflow

SystemUpdaterSample app shows list of available updates on the UI. User is allowed
to select an update and apply it to the device. App shows installation progress,
logs can be found in `adb logcat`. User can stop or reset an update. Resetting
the update requests update engine to cancel any ongoing update, and revert
if the update has been applied. Stopping does not revert the applied update.


## Update Config file

In this sample updates are defined in JSON update config files.
The structure of a config file is defined in
`com.example.android.systemupdatersample.UpdateConfig`, example file is located
at `res/raw/sample.json`.

In real-life update system the config files expected to be served from a server
to the app, but in this sample, the config files are stored on the device.
The directory can be found in logs or on the UI. In most cases it should be located at
`/data/user/0/com.example.android.systemupdatersample/files/configs/`.

SystemUpdaterSample app downloads OTA package from `url`. If `ab_install_type`
is `NON_STREAMING` then app downloads the whole package and
passes it to the `update_engine`. If `ab_install_type` is `STREAMING`
then app downloads only some files to prepare the streaming update and
`update_engine` will stream only `payload.bin`.
To support streaming A/B (seamless) update, OTA package file must be
an uncompressed (ZIP_STORED) zip file.

Config files can be generated using `tools/gen_update_config.py`.
Running `./tools/gen_update_config.py --help` shows usage of the script.


## Running on a device

The commands expected to be run from `$ANDROID_BUILD_TOP`.

1. Compile the app `$ mmma bootable/recovery/updater_sample`.
2. Install the app to the device using `$ adb install <APK_PATH>`.
3. Add update config files.


## Development

- [x] Create a UI with list of configs, current version,
      control buttons, progress bar and log viewer
- [x] Add `PayloadSpec` and `PayloadSpecs` for working with
      update zip file
- [x] Add `UpdateConfig` for working with json config files
- [x] Add applying non-streaming update
- [ ] Prepare streaming update (partially downloading package)
- [ ] Add applying streaming update
- [ ] Add tests for `MainActivity`
- [ ] Add stop/reset the update
- [ ] Verify system partition checksum for package
- [ ] HAL compatibility check
- [ ] Change partition demo
- [ ] Add non-A/B updates demo


## Running tests

1. Build `$ mmma bootable/recovery/updater_sample/`
2. Install app
   `$ adb install $OUT/system/app/SystemUpdaterSample/SystemUpdaterSample.apk`
3. Install tests
   `$ adb install $OUT/testcases/SystemUpdaterSampleTests/SystemUpdaterSampleTests.apk`
4. Run tests
   `$ adb shell am instrument -w com.example.android.systemupdatersample.tests/android.support.test.runner.AndroidJUnitRunner`
5. Run a test file
   ```
   $ adb shell am instrument \
     -w com.example.android.systemupdatersample.tests/android.support.test.runner.AndroidJUnitRunner \
     -c com.example.android.systemupdatersample.util.PayloadSpecsTest
   ```


## Getting access to `update_engine` API and read/write access to `/data`

Run adb shell as a root, and set SELinux mode to permissive (0):

```txt
$ adb root
$ adb shell
# setenforce 0
# getenforce
```
