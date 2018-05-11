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

SystemUpdaterSample app downloads OTA package from `url`. In this sample app
`url` is expected to point to file system, e.g. `file:///data/sample-builds/ota-002.zip`.

If `ab_install_type` is `NON_STREAMING` then app checks if `url` starts
with `file://` and passes `url` to the `update_engine`.

If `ab_install_type` is `STREAMING`, app downloads only the entries in need, as
opposed to the entire package, to initiate a streaming update. The `payload.bin`
entry, which takes up the majority of the space in an OTA package, will be
streamed by `update_engine` directly. The ZIP entries in such a package need to be
saved uncompressed (`ZIP_STORED`), so that their data can be downloaded directly
with the offset and length. As `payload.bin` itself is already in compressed
format, the size penalty is marginal.

Config files can be generated using `tools/gen_update_config.py`.
Running `./tools/gen_update_config.py --help` shows usage of the script.


## Running on a device

The commands expected to be run from `$ANDROID_BUILD_TOP` and for demo
purpose only.

1. Compile the app `$ mmma bootable/recovery/updater_sample`.
2. Install the app to the device using `$ adb install <APK_PATH>`.
3. Change permissions on `/data/ota_package/` to `0777` on the device.
4. Set SELinux mode to permissive. See instructions below.
5. Add update config files.
6. Push OTA packages to the device.


## Sending HTTP headers from UpdateEngine

Sometimes OTA package server might require some HTTP headers to be present,
e.g. `Authorization` header to contain valid auth token. While performing
streaming update, `UpdateEngine` allows passing on certain HTTP headers;
as of writing this sample app, these headers are `Authorization` and `User-Agent`.

`android.os.UpdateEngine#applyPayload` contains information on
which HTTP headers are supported.


## Development

- [x] Create a UI with list of configs, current version,
      control buttons, progress bar and log viewer
- [x] Add `PayloadSpec` and `PayloadSpecs` for working with
      update zip file
- [x] Add `UpdateConfig` for working with json config files
- [x] Add applying non-streaming update
- [x] Prepare streaming update (partially downloading package)
- [x] Add applying streaming update
- [x] Add stop/reset the update
- [x] Add demo for passing HTTP headers to `UpdateEngine#applyPayload`
- [x] [Package compatibility check](https://source.android.com/devices/architecture/vintf/match-rules)
- [ ] Add tests for `MainActivity`
- [ ] Change partition demo
- [ ] Verify system partition checksum for package
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


## Accessing `android.os.UpdateEngine` API

`android.os.UpdateEngine`` APIs are marked as `@SystemApi`, meaning only system apps can access them.


## Getting read/write access to `/data/ota_package/`

Following must be included in `AndroidManifest.xml`:

```xml
    <uses-permission android:name="android.permission.ACCESS_CACHE_FILESYSTEM" />
```

Note: access to cache filesystem is granted only to system apps.


## Setting SELinux mode to permissive (0)

```txt
local$ adb root
local$ adb shell
android# setenforce 0
android# getenforce
```


## License

SystemUpdaterSample app is released under
[Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0).
