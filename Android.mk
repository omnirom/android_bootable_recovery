# Copyright (C) 2007 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

TARGET_RECOVERY_GUI := true

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    recovery.cpp \
    twbootloader.cpp \
    install.cpp \
    roots.cpp \
    ui.cpp \
    screen_ui.cpp \
    verifier.cpp \
    fixPermissions.cpp \
    adb_install.cpp

LOCAL_SRC_FILES += \
    data.cpp \
    makelist.cpp \
    partition.cpp \
    partitionmanager.cpp \
    mtdutils/mtdutils.c \
    twinstall.cpp \
    twrp-functions.cpp \
    openrecoveryscript.cpp

ifneq ($(TARGET_RECOVERY_REBOOT_SRC),)
  LOCAL_SRC_FILES += $(TARGET_RECOVERY_REBOOT_SRC)
endif

LOCAL_MODULE := recovery

#LOCAL_FORCE_STATIC_EXECUTABLE := true

RECOVERY_API_VERSION := 3
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)

#LOCAL_STATIC_LIBRARIES := \
#    libext4_utils \
#    libminzip \
#    libmtdutils \
#    libmincrypt \
#    libminadbd \
#    libpixelflinger_static

LOCAL_C_INCLUDES += bionic external/stlport/stlport

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

LOCAL_STATIC_LIBRARIES += libmtdutils
LOCAL_STATIC_LIBRARIES += libminadbd libminzip libunz
LOCAL_STATIC_LIBRARIES += libminuitwrp libpixelflinger_static libpng libjpegtwrp libgui
LOCAL_SHARED_LIBRARIES += libz libc libstlport libcutils libstdc++ libmincrypt libext4_utils

ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    LOCAL_CFLAGS += -DUSE_EXT4
    LOCAL_C_INCLUDES += system/extras/ext4_utils
    #LOCAL_STATIC_LIBRARIES += libext4_utils
endif

ifeq ($(HAVE_SELINUX), true)
  LOCAL_C_INCLUDES += external/libselinux/include
  LOCAL_STATIC_LIBRARIES += libselinux
  LOCAL_CFLAGS += -DHAVE_SELINUX
endif # HAVE_SELINUX

# This binary is in the recovery ramdisk, which is otherwise a copy of root.
# It gets copied there in config/Makefile.  LOCAL_MODULE_TAGS suppresses
# a (redundant) copy of the binary in /system/bin for user builds.
# TODO: Build the ramdisk image in a more principled way.
LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_RECOVERY_UI_LIB),)
  LOCAL_SRC_FILES += default_device.cpp
else
  LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_UI_LIB)
endif

LOCAL_C_INCLUDES += system/extras/ext4_utils

#TWRP Build Flags
ifeq ($(BOARD_HAS_NO_REAL_SDCARD), true)
    LOCAL_CFLAGS += -DBOARD_HAS_NO_REAL_SDCARD
endif
ifneq ($(SP1_NAME),)
	LOCAL_CFLAGS += -DSP1_NAME=$(SP1_NAME) -DSP1_BACKUP_METHOD=$(SP1_BACKUP_METHOD) -DSP1_MOUNTABLE=$(SP1_MOUNTABLE)
endif
ifneq ($(SP1_DISPLAY_NAME),)
	LOCAL_CFLAGS += -DSP1_DISPLAY_NAME=$(SP1_DISPLAY_NAME)
endif
ifneq ($(SP2_NAME),)
	LOCAL_CFLAGS += -DSP2_NAME=$(SP2_NAME) -DSP2_BACKUP_METHOD=$(SP2_BACKUP_METHOD) -DSP2_MOUNTABLE=$(SP2_MOUNTABLE)
endif
ifneq ($(SP2_DISPLAY_NAME),)
	LOCAL_CFLAGS += -DSP2_DISPLAY_NAME=$(SP2_DISPLAY_NAME)
endif
ifneq ($(SP3_NAME),)
	LOCAL_CFLAGS += -DSP3_NAME=$(SP3_NAME) -DSP3_BACKUP_METHOD=$(SP3_BACKUP_METHOD) -DSP3_MOUNTABLE=$(SP3_MOUNTABLE)
endif
ifneq ($(SP3_DISPLAY_NAME),)
	LOCAL_CFLAGS += -DSP3_DISPLAY_NAME=$(SP3_DISPLAY_NAME)
