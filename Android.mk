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

ifdef project-path-for
    ifeq ($(LOCAL_PATH),$(call project-path-for,recovery))
        PROJECT_PATH_AGREES := true
    endif
else
    ifeq ($(LOCAL_PATH),bootable/recovery)
        PROJECT_PATH_AGREES := true
    endif
endif

ifeq ($(PROJECT_PATH_AGREES),true)

include $(CLEAR_VARS)

TWRES_PATH := "/twres/"
TWHTCD_PATH := $(TWRES_PATH)htcd/

TARGET_RECOVERY_GUI := true

LOCAL_SRC_FILES := \
    twrp.cpp \
    fixPermissions.cpp \
    twrpTar.cpp \
    twrpDU.cpp \
    twrpDigest.cpp \
    digest/md5.c \
    find_file.cpp \
    infomanager.cpp

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

#ifeq ($(HOST_OS),linux)
#LOCAL_REQUIRED_MODULES := mkfs.f2fs
#endif

RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wno-unused-parameter

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

LOCAL_C_INCLUDES += bionic external/stlport/stlport external/openssl/include $(LOCAL_PATH)/libmincrypt/includes

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

LOCAL_STATIC_LIBRARIES += libguitwrp
LOCAL_SHARED_LIBRARIES += libz libc libstlport libcutils libstdc++ libtar libblkid libminuitwrp libminadbd libmtdutils libminzip libaosprecovery
LOCAL_SHARED_LIBRARIES += libgccdemangle libcrecovery

ifneq ($(wildcard system/core/libsparse/Android.mk),)
LOCAL_SHARED_LIBRARIES += libsparse
endif

ifeq ($(TW_OEM_BUILD),true)
    LOCAL_CFLAGS += -DTW_OEM_BUILD
    BOARD_HAS_NO_REAL_SDCARD := true
    TW_USE_TOOLBOX := true
    TW_EXCLUDE_SUPERSU := true
    TW_EXCLUDE_MTP := true
endif
ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    LOCAL_CFLAGS += -DUSE_EXT4
    LOCAL_C_INCLUDES += system/extras/ext4_utils
    LOCAL_SHARED_LIBRARIES += libext4_utils
    ifneq ($(wildcard external/lz4/Android.mk),)
        LOCAL_STATIC_LIBRARIES += liblz4-static
    endif
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
        ifneq ($(wildcard external/lz4/Android.mk),)
            LOCAL_STATIC_LIBRARIES += liblz4-static
        endif
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
ifeq ($(TW_EXCLUDE_MTP),)
    LOCAL_SHARED_LIBRARIES += libtwrpmtp
    LOCAL_CFLAGS += -DTW_HAS_MTP
endif
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
ifeq ($(TW_NO_CPU_TEMP), true)
    LOCAL_CFLAGS += -DTW_NO_CPU_TEMP
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
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
    TW_INCLUDE_CRYPTO := true
endif
ifeq ($(TW_INCLUDE_L_CRYPTO), true)
    TW_INCLUDE_CRYPTO := true
