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
commands_TWRP_local_path := $(LOCAL_PATH)

ifneq ($(project-path-for),)
    ifeq ($(LOCAL_PATH),$(call project-path-for,recovery))
        PROJECT_PATH_AGREES := true
        BOARD_SEPOLICY_DIRS += $(call project-path-for,recovery)/sepolicy
    endif
else
    ifeq ($(LOCAL_PATH),bootable/recovery)
        PROJECT_PATH_AGREES := true
        BOARD_SEPOLICY_DIRS += bootable/recovery/sepolicy
    else
        ifeq ($(LOCAL_PATH),bootable/recovery-twrp)
            ifeq ($(RECOVERY_VARIANT),twrp)
                PROJECT_PATH_AGREES := true
                BOARD_SEPOLICY_DIRS += bootable/recovery-twrp/sepolicy
            endif
        endif
    endif
endif

ifeq ($(PROJECT_PATH_AGREES),true)

ifeq ($(CM_PLATFORM_SDK_VERSION),)
    CM_PLATFORM_SDK_VERSION := 0
endif

include $(CLEAR_VARS)

TWRES_PATH := /twres/
TWHTCD_PATH := $(TWRES_PATH)htcd/

TARGET_RECOVERY_GUI := true

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

ifneq ($(TW_DEVICE_VERSION),)
    LOCAL_CFLAGS += -DTW_DEVICE_VERSION='"-$(TW_DEVICE_VERSION)"'
else
    LOCAL_CFLAGS += -DTW_DEVICE_VERSION='"-0"'
endif
LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_SRC_FILES := \
    twrp.cpp \
    fixContexts.cpp \
    twrpTar.cpp \
    exclude.cpp \
    find_file.cpp \
    infomanager.cpp \
    data.cpp \
    partition.cpp \
    partitionmanager.cpp \
    progresstracking.cpp \
    startupArgs.cpp \
    twrp-functions.cpp \
    twrpDigestDriver.cpp \
    openrecoveryscript.cpp \
    tarWrite.c \
    twrpAdbBuFifo.cpp \
    twrpApex.cpp \
    twrpRepacker.cpp

LOCAL_STATIC_LIBRARIES += libavb libtwrpinstall
LOCAL_SHARED_LIBRARIES += libfs_mgr libinit
LOCAL_C_INCLUDES += \
    system/core/fs_mgr/libfs_avb/include/ \
    system/core/fs_mgr/include_fstab/ \
    system/core/fs_mgr/include/ \
    system/core/fs_mgr/libdm/include/ \
    system/core/fs_mgr/liblp/include/ \
    system/gsid/include/ \
    system/core/init/ \
    system/extras/ext4_utils/include \
    $(LOCAL_PATH)/twinstall/include

ifneq ($(TARGET_RECOVERY_REBOOT_SRC),)
  LOCAL_SRC_FILES += $(TARGET_RECOVERY_REBOOT_SRC)
endif

LOCAL_MODULE := recovery

RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wno-unused-parameter -Wno-unused-function
LOCAL_CLANG := true

LOCAL_C_INCLUDES += \
    bionic \
    system/vold \
    system/extras \
    system/core/adb \
    system/core/libsparse \
    external/zlib \
    system/core/libpixelflinger/include \
    system/core/libziparchive/include \
    external/freetype/include \
    external/boringssl/include \
    external/libcxx/include \
    external/libselinux/include \
    $(LOCAL_PATH)/recovery_ui/include \
    $(LOCAL_PATH)/otautil/include \
    $(LOCAL_PATH)/install/include \
    $(LOCAL_PATH)/fuse_sideload/include \
    $(LOCAL_PATH)/install/include \
    $(LOCAL_PATH)/twrpinstall/include

LOCAL_STATIC_LIBRARIES += libguitwrp
LOCAL_SHARED_LIBRARIES += libz libc libcutils libstdc++ libtar libblkid libminuitwrp libminadbd libmtdutils libtwadbbu libbootloader_message
LOCAL_SHARED_LIBRARIES += libcrecovery libtwrpdigest libc++ libaosprecovery libinit libcrypto libbase libziparchive libselinux

ifneq ($(wildcard system/core/libsparse/Android.mk),)
LOCAL_SHARED_LIBRARIES += libsparse
endif

