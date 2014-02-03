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

LOCAL_SRC_FILES := \
    twrp.cpp \
    fixPermissions.cpp \
    twrpTar.cpp \
	twrpDU.cpp \
    twrpDigest.cpp \

LOCAL_SRC_FILES += \
    data.cpp \
    partition.cpp \
    partitionmanager.cpp \
    twinstall.cpp \
    twrp-functions.cpp \
    openrecoveryscript.cpp \
    tarWrite.c

ifneq ($(TARGET_RECOVERY_REBOOT_SRC),)
  LOCAL_SRC_FILES += $(TARGET_RECOVERY_REBOOT_SRC)
endif

LOCAL_MODULE := recovery

#LOCAL_FORCE_STATIC_EXECUTABLE := true

RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)

#LOCAL_STATIC_LIBRARIES := \
#    libext4_utils_static \
#    libsparse_static \
#    libminzip \
#    libz \
#    libmtdutils \
#    libmincrypt \
#    libminadbd \
#    libminui \
#    libpixelflinger_static \
#    libpng \
#    libfs_mgr \
#    libcutils \
#    liblog \
#    libselinux \
#    libstdc++ \
#    libm \
#    libc

LOCAL_C_INCLUDES += bionic external/stlport/stlport

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

LOCAL_STATIC_LIBRARIES += libcrecovery libguitwrp
LOCAL_SHARED_LIBRARIES += libz libc libstlport libcutils libstdc++ libtar libblkid libminuitwrp libminadbd libmtdutils libminzip libaosprecovery

ifneq ($(wildcard system/core/libsparse/Android.mk),)
LOCAL_SHARED_LIBRARIES += libsparse
endif

ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    LOCAL_CFLAGS += -DUSE_EXT4
    LOCAL_C_INCLUDES += system/extras/ext4_utils
    LOCAL_SHARED_LIBRARIES += libext4_utils
endif
ifneq ($(wildcard external/libselinux/Android.mk),)
    TWHAVE_SELINUX := true
endif
ifeq ($(TWHAVE_SELINUX), true)
  #LOCAL_C_INCLUDES += external/libselinux/include
  #LOCAL_STATIC_LIBRARIES += libselinux
  #LOCAL_CFLAGS += -DHAVE_SELINUX -g
endif # HAVE_SELINUX
ifeq ($(TWHAVE_SELINUX), true)
    LOCAL_C_INCLUDES += external/libselinux/include
    LOCAL_SHARED_LIBRARIES += libselinux
    LOCAL_CFLAGS += -DHAVE_SELINUX -g
    ifneq ($(TARGET_USERIMAGES_USE_EXT4), true)
        LOCAL_CFLAGS += -DUSE_EXT4
        LOCAL_C_INCLUDES += system/extras/ext4_utils
        LOCAL_SHARED_LIBRARIES += libext4_utils
    endif
endif

# This binary is in the recovery ramdisk, which is otherwise a copy of root.
# It gets copied there in config/Makefile.  LOCAL_MODULE_TAGS suppresses
# a (redundant) copy of the binary in /system/bin for user builds.
# TODO: Build the ramdisk image in a more principled way.
LOCAL_MODULE_TAGS := eng

#ifeq ($(TARGET_RECOVERY_UI_LIB),)
  LOCAL_SRC_FILES += default_device.cpp
#else
#  LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_UI_LIB)
#endif

LOCAL_C_INCLUDES += system/extras/ext4_utils

#TWRP Build Flags
ifneq ($(TW_NO_SCREEN_TIMEOUT),)
    LOCAL_CFLAGS += -DTW_NO_SCREEN_TIMEOUT
endif
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
ifeq ($(TW_HAS_NO_BOOT_PARTITION), true)
    LOCAL_CFLAGS += -DTW_HAS_NO_BOOT_PARTITION
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
ifeq ($(TW_NO_SCREEN_BLANK), true)
    LOCAL_CFLAGS += -DTW_NO_SCREEN_BLANK
endif
ifeq ($(TW_SDEXT_NO_EXT4), true)
    LOCAL_CFLAGS += -DTW_SDEXT_NO_EXT4
endif
ifeq ($(TW_FORCE_CPUINFO_FOR_DEVICE_ID), true)
    LOCAL_CFLAGS += -DTW_FORCE_CPUINFO_FOR_DEVICE_ID
