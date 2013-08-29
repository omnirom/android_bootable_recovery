#!/system/bin/sh

# If you're implementing this in a custom kernel/firmware,
# I suggest you use a different script name, and add a service
# to launch it from init.rc

# Launches SuperSU in daemon mode only on Android 4.3+.
# Nothing will happen on 4.2.x or older.
# If you want to force loading the daemon, use "--daemon" instead

/system/xbin/daemonsu --auto-daemon &

# Some apps like to run stuff from this script as well, but the
# installer will have done "chattr +i" on this file to prevent
# accidental modification and deletion. In your code, just search 
# this file for "install-recovery-2.sh", and if present, write
# there instead.

/system/etc/install-recovery-2.sh
