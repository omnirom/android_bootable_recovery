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

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
# Make recovery domain permissive for TWRP
    BOARD_SEPOLICY_UNION += twrp.te
endif

ifeq ($(CM_PLATFORM_SDK_VERSION),)
    CM_PLATFORM_SDK_VERSION := 0
endif

include $(CLEAR_VARS)

TWRES_PATH := /twres/
TWHTCD_PATH := $(TWRES_PATH)htcd/

TARGET_RECOVERY_GUI := true

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
    twinstall.cpp \
    twrp-functions.cpp \
    twrpDigestDriver.cpp \
    openrecoveryscript.cpp \
    tarWrite.c \
    twrpAdbBuFifo.cpp

ifneq ($(TARGET_RECOVERY_REBOOT_SRC),)
  LOCAL_SRC_FILES += $(TARGET_RECOVERY_REBOOT_SRC)
endif

LOCAL_MODULE := recovery

#LOCAL_FORCE_STATIC_EXECUTABLE := true

#ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
#ifeq ($(HOST_OS),linux)
#LOCAL_REQUIRED_MODULES := mkfs.f2fs
#endif
#endif

RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CLANG := true

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

LOCAL_C_INCLUDES += \
    system/vold \
    system/extras \
    system/core/adb \
    system/core/libsparse \
    external/zlib \
    $(LOCAL_PATH)/bootloader_message_twrp/include

LOCAL_C_INCLUDES += bionic
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport external/openssl/include
    LOCAL_CFLAGS += -DUSE_FUSE_SIDELOAD22
else
    LOCAL_C_INCLUDES += external/boringssl/include external/libcxx/include
endif

LOCAL_STATIC_LIBRARIES :=
LOCAL_SHARED_LIBRARIES :=

LOCAL_STATIC_LIBRARIES += libguitwrp
LOCAL_SHARED_LIBRARIES += libaosprecovery libz libc libcutils libstdc++ libtar libblkid libminuitwrp libminadbd libmtdutils libtwadbbu libbootloader_message_twrp
LOCAL_SHARED_LIBRARIES += libcrecovery libtwadbbu libtwrpdigest libc++

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
    LOCAL_CFLAGS += -DTW_NO_SHA2_LIBRARY
endif
LOCAL_CFLAGS += -DSDK_VERSION=$(PLATFORM_SDK_VERSION)
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 25; echo $$?),0)
    LOCAL_CFLAGS += -DUSE_OLD_BASE_INCLUDE
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libmincrypttwrp
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/libmincrypt/includes
    LOCAL_CFLAGS += -DUSE_OLD_VERIFIER
else
    LOCAL_SHARED_LIBRARIES += libcrypto
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libbase
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
   LOCAL_SHARED_LIBRARIES += libziparchive
   LOCAL_C_INCLUDES += $(LOCAL_PATH)/otautil/include
else
    LOCAL_SHARED_LIBRARIES += libminzip
    LOCAL_CFLAGS += -DUSE_MINZIP
endif

ifneq ($(wildcard system/core/libsparse/Android.mk),)
LOCAL_SHARED_LIBRARIES += libsparse
endif

ifeq ($(TW_OEM_BUILD),true)
    LOCAL_CFLAGS += -DTW_OEM_BUILD
    BOARD_HAS_NO_REAL_SDCARD := true
    TW_USE_TOOLBOX := true
    TW_EXCLUDE_MTP := true
endif

ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 28; echo $$?),0)
        LOCAL_CFLAGS += -DUSE_EXT4
    endif
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -le 28; echo $$?),0)
        LOCAL_C_INCLUDES += system/extras/ext4_utils \
            system/extras/ext4_utils/include \
	    $(commands_TWRP_local_path)/crypto/ext4crypt
        LOCAL_SHARED_LIBRARIES += libext4_utils
        ifneq ($(wildcard external/lz4/Android.mk),)
            #LOCAL_STATIC_LIBRARIES += liblz4
        endif
    endif
endif
LOCAL_C_INCLUDES += external/libselinux/include
LOCAL_SHARED_LIBRARIES += libselinux

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
    LOCAL_SHARED_LIBRARIES += libhardware android.hardware.boot@1.0
    TWRP_REQUIRED_MODULES += libhardware
endif

LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

