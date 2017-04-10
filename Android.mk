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

# Needed by build/make/core/Makefile.
RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2

# libfusesideload (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := fuse_sideload.cpp
LOCAL_CLANG := true
LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter -Werror
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE
LOCAL_MODULE := libfusesideload
LOCAL_STATIC_LIBRARIES := libcutils libc libcrypto
include $(BUILD_STATIC_LIBRARY)

# libmounts (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := mounts.cpp
LOCAL_CLANG := true
LOCAL_CFLAGS := -Wall -Wno-unused-parameter -Werror
LOCAL_MODULE := libmounts
include $(BUILD_STATIC_LIBRARY)

# librecovery (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    install.cpp
LOCAL_CFLAGS := -Wno-unused-parameter -Werror
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)

ifeq ($(AB_OTA_UPDATER),true)
    LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
endif

LOCAL_MODULE := librecovery
LOCAL_STATIC_LIBRARIES := \
    libminui \
    libvintf_recovery \
    libcrypto_utils \
    libcrypto \
    libbase

include $(BUILD_STATIC_LIBRARY)

# recovery (static executable)
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    adb_install.cpp \
    asn1_decoder.cpp \
    device.cpp \
    fuse_sdcard_provider.cpp \
    recovery.cpp \
    roots.cpp \
    rotate_logs.cpp \
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

LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wno-unused-parameter -Werror
LOCAL_CLANG := true

LOCAL_C_INCLUDES += \
    system/vold \
    system/core/adb \

LOCAL_STATIC_LIBRARIES := \
    librecovery \
    libbatterymonitor \
    libbootloader_message \
    libext4_utils \
    libsparse \
    libziparchive \
    libotautil \
    libmounts \
    libz \
    libminadbd \
    libfusesideload \
    libminui \
    libpng \
    libfs_mgr \
    libcrypto_utils \
    libcrypto \
    libvintf_recovery \
    libvintf \
    libtinyxml2 \
    libbase \
    libcutils \
    libutils \
    liblog \
    libselinux \
    libm \
    libc

LOCAL_HAL_STATIC_LIBRARIES := libhealthd

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
LOCAL_SRC_FILES := \
    recovery-persist.cpp \
    rotate_logs.cpp
LOCAL_MODULE := recovery-persist
LOCAL_SHARED_LIBRARIES := liblog libbase
LOCAL_CFLAGS := -Werror
LOCAL_INIT_RC := recovery-persist.rc
include $(BUILD_EXECUTABLE)

# recovery-refresh (system partition dynamic executable run at init)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    recovery-refresh.cpp \
    rotate_logs.cpp
LOCAL_MODULE := recovery-refresh
LOCAL_SHARED_LIBRARIES := liblog libbase
LOCAL_CFLAGS := -Werror
LOCAL_INIT_RC := recovery-refresh.rc
include $(BUILD_EXECUTABLE)

# libverifier (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_MODULE := libverifier
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := \
    asn1_decoder.cpp \
    verifier.cpp
LOCAL_STATIC_LIBRARIES := \
    libcrypto_utils \
    libcrypto \
    libbase
LOCAL_CFLAGS := -Werror
include $(BUILD_STATIC_LIBRARY)

include \
    $(LOCAL_PATH)/applypatch/Android.mk \
    $(LOCAL_PATH)/bootloader_message/Android.mk \
    $(LOCAL_PATH)/edify/Android.mk \
    $(LOCAL_PATH)/minadbd/Android.mk \
    $(LOCAL_PATH)/minui/Android.mk \
    $(LOCAL_PATH)/otafault/Android.mk \
    $(LOCAL_PATH)/otautil/Android.mk \
    $(LOCAL_PATH)/tests/Android.mk \
    $(LOCAL_PATH)/tools/Android.mk \
    $(LOCAL_PATH)/uncrypt/Android.mk \
    $(LOCAL_PATH)/updater/Android.mk \
    $(LOCAL_PATH)/update_verifier/Android.mk \
