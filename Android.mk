LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

commands_recovery_local_path := $(LOCAL_PATH)

ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_ARCH),arm)

LOCAL_SRC_FILES := \
	recovery.c \
	bootloader.c \
	commands.c \
	firmware.c \
	install.c \
	roots.c \
	ui.c \
	verifier.c

LOCAL_SRC_FILES += test_roots.c

LOCAL_MODULE := recovery

LOCAL_FORCE_STATIC_EXECUTABLE := true

# This binary is in the recovery ramdisk, which is otherwise a copy of root.
# It gets copied there in config/Makefile.  LOCAL_MODULE_TAGS suppresses
# a (redundant) copy of the binary in /system/bin for user builds.
# TODO: Build the ramdisk image in a more principled way.

LOCAL_MODULE_TAGS := eng

LOCAL_STATIC_LIBRARIES := libminzip libunz libamend libmtdutils libmincrypt
LOCAL_STATIC_LIBRARIES += libminui libpixelflinger_static libcutils
LOCAL_STATIC_LIBRARIES += libstdc++ libc

# Specify a C-includable file containing the OTA public keys.
# This is built in config/Makefile.
# *** THIS IS A TOTAL HACK; EXECUTABLES MUST NOT CHANGE BETWEEN DIFFERENT
#     PRODUCTS/BUILD TYPES. ***
# TODO: make recovery read the keys from an external file.
RECOVERY_INSTALL_OTA_KEYS_INC := \
	$(call intermediates-dir-for,PACKAGING,ota_keys_inc)/keys.inc
# Let install.c say #include "keys.inc"
LOCAL_C_INCLUDES += $(dir $(RECOVERY_INSTALL_OTA_KEYS_INC))

include $(BUILD_EXECUTABLE)

# Depend on the generated keys.inc containing the OTA public keys.
$(intermediates)/install.o: $(RECOVERY_INSTALL_OTA_KEYS_INC)

include $(commands_recovery_local_path)/minui/Android.mk

endif   # TARGET_ARCH == arm
endif	# !TARGET_SIMULATOR

include $(commands_recovery_local_path)/amend/Android.mk
include $(commands_recovery_local_path)/minzip/Android.mk
include $(commands_recovery_local_path)/mtdutils/Android.mk
include $(commands_recovery_local_path)/tools/Android.mk
commands_recovery_local_path :=
