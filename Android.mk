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

include $(CLEAR_VARS)

LOCAL_SRC_FILES := fuse_sideload.cpp
LOCAL_CLANG := true
LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE

LOCAL_MODULE := libfusesideload

LOCAL_STATIC_LIBRARIES := libcutils libc libcrypto_static
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    adb_install.cpp \
    asn1_decoder.cpp \
    device.cpp \
    fuse_sdcard_provider.cpp \
    install.cpp \
    recovery.cpp \
    roots.cpp \
    screen_ui.cpp \
    ui.cpp \
    verifier.cpp \
    wear_ui.cpp \
    wear_touch.cpp \

LOCAL_MODULE := recovery

LOCAL_FORCE_STATIC_EXECUTABLE := true

ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
ifeq ($(HOST_OS),linux)
LOCAL_REQUIRED_MODULES := mkfs.f2fs
endif
endif

RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CLANG := true

LOCAL_C_INCLUDES += \
    system/vold \
    system/extras/ext4_utils \
    system/core/adb \

LOCAL_STATIC_LIBRARIES := \
    libbatterymonitor \
    libbootloader_message \
    libext4_utils_static \
    libsparse_static \
    libminzip \
    libz \
    libmtdutils \
    libminadbd \
    libfusesideload \
    libminui \
    libpng \
    libfs_mgr \
    libcrypto_static \
    libbase \
    libcutils \
    libutils \
    liblog \
    libselinux \
    libm \
    libc

LOCAL_HAL_STATIC_LIBRARIES := libhealthd

ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    LOCAL_CFLAGS += -DUSE_EXT4
    LOCAL_C_INCLUDES += system/extras/ext4_utils
    LOCAL_STATIC_LIBRARIES += libext4_utils_static libz
endif

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
endif

LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

ifeq ($(TARGET_RECOVERY_UI_LIB),)
  LOCAL_SRC_FILES += default_device.cpp
else
  LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_UI_LIB)
endif

ifeq ($(BOARD_CACHEIMAGE_PARTITION_SIZE),)
LOCAL_REQUIRED_MODULES := recovery-persist recovery-refresh
endif

include $(BUILD_EXECUTABLE)

# recovery-persist (system partition dynamic executable run after /data mounts)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := recovery-persist.cpp
LOCAL_MODULE := recovery-persist
LOCAL_SHARED_LIBRARIES := liblog libbase
LOCAL_CFLAGS := -Werror
LOCAL_INIT_RC := recovery-persist.rc
include $(BUILD_EXECUTABLE)

# recovery-refresh (system partition dynamic executable run at init)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := recovery-refresh.cpp
LOCAL_MODULE := recovery-refresh
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_CFLAGS := -Werror
LOCAL_INIT_RC := recovery-refresh.rc
include $(BUILD_EXECUTABLE)

# All the APIs for testing
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_MODULE := libverifier
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := \
    asn1_decoder.cpp \
    verifier.cpp \
    ui.cpp
LOCAL_STATIC_LIBRARIES := libcrypto_static
include $(BUILD_STATIC_LIBRARY)

include \
    $(LOCAL_PATH)/applypatch/Android.mk \
    $(LOCAL_PATH)/bootloader_message/Android.mk \
    $(LOCAL_PATH)/edify/Android.mk \
    $(LOCAL_PATH)/minui/Android.mk \
    $(LOCAL_PATH)/minzip/Android.mk \
    $(LOCAL_PATH)/minadbd/Android.mk \
    $(LOCAL_PATH)/mtdutils/Android.mk \
    $(LOCAL_PATH)/otafault/Android.mk \
    $(LOCAL_PATH)/tests/Android.mk \
    $(LOCAL_PATH)/tools/Android.mk \
    $(LOCAL_PATH)/uncrypt/Android.mk \
    $(LOCAL_PATH)/updater/Android.mk \
    $(LOCAL_PATH)/update_verifier/Android.mk \
