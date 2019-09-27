minadbd
=======

`minadbd` is analogous to the regular `adbd`, but providing the minimal services to support
recovery-specific use cases. Generally speaking, `adbd` = `libadbd` + `libadbd_services`, whereas
`minadbd` = `libadbd` + `libminadbd_services`.

Although both modules may be installed into the recovery image, only one of them, or none, can be
active at any given time.

- The start / stop of `adbd` is managed via system property `sys.usb.config`, when setting to `adb`
  or `none` respectively. Upon starting recovery mode, `adbd` is started in debuggable builds by
  default; otherwise `adbd` will stay off at all times in user builds. See the triggers in
  `bootable/recovery/etc/init.rc`.

- `minadbd` is started by `recovery` as needed.
  - When requested to start `minadbd`, `recovery` stops `adbd` first, if it's running; it then forks
    and execs `minadbd` in a separate process.
  - `minadbd` talks to host-side `adb` server to get user requests.
    - `minadbd` handles some requests directly, e.g. querying device properties for rescue service.
    - `minadbd` communicates with `recovery` to fulfill requests regarding package installation. See
      the comments in `bootable/recovery/install/adb_install.cpp` for the IPC protocol between
      `recovery` and `minadbd`.
  - Upon exiting `minadbd`, `recovery` restarts `adbd` if it was previously running.