#ifeq ($(TARGET_RECOVERY_UI_LIB),)
#  LOCAL_SRC_FILES += default_device.cpp
#else
#  LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_UI_LIB)
#endif
ifeq ($(TARGET_RECOVERY_TWRP_LIB),)
    LOCAL_SRC_FILES += BasePartition.cpp
else
    LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_TWRP_LIB)
endif

tw_git_revision := $(shell git -C $(LOCAL_PATH) rev-parse --short=8 HEAD 2>/dev/null)
ifeq ($(shell git -C $(LOCAL_PATH) diff --quiet; echo $$?),1)
    tw_git_revision := $(tw_git_revision)-dirty
endif
LOCAL_CFLAGS += -DTW_GIT_REVISION='"$(tw_git_revision)"'

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
ifeq ($(TW_FORCE_USE_BUSYBOX), true)
    TW_USE_TOOLBOX := false
else
    TW_USE_TOOLBOX := true
endif
ifeq ($(TW_EXCLUDE_MTP),)
    LOCAL_SHARED_LIBRARIES += libtwrpmtp-ffs
endif
else
ifeq ($(TW_EXCLUDE_MTP),)
    LOCAL_CFLAGS += -DTW_HAS_LEGACY_MTP
    LOCAL_SHARED_LIBRARIES += libtwrpmtp-legacy
endif
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
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO
    LOCAL_SHARED_LIBRARIES += libcryptfsfde libgpt_twrp
    LOCAL_C_INCLUDES += external/boringssl/src/include
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
        TW_INCLUDE_CRYPTO_FBE := true
        LOCAL_CFLAGS += -DTW_INCLUDE_FBE
        LOCAL_SHARED_LIBRARIES += libe4crypt
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
            LOCAL_CFLAGS += -DTW_INCLUDE_FBE_METADATA_DECRYPT
        endif
    endif
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),)
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),false)
		ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
			TW_INCLUDE_LIBRESETPROP := true
		endif
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
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
    LOCAL_CFLAGS += -DTW_USE_NEW_MINADBD
endif
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
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
        $(warning libresetprop is not available for android < 7)
    else
        LOCAL_SHARED_LIBRARIES += libresetprop
        LOCAL_C_INCLUDES += external/magisk-prebuilt/include
        LOCAL_CFLAGS += -DTW_INCLUDE_LIBRESETPROP
    endif
endif

TWRP_REQUIRED_MODULES += \
    dump_image \
    erase_image \
    flash_image \
    mke2fs.conf \
    pigz \
    teamwin \
    toolbox_symlinks \
    twrp \
    fsck.fat \
    fatlabel \
    mkfs.fat \
    permissive.sh \
    simg2img_twrp \
    libbootloader_message_twrp \
    init.recovery.hlthchrg.rc \
    init.recovery.service.rc

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
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
        LOCAL_POST_INSTALL_CMD += \
            $(hide) mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin && \
            ln -sf /sbin/busybox $(TARGET_RECOVERY_ROOT_OUT)/sbin/sh
    endif
else
    ifneq ($(wildcard external/toybox/Android.mk),)
        TWRP_REQUIRED_MODULES += toybox_symlinks
    endif
    ifneq ($(wildcard external/zip/Android.mk),)
        TWRP_REQUIRED_MODULES += zip
    endif
    ifneq ($(wildcard external/unzip/Android.mk),)
        TWRP_REQUIRED_MODULES += unzip
    endif
endif

ifneq ($(TW_NO_EXFAT), true)
    TWRP_REQUIRED_MODULES += mkexfatfs fsckexfat
    ifneq ($(TW_NO_EXFAT_FUSE), true)
        TWRP_REQUIRED_MODULES += exfat-fuse
    endif
endif
ifeq ($(BOARD_HAS_NO_REAL_SDCARD),)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
        TWRP_REQUIRED_MODULES += sgdisk
    else
        TWRP_REQUIRED_MODULES += sgdisk_static
    endif
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
    TWRP_REQUIRED_MODULES += logcat
    ifeq ($(TARGET_USES_LOGD), true)
        TWRP_REQUIRED_MODULES += logd libsysutils libnl event-log-tags init.recovery.logd.rc
        ifeq ($(BOARD_BUILD_SYSTEM_ROOT_IMAGE),true)
            LOCAL_POST_INSTALL_CMD += \
                $(hide) mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/system_root/system/etc; \
                cp $(TARGET_OUT_ETC)/event-log-tags $(TARGET_RECOVERY_ROOT_OUT)/system_root/system/etc/;
        else
            LOCAL_POST_INSTALL_CMD += \
                $(hide) mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/system/etc; \
                cp $(TARGET_OUT_ETC)/event-log-tags $(TARGET_RECOVERY_ROOT_OUT)/system/etc/;
        endif
    endif
