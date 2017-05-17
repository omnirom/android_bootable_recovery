The contents of this directory are copied from system/core/adb, with
the following changes:

adb.c
  - much support for host mode and non-linux OS's stripped out; this
    version only runs as adbd on the device.
  - always setuid/setgid's itself to the shell user
  - only uses USB transport
  - references to JDWP removed
  - main() removed
  - all ADB_HOST and win32 code removed
  - removed listeners, logging code, background server (for host)

adb.h
  - minor changes to match adb.c changes

sockets.c
  - references to JDWP removed
  - ADB_HOST code removed

services.c
  - all services except echo_service (which is commented out) removed
  - all host mode support removed
  - sideload_service() added; this is the only service supported.  It
    receives a single blob of data, writes it to a fixed filename, and
    makes the process exit.

Android.mk
  - only builds in adbd mode; builds as static library instead of a
    standalone executable.

sysdeps.h
  - changes adb_creat() to use O_NOFOLLOW

transport.c
  - removed ADB_HOST code

transport_usb.c
  - removed ADB_HOST code