ifeq ($(TW_OEM_BUILD),true)
    LOCAL_CFLAGS += -DTW_OEM_BUILD
    BOARD_HAS_NO_REAL_SDCARD := true
    TW_USE_TOOLBOX := true
    TW_EXCLUDE_MTP := true
    TW_EXCLUDE_TZDATA := true
    TW_EXCLUDE_NANO := true
    TW_EXCLUDE_BASH := true
endif

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
    LOCAL_SHARED_LIBRARIES += libhardware android.hardware.boot@1.0
    TWRP_REQUIRED_MODULES += libhardware android.hardware.boot@1.0-service android.hardware.boot@1.0-service.rc
endif

ifeq ($(PRODUCT_USE_DYNAMIC_PARTITIONS),true)
    LOCAL_CFLAGS += -DPRODUCT_USE_DYNAMIC_PARTITIONS=1
    TWRP_REQUIRED_MODULES += android.hardware.health@2.0-service android.hardware.health@2.0-service.rc
endif

ifeq ($(TW_USES_VENDOR_LIBS),true)
    LOCAL_CFLAGS += -DUSE_VENDOR_LIBS=1
endif

ifneq ($(BOARD_GOOGLE_DYNAMIC_PARTITIONS_PARTITION_LIST),)
	BOARD_DYNAMIC_PARTITIONS_PARTITION_LIST := $(BOARD_GOOGLE_DYNAMIC_PARTITIONS_PARTITION_LIST)
else ifneq ($(BOARD_QTI_DYNAMIC_PARTITIONS_PARTITION_LIST),)
	BOARD_DYNAMIC_PARTITIONS_PARTITION_LIST := $(BOARD_QTI_DYNAMIC_PARTITIONS_PARTITION_LIST)
else ifneq ($(BOARD_SAMSUNG_DYNAMIC_PARTITIONS_PARTITION_LIST),)
	BOARD_DYNAMIC_PARTITIONS_PARTITION_LIST := $(BOARD_SAMSUNG_DYNAMIC_PARTITIONS_PARTITION_LIST)
endif
ifneq ($(BOARD_DYNAMIC_PARTITIONS_PARTITION_LIST),)
	LOCAL_CFLAGS += "-DBOARD_DYNAMIC_PARTITIONS_PARTITION_LIST=\"$(shell echo $(BOARD_DYNAMIC_PARTITIONS_PARTITION_LIST) | sed -r 's/\b(.)/\u\1/g' | sed -e 's/ \+/, /g')\""
endif

ifeq ($(TW_NO_BIND_SYSTEM),true)
    LOCAL_CFLAGS += -DTW_NO_BIND_SYSTEM
endif

LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin

ifeq ($(TARGET_RECOVERY_TWRP_LIB),)
    LOCAL_SRC_FILES += BasePartition.cpp
else
    LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_TWRP_LIB)
endif

LOCAL_C_INCLUDES += system/extras/ext4_utils

tw_git_revision := $(shell git -C $(LOCAL_PATH) rev-parse --short=8 HEAD 2>/dev/null)
ifeq ($(shell git -C $(LOCAL_PATH) diff --quiet; echo $$?),1)
    tw_git_revision := $(tw_git_revision)-dirty
endif
LOCAL_CFLAGS += -DTW_GIT_REVISION='"$(tw_git_revision)"'

ifeq ($(TW_FORCE_USE_BUSYBOX), true)
    TW_USE_TOOLBOX := false
else
    TW_USE_TOOLBOX := true
endif
ifeq ($(TW_EXCLUDE_MTP),)
    LOCAL_SHARED_LIBRARIES += libtwrpmtp-ffs
endif

#TWRP Build Flags
ifeq ($(TW_EXCLUDE_MTP),)
    LOCAL_CFLAGS += -DTW_HAS_MTP
endif
ifneq ($(TW_NO_SCREEN_TIMEOUT),)
    LOCAL_CFLAGS += -DTW_NO_SCREEN_TIMEOUT
endif
ifeq ($(BOARD_HAS_NO_REAL_SDCARD), true)
    LOCAL_CFLAGS += -DBOARD_HAS_NO_REAL_SDCARD
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
ifneq ($(TARGET_USE_CUSTOM_LUN_FILE_PATH),)
    LOCAL_CFLAGS += -DCUSTOM_LUN_FILE=\"$(TARGET_USE_CUSTOM_LUN_FILE_PATH)\"
endif
ifneq ($(BOARD_UMS_LUNFILE),)
    LOCAL_CFLAGS += -DCUSTOM_LUN_FILE=\"$(BOARD_UMS_LUNFILE)\"