endif
# Allow devices to specify device-specific recovery dependencies
ifneq ($(TARGET_RECOVERY_DEVICE_MODULES),)
    TWRP_REQUIRED_MODULES += $(TARGET_RECOVERY_DEVICE_MODULES)
endif
LOCAL_CFLAGS += -DTWRES=\"$(TWRES_PATH)\"
LOCAL_CFLAGS += -DTWHTCD_PATH=\"$(TWHTCD_PATH)\"
ifeq ($(TW_INCLUDE_NTFS_3G),true)
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
    TWRP_REQUIRED_MODULES += \
        mount.ntfs \
        fsck.ntfs \
        mkfs.ntfs
else
    TWRP_REQUIRED_MODULES += \
        ntfs-3g \
        ntfsfix \
        mkntfs
endif
endif
ifeq ($(TARGET_USERIMAGES_USE_F2FS), true)
ifeq ($(shell test $(CM_PLATFORM_SDK_VERSION) -ge 3; echo $$?),0)
    TWRP_REQUIRED_MODULES += \
        fsck.f2fs \
        mkfs.f2fs
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
    TWRP_REQUIRED_MODULES += sload.f2fs
endif
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
    TWRP_REQUIRED_MODULES += ld.config.txt
    TWRP_REQUIRED_MODULES += init.recovery.ldconfig.rc
    LOCAL_POST_INSTALL_CMD += \
        sed 's/\(namespace.default.search.paths\)\s\{1,\}=/namespace.default.search.paths  = \/sbin\n\1 +=/' \
            $(TARGET_OUT_ETC)/ld.config*.txt > $(TARGET_RECOVERY_ROOT_OUT)/sbin/ld.config.txt;
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 25; echo $$?),0)
    TWRP_REQUIRED_MODULES += file_contexts_text
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
    ifeq ($(BOARD_CACHEIMAGE_PARTITION_SIZE),)
        TWRP_REQUIRED_MODULES += recovery-persist recovery-refresh
    endif
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
    LOCAL_REQUIRED_MODULES += $(TWRP_REQUIRED_MODULES)
else
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TWRP_REQUIRED_MODULES)
endif

include $(BUILD_EXECUTABLE)

# Symlink for file_contexts
include $(CLEAR_VARS)

LOCAL_MODULE := file_contexts_text
LOCAL_MODULE_TAGS := optional
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
    LOCAL_REQUIRED_MODULES := file_contexts.bin
else
    LOCAL_ADDITIONAL_DEPENDENCIES := file_contexts.bin
endif
LOCAL_POST_INSTALL_CMD += \
    $(hide) cp -f $(PRODUCT_OUT)/obj/ETC/file_contexts.bin_intermediates/file_contexts.concat.tmp $(TARGET_RECOVERY_ROOT_OUT)/file_contexts

include $(BUILD_PHONY_PACKAGE)

ifneq ($(TW_USE_TOOLBOX), true)
include $(CLEAR_VARS)
# Create busybox symlinks... gzip and gunzip are excluded because those need to link to pigz instead
BUSYBOX_LINKS := $(shell cat external/busybox/busybox-full.links)
exclude := tune2fs mke2fs mkdosfs mkfs.vfat gzip gunzip

# Having /sbin/modprobe present on 32 bit devices with can cause a massive
# performance problem if the kernel has CONFIG_MODULES=y
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
    ifneq ($(TARGET_ARCH), arm64)
        ifneq ($(TARGET_ARCH), x86_64)
            exclude += modprobe
        endif
    endif
endif

# If busybox does not have restorecon, assume it does not have SELinux support.
# Then, let toolbox provide 'ls' so -Z is available to list SELinux contexts.
ifeq ($(filter restorecon, $(notdir $(BUSYBOX_LINKS))),)
    exclude += ls
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