endif
ifneq ($(RECOVERY_SDCARD_ON_DATA),)
	LOCAL_CFLAGS += -DRECOVERY_SDCARD_ON_DATA
endif
ifneq ($(TW_INCLUDE_DUMLOCK),)
	LOCAL_CFLAGS += -DTW_INCLUDE_DUMLOCK
endif
ifneq ($(TW_INTERNAL_STORAGE_PATH),)
	LOCAL_CFLAGS += -DTW_INTERNAL_STORAGE_PATH=$(TW_INTERNAL_STORAGE_PATH)
endif
ifneq ($(TW_INTERNAL_STORAGE_MOUNT_POINT),)
	LOCAL_CFLAGS += -DTW_INTERNAL_STORAGE_MOUNT_POINT=$(TW_INTERNAL_STORAGE_MOUNT_POINT)
endif
ifneq ($(TW_EXTERNAL_STORAGE_PATH),)
	LOCAL_CFLAGS += -DTW_EXTERNAL_STORAGE_PATH=$(TW_EXTERNAL_STORAGE_PATH)
endif
ifneq ($(TW_EXTERNAL_STORAGE_MOUNT_POINT),)
	LOCAL_CFLAGS += -DTW_EXTERNAL_STORAGE_MOUNT_POINT=$(TW_EXTERNAL_STORAGE_MOUNT_POINT)
endif
ifeq ($(TW_HAS_NO_RECOVERY_PARTITION), true)
    LOCAL_CFLAGS += -DTW_HAS_NO_RECOVERY_PARTITION
endif
ifeq ($(TW_NO_REBOOT_BOOTLOADER), true)
    LOCAL_CFLAGS += -DTW_NO_REBOOT_BOOTLOADER
endif
ifeq ($(TW_NO_REBOOT_RECOVERY), true)
    LOCAL_CFLAGS += -DTW_NO_REBOOT_RECOVERY
endif
ifeq ($(TW_NO_BATT_PERCENT), true)
    LOCAL_CFLAGS += -DTW_NO_BATT_PERCENT
endif
ifneq ($(TW_CUSTOM_POWER_BUTTON),)
	LOCAL_CFLAGS += -DTW_CUSTOM_POWER_BUTTON=$(TW_CUSTOM_POWER_BUTTON)
endif
ifeq ($(TW_ALWAYS_RMRF), true)
    LOCAL_CFLAGS += -DTW_ALWAYS_RMRF
endif
ifeq ($(TW_NEVER_UNMOUNT_SYSTEM), true)
    LOCAL_CFLAGS += -DTW_NEVER_UNMOUNT_SYSTEM
endif
ifeq ($(TW_NO_USB_STORAGE), true)
    LOCAL_CFLAGS += -DTW_NO_USB_STORAGE