endif
ifeq ($(TW_HAS_DOWNLOAD_MODE), true)
    LOCAL_CFLAGS += -DTW_HAS_DOWNLOAD_MODE
endif
ifeq ($(TW_HAS_EDL_MODE), true)
    LOCAL_CFLAGS += -DTW_HAS_EDL_MODE
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
ifeq ($(TW_NO_HAPTICS), true)
    LOCAL_CFLAGS += -DTW_NO_HAPTICS
endif
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
    TW_INCLUDE_CRYPTO := true
endif
ifeq ($(TW_INCLUDE_L_CRYPTO), true)
    TW_INCLUDE_CRYPTO := true
endif
ifeq ($(TW_INCLUDE_CRYPTO), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO -DUSE_FSCRYPT -Wno-macro-redefined
    LOCAL_SHARED_LIBRARIES += libcryptfsfde libgpt_twrp
    LOCAL_C_INCLUDES += external/boringssl/src/include bootable/recovery/crypto/fscrypt \
        bootable/recovery/crypto
    TW_INCLUDE_CRYPTO_FBE := true
    LOCAL_CFLAGS += -DTW_INCLUDE_FBE
    LOCAL_SHARED_LIBRARIES += libtwrpfscrypt android.frameworks.stats@1.0 android.hardware.authsecret@1.0 \
        android.hardware.oemlock@1.0
    LOCAL_CFLAGS += -DTW_INCLUDE_FBE_METADATA_DECRYPT
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),)
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),false)
		TW_INCLUDE_LIBRESETPROP := true
        LOCAL_CFLAGS += -DTW_CRYPTO_USE_SYSTEM_VOLD
        LOCAL_STATIC_LIBRARIES += libvolddecrypt
    endif
    endif
endif
WITH_CRYPTO_UTILS := \
    $(if $(wildcard system/core/libcrypto_utils/android_pubkey.c),true)
ifeq ($(TW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID), true)
    LOCAL_CFLAGS += -DTW_USE_MODEL_HARDWARE_ID_FOR_DEVICE_ID
endif
ifeq ($(TW_USE_SERIALNO_PROPERTY_FOR_DEVICE_ID), true)
    LOCAL_CFLAGS += -DTW_USE_SERIALNO_PROPERTY_FOR_DEVICE_ID
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
ifneq ($(TW_DEFAULT_BRIGHTNESS),)
	LOCAL_CFLAGS += -DTW_DEFAULT_BRIGHTNESS=$(TW_DEFAULT_BRIGHTNESS)
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
  ifneq ($(filter msm8226 msm8x26 msm8610 msm8974 msm8x74 msm8084 msm8x84 apq8084 msm8909 msm8916 msm8992 msm8994 msm8952 msm8996 msm8937 msm8953 msm8998,$(TARGET_BOARD_PLATFORM)),)
    LOCAL_CFLAGS += -DQCOM_RTC_FIX
  else ifeq ($(TARGET_CPU_VARIANT),krait)
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
LOCAL_CFLAGS += -DTW_USE_NEW_MINADBD
ifneq ($(TW_DEFAULT_LANGUAGE),)
    LOCAL_CFLAGS += -DTW_DEFAULT_LANGUAGE=$(TW_DEFAULT_LANGUAGE)
else
    LOCAL_CFLAGS += -DTW_DEFAULT_LANGUAGE=en
endif
ifneq ($(TW_CLOCK_OFFSET),)
	LOCAL_CFLAGS += -DTW_CLOCK_OFFSET=$(TW_CLOCK_OFFSET)
endif
ifneq ($(TW_OVERRIDE_SYSTEM_PROPS),)
    TW_INCLUDE_LIBRESETPROP := true
    LOCAL_CFLAGS += -DTW_OVERRIDE_SYSTEM_PROPS=$(TW_OVERRIDE_SYSTEM_PROPS)
endif
ifneq ($(TW_INCLUDE_LIBRESETPROP),)
    LOCAL_SHARED_LIBRARIES += libresetprop
    LOCAL_C_INCLUDES += external/magisk-prebuilt/include
    LOCAL_CFLAGS += -DTW_INCLUDE_LIBRESETPROP
endif
ifeq ($(TW_EXCLUDE_NANO), true)
    LOCAL_CFLAGS += -DTW_EXCLUDE_NANO
endif