endif
ifeq ($(TW_INCLUDE_CRYPTO), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO
    LOCAL_SHARED_LIBRARIES += libcryptfslollipop
endif
ifeq ($(TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID), true)
    LOCAL_CFLAGS += -DTW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID
endif
ifneq ($(TW_BRIGHTNESS_PATH),)
	LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=$(TW_BRIGHTNESS_PATH)
endif
ifneq ($(TW_SECONDARY_BRIGHTNESS_PATH),)
	LOCAL_CFLAGS += -DTW_SECONDARY_BRIGHTNESS_PATH=$(TW_SECONDARY_BRIGHTNESS_PATH)
endif
ifneq ($(TW_MAX_BRIGHTNESS),)
	LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=$(TW_MAX_BRIGHTNESS)
endif
ifneq ($(TW_CUSTOM_BATTERY_PATH),)
	LOCAL_CFLAGS += -DTW_CUSTOM_BATTERY_PATH=$(TW_CUSTOM_BATTERY_PATH)
endif
ifneq ($(TW_CUSTOM_CPU_TEMP_PATH),)
	LOCAL_CFLAGS += -DTW_CUSTOM_CPU_TEMP_PATH=$(TW_CUSTOM_CPU_TEMP_PATH)
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
ifneq ($(TW_NO_LEGACY_PROPS),)
	LOCAL_CFLAGS += -DTW_NO_LEGACY_PROPS
endif
ifneq ($(wildcard bionic/libc/include/sys/capability.h),)
    LOCAL_CFLAGS += -DHAVE_CAPABILITIES
endif
ifneq ($(TARGET_RECOVERY_INITRC),)
    TW_EXCLUDE_DEFAULT_USB_INIT := true
endif

LOCAL_ADDITIONAL_DEPENDENCIES := \
    dump_image \
    erase_image \
    flash_image \
    fix_permissions.sh \
    mke2fs.conf \
    pigz \
    teamwin \
    toolbox_symlinks \
    twrp \
    unpigz_symlink \
    dosfsck \
    dosfslabel \
    fsck_msdos_symlink \
    mkdosfs

ifneq ($(TARGET_ARCH), arm64)
    ifneq ($(TARGET_ARCH), x86_64)
        LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker
    else
        LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
    endif
else
    LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
endif
ifneq ($(TW_USE_TOOLBOX), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += busybox_symlinks
endif
ifneq ($(TW_NO_EXFAT), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += mkexfatfs
endif
ifeq ($(BOARD_HAS_NO_REAL_SDCARD),)
    LOCAL_ADDITIONAL_DEPENDENCIES += parted
endif
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += openaes ../openaes/LICENSE
endif
ifeq ($(TW_INCLUDE_DUMLOCK), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += \
        htcdumlock htcdumlocksys flash_imagesys dump_imagesys libbmlutils.so \
        libflashutils.so libmmcutils.so libmtdutils.so HTCDumlock.apk
endif
ifneq ($(TW_EXCLUDE_SUPERSU), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += \
        su install-recovery.sh 99SuperSUDaemon Superuser.apk
endif
ifneq ($(TW_NO_EXFAT_FUSE), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += exfat-fuse
endif
ifeq ($(TW_INCLUDE_FB2PNG), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += fb2png
endif
ifneq ($(TW_OEM_BUILD),true)
    LOCAL_ADDITIONAL_DEPENDENCIES += orscmd
endif
ifeq ($(BOARD_USES_BML_OVER_MTD),true)
    LOCAL_ADDITIONAL_DEPENDENCIES += bml_over_mtd
endif
ifeq ($(TW_INCLUDE_INJECTTWRP), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += injecttwrp
endif
ifneq ($(TW_EXCLUDE_DEFAULT_USB_INIT), true)
    LOCAL_ADDITIONAL_DEPENDENCIES += init.recovery.usb.rc
endif
# Allow devices to specify device-specific recovery dependencies
ifneq ($(TARGET_RECOVERY_DEVICE_MODULES),)
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_RECOVERY_DEVICE_MODULES)
endif
LOCAL_CFLAGS += -DTWRES=\"$(TWRES_PATH)\"
LOCAL_CFLAGS += -DTWHTCD_PATH=\"$(TWHTCD_PATH)\"
ifeq ($(TW_INCLUDE_NTFS_3G),true)
    LOCAL_ADDITIONAL_DEPENDENCIES += \
        ntfs-3g \
        ntfsfix \
        mkntfs
endif

include $(BUILD_EXECUTABLE)

ifneq ($(TW_USE_TOOLBOX), true)
include $(CLEAR_VARS)
# Create busybox symlinks... gzip and gunzip are excluded because those need to link to pigz instead
BUSYBOX_LINKS := $(shell cat external/busybox/busybox-full.links)
exclude := tune2fs mke2fs mkdosfs gzip gunzip

# If busybox does not have restorecon, assume it does not have SELinux support.
# Then, let toolbox provide 'ls' so -Z is available to list SELinux contexts.
ifeq ($(TWHAVE_SELINUX), true)
	ifeq ($(filter restorecon, $(notdir $(BUSYBOX_LINKS))),)
		exclude += ls
	endif
endif

RECOVERY_BUSYBOX_TOOLS := $(filter-out $(exclude), $(notdir $(BUSYBOX_LINKS)))
RECOVERY_BUSYBOX_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/, $(RECOVERY_BUSYBOX_TOOLS))
$(RECOVERY_BUSYBOX_SYMLINKS): BUSYBOX_BINARY := busybox
$(RECOVERY_BUSYBOX_SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@

include $(CLEAR_VARS)
LOCAL_MODULE := busybox_symlinks
LOCAL_MODULE_TAGS := optional
LOCAL_ADDITIONAL_DEPENDENCIES := $(RECOVERY_BUSYBOX_SYMLINKS)
ifneq (,$(filter $(PLATFORM_SDK_VERSION),16 17 18))
ALL_DEFAULT_INSTALLED_MODULES += $(RECOVERY_BUSYBOX_SYMLINKS)
endif
include $(BUILD_PHONY_PACKAGE)
RECOVERY_BUSYBOX_SYMLINKS :=
endif # !TW_USE_TOOLBOX

# All the APIs for testing
include $(CLEAR_VARS)
LOCAL_MODULE := libverifier
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := \
    asn1_decoder.cpp
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := fuse_sideload.c

LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libfusesideload
LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmincrypt/includes
LOCAL_SHARED_LIBRARIES := libcutils libc libmincrypttwrp
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := verifier_test
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS := tests

LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmincrypt/includes

LOCAL_CFLAGS += -DNO_RECOVERY_MOUNT
LOCAL_CFLAGS += -Wno-unused-parameter

LOCAL_SRC_FILES := \
    verifier_test.cpp \
    asn1_decoder.cpp \
    verifier.cpp \
    ui.cpp
LOCAL_STATIC_LIBRARIES := \
    libmincrypttwrp \
    libminui \
    libminzip \
    libcutils \
    libstdc++ \
    libc
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_MODULE := libaosprecovery
LOCAL_MODULE_TAGS := eng optional
LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmincrypt/includes
LOCAL_SRC_FILES := adb_install.cpp asn1_decoder.cpp bootloader.cpp legacy_property_service.c verifier.cpp set_metadata.c tw_atomic.cpp
LOCAL_SHARED_LIBRARIES += libc liblog libcutils libmtdutils libfusesideload libmincrypttwrp libselinux

ifneq ($(BOARD_RECOVERY_BLDRMSG_OFFSET),)
    LOCAL_CFLAGS += -DBOARD_RECOVERY_BLDRMSG_OFFSET=$(BOARD_RECOVERY_BLDRMSG_OFFSET)
endif

include $(BUILD_SHARED_LIBRARY)

commands_recovery_local_path := $(LOCAL_PATH)
include $(LOCAL_PATH)/minui/Android.mk \
    $(LOCAL_PATH)/minadbd/Android.mk \
    $(LOCAL_PATH)/tests/Android.mk \
    $(LOCAL_PATH)/tools/Android.mk \
    $(LOCAL_PATH)/edify/Android.mk \
    $(LOCAL_PATH)/updater/Android.mk \
    $(LOCAL_PATH)/applypatch/Android.mk

ifeq ($(wildcard system/core/uncrypt/Android.mk),)
    include $(commands_recovery_local_path)/uncrypt/Android.mk
endif

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
    $(commands_recovery_local_path)/libtar/Android.mk \
    $(commands_recovery_local_path)/libcrecovery/Android.mk \
    $(commands_recovery_local_path)/libblkid/Android.mk \
    $(commands_recovery_local_path)/minuitwrp/Android.mk \
    $(commands_recovery_local_path)/openaes/Android.mk \
    $(commands_recovery_local_path)/toolbox/Android.mk \
    $(commands_recovery_local_path)/libmincrypt/Android.mk \
    $(commands_recovery_local_path)/twrpTarMain/Android.mk \
    $(commands_recovery_local_path)/mtp/Android.mk \
    $(commands_recovery_local_path)/minzip/Android.mk \
    $(commands_recovery_local_path)/dosfstools/Android.mk \
    $(commands_recovery_local_path)/etc/Android.mk

ifeq ($(TW_INCLUDE_CRYPTO), true)
    include $(commands_recovery_local_path)/crypto/lollipop/Android.mk
    include $(commands_recovery_local_path)/crypto/scrypt/Android.mk
endif
ifeq ($(BUILD_ID), GINGERBREAD)
    TW_NO_EXFAT := true
endif
ifneq ($(TW_NO_EXFAT), true)
    include $(commands_recovery_local_path)/exfat/mkfs/Android.mk \
            $(commands_recovery_local_path)/fuse/Android.mk \
            $(commands_recovery_local_path)/exfat/libexfat/Android.mk
endif
ifneq ($(TW_NO_EXFAT_FUSE), true)
    include $(commands_recovery_local_path)/exfat/exfat-fuse/Android.mk
endif
ifneq ($(TW_OEM_BUILD),true)
    include $(commands_recovery_local_path)/orscmd/Android.mk
endif

# FB2PNG
ifeq ($(TW_INCLUDE_FB2PNG), true)
    include $(commands_recovery_local_path)/fb2png/Android.mk
endif

commands_recovery_local_path :=

endif