endif
ifeq ($(TW_NO_EXFAT_FUSE), true)
    LOCAL_CFLAGS += -DTW_NO_EXFAT_FUSE
endif
ifeq ($(TW_INCLUDE_CRYPTO), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO
    LOCAL_CFLAGS += -DCRYPTO_FS_TYPE=\"$(TW_CRYPTO_FS_TYPE)\"
    LOCAL_CFLAGS += -DCRYPTO_REAL_BLKDEV=\"$(TW_CRYPTO_REAL_BLKDEV)\"
    LOCAL_CFLAGS += -DCRYPTO_MNT_POINT=\"$(TW_CRYPTO_MNT_POINT)\"
    LOCAL_CFLAGS += -DCRYPTO_FS_OPTIONS=\"$(TW_CRYPTO_FS_OPTIONS)\"
    LOCAL_CFLAGS += -DCRYPTO_FS_FLAGS=\"$(TW_CRYPTO_FS_FLAGS)\"
    LOCAL_CFLAGS += -DCRYPTO_KEY_LOC=\"$(TW_CRYPTO_KEY_LOC)\"
ifeq ($(TW_INCLUDE_CRYPTO_SAMSUNG), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO_SAMSUNG=\"$(TW_INCLUDE_CRYPTO_SAMSUNG)\"
    ifdef TW_CRYPTO_SD_REAL_BLKDEV
        LOCAL_CFLAGS += -DCRYPTO_SD_REAL_BLKDEV=\"$(TW_CRYPTO_SD_REAL_BLKDEV)\"
        LOCAL_CFLAGS += -DCRYPTO_SD_FS_TYPE=\"$(TW_CRYPTO_SD_FS_TYPE)\"
    endif
    #LOCAL_LDFLAGS += -L$(TARGET_OUT_SHARED_LIBRARIES) -lsec_km
    LOCAL_LDFLAGS += -ldl
    LOCAL_STATIC_LIBRARIES += libcrypt_samsung
endif
    LOCAL_SHARED_LIBRARIES += libcryptfsics
    #LOCAL_SRC_FILES += crypto/ics/cryptfs.c
    #LOCAL_C_INCLUDES += system/extras/ext4_utils external/openssl/include
endif
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO
    LOCAL_CFLAGS += -DTW_INCLUDE_JB_CRYPTO
    LOCAL_SHARED_LIBRARIES += libcryptfsjb
    #LOCAL_SRC_FILES += crypto/jb/cryptfs.c
    #LOCAL_C_INCLUDES += system/extras/ext4_utils external/openssl/include
endif
ifeq ($(TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID), true)
    LOCAL_CFLAGS += -DTW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID
endif
ifneq ($(TW_BRIGHTNESS_PATH),)
	LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=$(TW_BRIGHTNESS_PATH)
endif
ifneq ($(TW_MAX_BRIGHTNESS),)
	LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=$(TW_MAX_BRIGHTNESS)
endif
ifneq ($(TW_CUSTOM_BATTERY_PATH),)
	LOCAL_CFLAGS += -DTW_CUSTOM_BATTERY_PATH=$(TW_CUSTOM_BATTERY_PATH)
endif
ifeq ($(TARGET_BOARD_PLATFORM),rk30xx)
    LOCAL_CFLAGS += -DRK3066
endif
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_SHARED_LIBRARIES += libopenaes
else
    LOCAL_CFLAGS += -DTW_EXCLUDE_ENCRYPTED_BACKUPS
endif
ifeq ($(TARGET_RECOVERY_QCOM_RTC_FIX),)
  ifeq ($(TARGET_CPU_VARIANT),krait)
    LOCAL_CFLAGS += -DQCOM_RTC_FIX
  endif
else ifeq ($(TARGET_RECOVERY_QCOM_RTC_FIX),true)
    LOCAL_CFLAGS += -DQCOM_RTC_FIX
endif

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
# Create busybox symlinks... gzip and gunzip are excluded because those need to link to pigz instead
BUSYBOX_LINKS := $(shell cat external/busybox/busybox-full.links)
exclude := tune2fs mke2fs mkdosfs gzip gunzip
ifeq ($(TWHAVE_SELINUX), true)
	exclude += ls
	# toolbox will provide ls support with ls -Z capability for listing SELinux contexts
endif
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
LOCAL_C_INCLUDES := bootable/recovery/libmincrypt/includes
LOCAL_SRC_FILES := \
    verifier_test.cpp \
    verifier.cpp \
    ui.cpp
LOCAL_STATIC_LIBRARIES := \
    libmincrypttwrp \
    libminui \
    libcutils \
    libstdc++ \
    libc
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := libaosprecovery
LOCAL_MODULE_TAGS := eng
LOCAL_MODULES_TAGS = optional
ifeq ($(TARGET_BOARD_PLATFORM),rk30xx)
    LOCAL_CFLAGS += -DRK3066
endif
LOCAL_C_INCLUDES := bootable/recovery/libmincrypt/includes
LOCAL_SRC_FILES = adb_install.cpp bootloader.cpp verifier.cpp mtdutils/mtdutils.c
LOCAL_SHARED_LIBRARIES += libc liblog libcutils libmtdutils
LOCAL_STATIC_LIBRARIES += libmincrypttwrp

include $(BUILD_SHARED_LIBRARY)

commands_recovery_local_path := $(LOCAL_PATH)
include $(LOCAL_PATH)/minui/Android.mk \
    $(LOCAL_PATH)/minelf/Android.mk \
    $(LOCAL_PATH)/minadbd/Android.mk \
    $(LOCAL_PATH)/tools/Android.mk \
    $(LOCAL_PATH)/edify/Android.mk \
    $(LOCAL_PATH)/updater/Android.mk \
    $(LOCAL_PATH)/applypatch/Android.mk

#includes for TWRP
include $(commands_recovery_local_path)/injecttwrp/Android.mk \
    $(commands_recovery_local_path)/htcdumlock/Android.mk \
    $(commands_recovery_local_path)/gui/Android.mk \
    $(commands_recovery_local_path)/mmcutils/Android.mk \
    $(commands_recovery_local_path)/bmlutils/Android.mk \
    $(commands_recovery_local_path)/prebuilt/Android.mk \
    $(commands_recovery_local_path)/mtdutils/Android.mk \
    $(commands_recovery_local_path)/flashutils/Android.mk \
    $(commands_recovery_local_path)/pigz/Android.mk \
    $(commands_recovery_local_path)/dosfstools/Android.mk \
    $(commands_recovery_local_path)/libtar/Android.mk \
    $(commands_recovery_local_path)/crypto/cryptsettings/Android.mk \
    $(commands_recovery_local_path)/crypto/cryptfs/Android.mk \
    $(commands_recovery_local_path)/libcrecovery/Android.mk \
    $(commands_recovery_local_path)/libblkid/Android.mk \
    $(commands_recovery_local_path)/minuitwrp/Android.mk \
    $(commands_recovery_local_path)/openaes/Android.mk \
    $(commands_recovery_local_path)/toolbox/Android.mk \
    $(commands_recovery_local_path)/libmincrypt/Android.mk

ifeq ($(TW_INCLUDE_CRYPTO_SAMSUNG), true)
    include $(commands_recovery_local_path)/crypto/libcrypt_samsung/Android.mk
endif

ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
    include $(commands_recovery_local_path)/crypto/jb/Android.mk
    include $(commands_recovery_local_path)/crypto/fs_mgr/Android.mk
    include $(commands_recovery_local_path)/crypto/logwrapper/Android.mk
    include $(commands_recovery_local_path)/crypto/scrypt/Android.mk
    include $(commands_recovery_local_path)/crypto/crypttools/Android.mk
endif
ifeq ($(TWHAVE_SELINUX), true)
    include $(commands_recovery_local_path)/minzip/Android.mk
else
    include $(commands_recovery_local_path)/minzipold/Android.mk
endif
ifeq ($(BUILD_ID), GINGERBREAD)
    TW_NO_EXFAT := true
endif
ifneq ($(TW_NO_EXFAT), true)
    include $(commands_recovery_local_path)/exfat/exfat-fuse/Android.mk \
            $(commands_recovery_local_path)/exfat/mkfs/Android.mk \
            $(commands_recovery_local_path)/fuse/Android.mk \
            $(commands_recovery_local_path)/exfat/libexfat/Android.mk
endif
ifeq ($(TW_INCLUDE_CRYPTO), true)
    include $(commands_recovery_local_path)/crypto/ics/Android.mk
endif

# FB2PNG
ifeq ($(TW_INCLUDE_FB2PNG), true)
    include $(commands_recovery_local_path)/fb2png/Android.mk
endif

commands_recovery_local_path :=