TWRP_REQUIRED_MODULES += \
    relink_libraries \
    relink_binaries \
    twrp_ramdisk \
    dump_image \
    erase_image \
    flash_image \
    mke2fs.conf \
    pigz \
    teamwin \
    twrp \
    fsck.fat \
    fatlabel \
    mkfs.fat \
    permissive.sh \
    simg2img_twrp \
    libbootloader_message \
    init.recovery.hlthchrg.rc \
    init.recovery.service.rc \
    init.recovery.ldconfig.rc \
    awk \
    toybox \
    toolbox \
    mkshrc_twrp \
    plat_hwservice_contexts \
    vendor_hwservice_contexts \
    minadbd \
    twrpbu \
    me.twrp.twrpapp.apk \
    privapp-permissions-twrpapp.xml

ifneq ($(TW_EXCLUDE_TZDATA), true)
TWRP_REQUIRED_MODULES += \
    tzdata_twrp
endif

ifneq ($(TW_EXCLUDE_NANO), true)
TWRP_REQUIRED_MODULES += \
    nano_twrp \
    nano.rc
endif

ifneq ($(TW_EXCLUDE_BASH), true)
    ifneq ($(wildcard external/bash/.),)
    TWRP_REQUIRED_MODULES += \
        bash_twrp
    endif
endif

ifeq ($(TW_INCLUDE_REPACKTOOLS), true)
TWRP_REQUIRED_MODULES += \
    magiskboot
endif

ifeq ($(TW_INCLUDE_RESETPROP), true)
TWRP_REQUIRED_MODULES += \
    resetprop
endif

ifneq ($(TW_INCLUDE_CRYPTO),)
TWRP_REQUIRED_MODULES += \
    hwservicemanager \
    hwservicemanager.rc \
    vndservicemanager \
    vndservicemanager.rc \
    vold_prepare_subdirs \
    task_recovery_profiles.json \
    fscryptpolicyget
    ifneq ($(TW_INCLUDE_CRYPTO_FBE),)
    TWRP_REQUIRED_MODULES += \
        plat_service_contexts \
        servicemanager \
        servicemanager.rc
    endif
endif

ifneq ($(wildcard external/zip/Android.mk),)
    TWRP_REQUIRED_MODULES += zip
endif
ifneq ($(wildcard external/unzip/Android.mk),)
    TWRP_REQUIRED_MODULES += unzip
endif

ifneq ($(TW_NO_EXFAT), true)
    TWRP_REQUIRED_MODULES += mkexfatfs fsckexfat
    ifneq ($(TW_NO_EXFAT_FUSE), true)
        TWRP_REQUIRED_MODULES += exfat-fuse
    endif
endif
ifeq ($(BOARD_HAS_NO_REAL_SDCARD),)
    TWRP_REQUIRED_MODULES += sgdisk
endif
ifneq ($(TW_EXCLUDE_ENCRYPTED_BACKUPS), true)
    TWRP_REQUIRED_MODULES += openaes openaes_license
endif
ifeq ($(TW_INCLUDE_DUMLOCK), true)
    TWRP_REQUIRED_MODULES += \
        htcdumlock htcdumlocksys flash_imagesys dump_imagesys libbmlutils.so \
        libflashutils.so libmmcutils.so libmtdutils.so HTCDumlock.apk
endif
ifeq ($(TW_INCLUDE_FB2PNG), true)
    TWRP_REQUIRED_MODULES += fb2png
endif
ifneq ($(TW_OEM_BUILD),true)
    TWRP_REQUIRED_MODULES += orscmd
endif
ifeq ($(BOARD_USES_BML_OVER_MTD),true)
    TWRP_REQUIRED_MODULES += bml_over_mtd
endif
ifeq ($(TW_INCLUDE_INJECTTWRP), true)
    TWRP_REQUIRED_MODULES += injecttwrp
endif
ifneq ($(TW_EXCLUDE_DEFAULT_USB_INIT), true)
    TWRP_REQUIRED_MODULES += init.recovery.usb.rc
endif
ifeq ($(TWRP_INCLUDE_LOGCAT), true)
    TWRP_REQUIRED_MODULES += logcat event-log-tags
    ifeq ($(TARGET_USES_LOGD), true)
        TWRP_REQUIRED_MODULES += logd libsysutils libnl init.recovery.logd.rc
    endif