# recovery-persist (system partition dynamic executable run after /data mounts)
# ===============================
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
    include $(CLEAR_VARS)
    LOCAL_SRC_FILES := \
        recovery-persist.cpp \
        rotate_logs.cpp
    LOCAL_MODULE := recovery-persist
    LOCAL_SHARED_LIBRARIES := liblog libbase
    LOCAL_CFLAGS := -Werror
    LOCAL_INIT_RC := recovery-persist.rc
    include $(BUILD_EXECUTABLE)
endif

# recovery-refresh (system partition dynamic executable run at init)
# ===============================
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
    include $(CLEAR_VARS)
    LOCAL_SRC_FILES := \
        recovery-refresh.cpp \
        rotate_logs.cpp
    LOCAL_MODULE := recovery-refresh
    LOCAL_SHARED_LIBRARIES := liblog libbase
    LOCAL_CFLAGS := -Werror
    LOCAL_INIT_RC := recovery-refresh.rc
    include $(BUILD_EXECUTABLE)
endif

# shared libfusesideload
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_CFLAGS := -Wall -Werror -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libfusesideload
LOCAL_SHARED_LIBRARIES := libcutils libc
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmincrypt/includes
    LOCAL_SHARED_LIBRARIES += libmincrypttwrp
    LOCAL_CFLAGS += -DUSE_MINCRYPT
else
    LOCAL_SHARED_LIBRARIES += libcrypto
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SRC_FILES := fuse_sideload22.cpp
    LOCAL_CFLAGS += -DUSE_FUSE_SIDELOAD22
else
    LOCAL_SRC_FILES := fuse_sideload.cpp
endif
include $(BUILD_SHARED_LIBRARY)

# static libfusesideload
# =============================== (required to fix build errors in 8.1 due to use by tests)
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_CFLAGS := -Wall -Werror -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libfusesideload
LOCAL_SHARED_LIBRARIES := libcutils libc
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    LOCAL_C_INCLUDES := $(LOCAL_PATH)/libmincrypt/includes
    LOCAL_STATIC_LIBRARIES += libmincrypttwrp
    LOCAL_CFLAGS += -DUSE_MINCRYPT
else
    LOCAL_STATIC_LIBRARIES += libcrypto_static
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SRC_FILES := fuse_sideload22.cpp
    LOCAL_CFLAGS += -DUSE_FUSE_SIDELOAD22
else
    LOCAL_SRC_FILES := fuse_sideload.cpp
endif
include $(BUILD_STATIC_LIBRARY)

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
    libvintf_recovery \
    libcrypto_utils \
    libcrypto \
    libbase \
    libziparchive \

include $(BUILD_STATIC_LIBRARY)

# shared libaosprecovery for Apache code
# ===============================
include $(CLEAR_VARS)


LOCAL_MODULE := libaosprecovery
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS := -std=gnu++0x
LOCAL_SRC_FILES := adb_install.cpp legacy_property_service.cpp set_metadata.cpp tw_atomic.cpp installcommand.cpp zipwrap.cpp
LOCAL_SHARED_LIBRARIES += libc liblog libcutils libmtdutils libfusesideload libselinux libminzip
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstdc++ libstlport
    LOCAL_C_INCLUDES += bionic external/stlport/stlport
    LOCAL_CFLAGS += -DUSE_FUSE_SIDELOAD22
else
    LOCAL_SHARED_LIBRARIES += libc++
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libmincrypttwrp
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/libmincrypt/includes
    LOCAL_SRC_FILES += verifier24/verifier.cpp verifier24/asn1_decoder.cpp
    LOCAL_CFLAGS += -DUSE_OLD_VERIFIER
else
    LOCAL_SHARED_LIBRARIES += libcrypto libbase
    LOCAL_SRC_FILES += verifier.cpp asn1_decoder.cpp
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/otautil/include
endif

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
    LOCAL_SRC_FILES += otautil/ZipUtil.cpp otautil/SysUtil.cpp otautil/DirUtil.cpp
    LOCAL_SHARED_LIBRARIES += libziparchive libext4_utils libcrypto libcrypto_utils
    LOCAL_STATIC_LIBRARIES += libvintf_recovery libfs_mgr liblogwrap libavb libvintf libtinyxml2 libz
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/otautil/include
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 27; echo $$?),0)
        # Android 9.0 needs c++17 for libvintf
        LOCAL_CPPFLAGS += -std=c++17
        # Android 9.0's libvintf also needs this library
        LOCAL_STATIC_LIBRARIES += libhidl-gen-utils
    endif
else
    LOCAL_CFLAGS += -DUSE_MINZIP
