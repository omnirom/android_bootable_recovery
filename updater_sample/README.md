# SystemUpdaterSample

This app demonstrates how to use Android system updates APIs to install
[OTA updates](https://source.android.com/devices/tech/ota/). It contains a sample
client for `update_engine` to install A/B (seamless) updates and a sample of
applying non-A/B updates using `recovery`.

A/B (seamless) update is available since Android Nougat (API 24), but this sample
targets the latest android.


## Running on a device

The commands expected to be run from `$ANDROID_BUILD_TOP`.

1. Compile the app `$ mmma bootable/recovery/updater_sample`.
2. Install the app to the device using `$ adb install <APK_PATH>`.
3. Add update config files.


## Update Config file

Directory can be found in logs or on UI. Usually json config files are located in
`/data/user/0/com.example.android.systemupdatersample/files/configs/`. Example file
is located at `res/raw/sample.json`.


## Development

- [x] Create a UI with list of configs, current version,
      control buttons, progress bar and log viewer
- [x] Add `PayloadSpec` and `PayloadSpecs` for working with
      update zip file
- [x] Add `UpdateConfig` for working with json config files
- [x] Add applying non-streaming update
- [ ] Add applying streaming update
- [ ] Prepare streaming update (partially downloading package)
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