endif
# Allow devices to specify device-specific recovery dependencies
ifneq ($(TARGET_RECOVERY_DEVICE_MODULES),)
    TWRP_REQUIRED_MODULES += $(TARGET_RECOVERY_DEVICE_MODULES)
endif
LOCAL_CFLAGS += -DTWRES=\"$(TWRES_PATH)\"
LOCAL_CFLAGS += -DTWHTCD_PATH=\"$(TWHTCD_PATH)\"
ifeq ($(TW_INCLUDE_NTFS_3G),true)
    TWRP_REQUIRED_MODULES += \
        mount.ntfs \
        fsck.ntfs \
        mkfs.ntfs
endif
ifeq ($(TARGET_USERIMAGES_USE_F2FS), true)
    TWRP_REQUIRED_MODULES += sload.f2fs \
        libfs_mgr \
        fs_mgr \
        libinit
endif

TWRP_REQUIRED_MODULES += file_contexts_text

ifeq ($(BOARD_CACHEIMAGE_PARTITION_SIZE),)
    TWRP_REQUIRED_MODULES += recovery-persist recovery-refresh
endif

LOCAL_REQUIRED_MODULES += $(TWRP_REQUIRED_MODULES)

include $(BUILD_EXECUTABLE)

# Symlink for file_contexts
include $(CLEAR_VARS)

LOCAL_MODULE := file_contexts_text
LOCAL_MODULE_TAGS := optional
LOCAL_REQUIRED_MODULES := file_contexts.bin

LOCAL_POST_INSTALL_CMD := \
    $(hide) cp -f $(PRODUCT_OUT)/obj/ETC/file_contexts.bin_intermediates/file_contexts.concat.tmp $(TARGET_RECOVERY_ROOT_OUT)/file_contexts

include $(BUILD_PHONY_PACKAGE)

# recovery-persist (system partition dynamic executable run after /data mounts)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    recovery-persist.cpp 
LOCAL_MODULE := recovery-persist
LOCAL_SHARED_LIBRARIES := liblog libbase libmetricslogger
LOCAL_STATIC_LIBRARIES := libotautil
LOCAL_C_INCLUDES += $(LOCAL_PATH)/otautil/include
LOCAL_C_INCLUDES += system/core/libmetricslogger/include \
    system/core/libstats/include
LOCAL_CFLAGS := -Werror
LOCAL_INIT_RC := recovery-persist.rc
include $(BUILD_EXECUTABLE)

# recovery-refresh (system partition dynamic executable run at init)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    recovery-refresh.cpp
LOCAL_MODULE := recovery-refresh
LOCAL_SHARED_LIBRARIES := liblog libbase
LOCAL_STATIC_LIBRARIES := libotautil
LOCAL_C_INCLUDES += $(LOCAL_PATH)/otautil/include
LOCAL_CFLAGS := -Werror
LOCAL_INIT_RC := recovery-refresh.rc
include $(BUILD_EXECUTABLE)

# libmounts (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := mounts.cpp
LOCAL_CFLAGS := \
    -Wall \
    -Werror
LOCAL_MODULE := libmounts
LOCAL_STATIC_LIBRARIES := libbase
include $(BUILD_STATIC_LIBRARY)

# librecovery (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    install.cpp
LOCAL_CFLAGS := -Wall -Werror
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
endif

LOCAL_MODULE := librecovery
LOCAL_STATIC_LIBRARIES := \
    libminui \
    libotautil \
    libvintf \
    libcrypto_utils \
    libcrypto \
    libbase \
    libziparchive \

include $(BUILD_STATIC_LIBRARY)

# shared libaosprecovery for Apache code
# ===============================
include $(CLEAR_VARS)


LOCAL_MODULE := libaosprecovery
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := install/adb_install.cpp install/asn1_decoder.cpp install/fuse_sdcard_install.cpp \
    install/get_args.cpp install/install.cpp install/legacy_property_service.cpp \
    install/package.cpp install/verifier.cpp install/wipe_data.cpp \
    install/set_metadata.cpp install/zipwrap.cpp install/ZipUtil.cpp