endif

include $(BUILD_SHARED_LIBRARY)
# libverifier (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_MODULE := libverifier
LOCAL_SRC_FILES := \
    asn1_decoder.cpp \
    verifier.cpp
LOCAL_STATIC_LIBRARIES := \
    libotautil \
    libcrypto_utils \
    libcrypto \
    libbase
LOCAL_CFLAGS := -Wall -Werror
include $(BUILD_STATIC_LIBRARY)

# Wear default device
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := wear_device.cpp
LOCAL_CFLAGS := -Wall -Werror

# Should match TARGET_RECOVERY_UI_LIB in BoardConfig.mk.
LOCAL_MODULE := librecovery_ui_wear

include $(BUILD_STATIC_LIBRARY)

# vr headset default device
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := vr_device.cpp
LOCAL_CFLAGS := -Wall -Werror

# should match TARGET_RECOVERY_UI_LIB set in BoardConfig.mk
LOCAL_MODULE := librecovery_ui_vr

include $(BUILD_STATIC_LIBRARY)

commands_recovery_local_path := $(LOCAL_PATH)

#    $(LOCAL_PATH)/edify/Android.mk
#    $(LOCAL_PATH)/otafault/Android.mk
#    $(LOCAL_PATH)/bootloader_message/Android.mk
include \
    $(commands_TWRP_local_path)/boot_control/Android.mk \
    $(commands_TWRP_local_path)/tests/Android.mk \
    $(commands_TWRP_local_path)/tools/Android.mk \
    $(commands_TWRP_local_path)/updater/Android.mk \
    $(commands_TWRP_local_path)/update_verifier/Android.mk \
    $(commands_TWRP_local_path)/bootloader_message_twrp/Android.mk

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -le 25; echo $$?),0)
include $(commands_TWRP_local_path)/bootloader_message/Android.mk
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
    include $(commands_TWRP_local_path)/mtp/ffs/Android.mk
else
    include $(commands_TWRP_local_path)/mtp/legacy/Android.mk
endif

ifeq ($(wildcard system/core/uncrypt/Android.mk),)
    #include $(commands_TWRP_local_path)/uncrypt/Android.mk
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 26; echo $$?),0)
        TARGET_GLOBAL_CFLAGS += -DTW_USE_MINUI_WITH_DATA
        CLANG_TARGET_GLOBAL_CFLAGS += -DTW_USE_MINUI_WITH_DATA
    endif
    include $(commands_TWRP_local_path)/minadbd/Android.mk \
        $(commands_TWRP_local_path)/minui/Android.mk
else
    TARGET_GLOBAL_CFLAGS += -DTW_USE_MINUI_21
    include $(commands_TWRP_local_path)/minadbd21/Android.mk \
        $(commands_TWRP_local_path)/minui21/Android.mk
endif

#$(commands_TWRP_local_path)/otautil/Android.mk
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
    $(commands_TWRP_local_path)/toolbox/Android.mk \
    $(commands_TWRP_local_path)/twrpTarMain/Android.mk \
    $(commands_TWRP_local_path)/minzip/Android.mk \
    $(commands_TWRP_local_path)/dosfstools/Android.mk \
    $(commands_TWRP_local_path)/etc/Android.mk \
    $(commands_TWRP_local_path)/toybox/Android.mk \
    $(commands_TWRP_local_path)/simg2img/Android.mk \
    $(commands_TWRP_local_path)/adbbu/Android.mk \
    $(commands_TWRP_local_path)/libpixelflinger/Android.mk \
    $(commands_TWRP_local_path)/twrpDigest/Android.mk \
    $(commands_TWRP_local_path)/attr/Android.mk

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    include $(commands_TWRP_local_path)/libmincrypt/Android.mk
endif

ifneq ($(TW_OZIP_DECRYPT_KEY),)
    TWRP_REQUIRED_MODULES += ozip_decrypt
    include $(commands_TWRP_local_path)/ozip_decrypt/Android.mk
endif

ifeq ($(TW_INCLUDE_CRYPTO), true)
    include $(commands_TWRP_local_path)/crypto/fde/Android.mk
    include $(commands_TWRP_local_path)/crypto/scrypt/Android.mk
    ifeq ($(TW_INCLUDE_CRYPTO_FBE), true)
        include $(commands_TWRP_local_path)/crypto/ext4crypt/Android.mk
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