endif
ifeq ($(TW_INCLUDE_INJECTTWRP), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_INJECTTWRP
endif
ifeq ($(TW_INCLUDE_BLOBPACK), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_BLOBPACK
endif
ifeq ($(TW_DEFAULT_EXTERNAL_STORAGE), true)
    LOCAL_CFLAGS += -DTW_DEFAULT_EXTERNAL_STORAGE
endif
ifneq ($(TARGET_USE_CUSTOM_LUN_FILE_PATH),)
    LOCAL_CFLAGS += -DCUSTOM_LUN_FILE=\"$(TARGET_USE_CUSTOM_LUN_FILE_PATH)\"
endif
ifneq ($(BOARD_UMS_LUNFILE),)
    LOCAL_CFLAGS += -DCUSTOM_LUN_FILE=\"$(BOARD_UMS_LUNFILE)\"
endif
#ifeq ($(TW_FLASH_FROM_STORAGE), true) Making this the default behavior
    LOCAL_CFLAGS += -DTW_FLASH_FROM_STORAGE
#endif
ifeq ($(TW_HAS_DOWNLOAD_MODE), true)
    LOCAL_CFLAGS += -DTW_HAS_DOWNLOAD_MODE
endif
ifeq ($(TW_SDEXT_NO_EXT4), true)
    LOCAL_CFLAGS += -DTW_SDEXT_NO_EXT4
endif
ifeq ($(TW_FORCE_CPUINFO_FOR_DEVICE_ID), true)
    LOCAL_CFLAGS += -DTW_FORCE_CPUINFO_FOR_DEVICE_ID
endif
ifeq ($(TW_INCLUDE_CRYPTO), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO
    LOCAL_CFLAGS += -DCRYPTO_FS_TYPE=\"$(TW_CRYPTO_FS_TYPE)\"
    LOCAL_CFLAGS += -DCRYPTO_REAL_BLKDEV=\"$(TW_CRYPTO_REAL_BLKDEV)\"
    LOCAL_CFLAGS += -DCRYPTO_MNT_POINT=\"$(TW_CRYPTO_MNT_POINT)\"
    LOCAL_CFLAGS += -DCRYPTO_FS_OPTIONS=\"$(TW_CRYPTO_FS_OPTIONS)\"
    LOCAL_CFLAGS += -DCRYPTO_FS_FLAGS=\"$(TW_CRYPTO_FS_FLAGS)\"
    LOCAL_CFLAGS += -DCRYPTO_KEY_LOC=\"$(TW_CRYPTO_KEY_LOC)\"
    LOCAL_SHARED_LIBRARIES += libcrypto
    LOCAL_SRC_FILES += crypto/ics/cryptfs.c
    LOCAL_C_INCLUDES += system/extras/ext4_utils external/openssl/include
endif
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO
    LOCAL_CFLAGS += -DTW_INCLUDE_JB_CRYPTO
    LOCAL_SHARED_LIBRARIES += libcrypto
    LOCAL_STATIC_LIBRARIES += libfs_mgrtwrp
    LOCAL_SRC_FILES += crypto/jb/cryptfs.c
    LOCAL_C_INCLUDES += system/extras/ext4_utils external/openssl/include
endif

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
# Create busybox symlinks... gzip and gunzip are excluded because those need to link to pigz instead
BUSYBOX_LINKS := $(shell cat external/busybox/busybox-full.links)
exclude := tune2fs mke2fs mkdosfs gzip gunzip
RECOVERY_BUSYBOX_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(filter-out $(exclude),$(notdir $(BUSYBOX_LINKS))))
$(RECOVERY_BUSYBOX_SYMLINKS): BUSYBOX_BINARY := busybox
$(RECOVERY_BUSYBOX_SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@

ALL_DEFAULT_INSTALLED_MODULES += $(RECOVERY_BUSYBOX_SYMLINKS)

include $(CLEAR_VARS)
LOCAL_MODULE := verifier_test
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := \
    verifier_test.cpp \
    verifier.cpp \
    ui.cpp
LOCAL_STATIC_LIBRARIES := \
    libmincrypt \
    libminui \
    libcutils \
    libstdc++ \
    libc
include $(BUILD_EXECUTABLE)

commands_recovery_local_path := $(LOCAL_PATH)
include $(LOCAL_PATH)/minui/Android.mk \
    $(LOCAL_PATH)/minelf/Android.mk \
    $(LOCAL_PATH)/minzip/Android.mk \
    $(LOCAL_PATH)/minadbd/Android.mk \
    $(LOCAL_PATH)/tools/Android.mk \
    $(LOCAL_PATH)/edify/Android.mk \
    $(LOCAL_PATH)/updater/Android.mk \
    $(LOCAL_PATH)/applypatch/Android.mk

#includes for TWRP
include $(commands_recovery_local_path)/libjpegtwrp/Android.mk \
    $(commands_recovery_local_path)/injecttwrp/Android.mk \
    $(commands_recovery_local_path)/htcdumlock/Android.mk \
    $(commands_recovery_local_path)/minuitwrp/Android.mk \
    $(commands_recovery_local_path)/gui/Android.mk \
    $(commands_recovery_local_path)/mmcutils/Android.mk \
    $(commands_recovery_local_path)/bmlutils/Android.mk \
    $(commands_recovery_local_path)/flashutils/Android.mk \
    $(commands_recovery_local_path)/prebuilt/Android.mk \
    $(commands_recovery_local_path)/mtdutils/Android.mk \
    $(commands_recovery_local_path)/pigz/Android.mk \
    $(commands_recovery_local_path)/crypto/cryptsettings/Android.mk \
    $(commands_recovery_local_path)/libcrecovery/Android.mk \
    $(commands_recovery_local_path)/twmincrypt/Android.mk


ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
    include $(commands_recovery_local_path)/crypto/fs_mgr/Android.mk
endif

commands_recovery_local_path :=