LOCAL_SHARED_LIBRARIES += libbase libbootloader_message libcrypto libext4_utils \
    libfs_mgr libfusesideload libhidl-gen-utils libhidlbase \
    liblog libselinux libtinyxml2 libutils libz libziparchive libcutils
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_SHARED_LIBRARIES += libc++
LOCAL_CFLAGS := -std=gnu++2a
LOCAL_C_INCLUDES += $(commands_TWRP_local_path)/install/include \
                    $(commands_TWRP_local_path)/recovery_ui/include \
                    $(commands_TWRP_local_path)/otautil/include \
                    $(commands_TWRP_local_path)/minadbd \
                    $(commands_TWRP_local_path)/minzip \
                    $(commands_TWRP_local_path)/twrpinstall/include \
                    system/libvintf/include
LOCAL_STATIC_LIBRARIES += libotautil libvintf_recovery libvintf libhidl-gen-utils
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
endif

include $(BUILD_SHARED_LIBRARY)

commands_recovery_local_path := $(LOCAL_PATH)

include \
    $(commands_TWRP_local_path)/updater/Android.mk

include $(commands_TWRP_local_path)/mtp/ffs/Android.mk \
    $(commands_TWRP_local_path)/minadbd/Android.mk \
    $(commands_TWRP_local_path)/minui/Android.mk

#includes for TWRP
include $(commands_TWRP_local_path)/injecttwrp/Android.mk \
    $(commands_TWRP_local_path)/htcdumlock/Android.mk \
    $(commands_TWRP_local_path)/gui/Android.mk \
    $(commands_TWRP_local_path)/mmcutils/Android.mk \
    $(commands_TWRP_local_path)/bmlutils/Android.mk \
    $(commands_TWRP_local_path)/prebuilt/Android.mk \
    $(commands_TWRP_local_path)/mtdutils/Android.mk \
    $(commands_TWRP_local_path)/flashutils/Android.mk \
    $(commands_TWRP_local_path)/pigz/Android.mk \
    $(commands_TWRP_local_path)/libtar/Android.mk \
    $(commands_TWRP_local_path)/libcrecovery/Android.mk \
    $(commands_TWRP_local_path)/libblkid/Android.mk \
    $(commands_TWRP_local_path)/minuitwrp/Android.mk \
    $(commands_TWRP_local_path)/openaes/Android.mk \
    $(commands_TWRP_local_path)/twrpTarMain/Android.mk \
    $(commands_TWRP_local_path)/minzip/Android.mk \
    $(commands_TWRP_local_path)/dosfstools/Android.mk \
    $(commands_TWRP_local_path)/etc/Android.mk \
    $(commands_TWRP_local_path)/simg2img/Android.mk \
    $(commands_TWRP_local_path)/adbbu/Android.mk \
    $(commands_TWRP_local_path)/libpixelflinger/Android.mk \
    $(commands_TWRP_local_path)/twrpDigest/Android.mk \
    $(commands_TWRP_local_path)/attr/Android.mk

ifneq ($(TW_OZIP_DECRYPT_KEY),)
    TWRP_REQUIRED_MODULES += ozip_decrypt
    include $(commands_TWRP_local_path)/ozip_decrypt/Android.mk
endif

ifeq ($(TW_INCLUDE_CRYPTO), true)
    include $(commands_TWRP_local_path)/crypto/fde/Android.mk
    include $(commands_TWRP_local_path)/crypto/scrypt/Android.mk
    ifeq ($(TW_INCLUDE_CRYPTO_FBE), true)
        include $(commands_TWRP_local_path)/crypto/fscrypt/Android.mk
    endif
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),)
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),false)
        include $(commands_TWRP_local_path)/crypto/vold_decrypt/Android.mk
    endif
    endif
    include $(commands_TWRP_local_path)/gpt/Android.mk
endif
ifeq ($(BUILD_ID), GINGERBREAD)
    TW_NO_EXFAT := true
endif
ifneq ($(TW_NO_EXFAT), true)
    include $(commands_TWRP_local_path)/exfat/mkfs/Android.mk \
            $(commands_TWRP_local_path)/exfat/fsck/Android.mk \
            $(commands_TWRP_local_path)/fuse/Android.mk \
            $(commands_TWRP_local_path)/exfat/libexfat/Android.mk
    ifneq ($(TW_NO_EXFAT_FUSE), true)
        include $(commands_TWRP_local_path)/exfat/fuse/Android.mk
    endif
endif
ifneq ($(TW_OEM_BUILD),true)
    include $(commands_TWRP_local_path)/orscmd/Android.mk
endif

# FB2PNG
ifeq ($(TW_INCLUDE_FB2PNG), true)
    include $(commands_TWRP_local_path)/fb2png/Android.mk
endif

endif

commands_TWRP_local_path :=
