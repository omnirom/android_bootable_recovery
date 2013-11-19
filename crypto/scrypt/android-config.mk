#
# These flags represent the build-time configuration of scrypt for Android
#
# The value of $(scrypt_cflags) was pruned from the Makefile generated
# by running ./configure from import_scrypt.sh.
#
# This script performs minor but required patching for the Android build.
#

LOCAL_CFLAGS += $(scrypt_cflags)

# Add in flags to let config.h be read properly
LOCAL_CFLAGS += "-DHAVE_CONFIG_H"

# Add clang here when it works on host
# LOCAL_CLANG := true
