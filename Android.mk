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

# Needed by build/make/core/Makefile. Must be consistent with the value in Android.bp.
RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2

# TARGET_RECOVERY_UI_LIB should be one of librecovery_ui_{default,wear,vr,ethernet} or a
# device-specific module that defines make_device() and the exact RecoveryUI class for the
# target. It defaults to librecovery_ui_default, which uses ScreenRecoveryUI.
TARGET_RECOVERY_UI_LIB ?= librecovery_ui_default

# librecovery_ui_ext (shared library)
# ===================================
include $(CLEAR_VARS)

LOCAL_MODULE := librecovery_ui_ext

# LOCAL_MODULE_PATH for shared libraries is unsupported in multiarch builds.
LOCAL_MULTILIB := first

ifeq ($(TARGET_IS_64_BIT),true)
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/lib64
else
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/lib
endif

LOCAL_WHOLE_STATIC_LIBRARIES := \
    $(TARGET_RECOVERY_UI_LIB)

LOCAL_SHARED_LIBRARIES := \
    libbase \
    liblog \
    librecovery_ui.recovery

include $(BUILD_SHARED_LIBRARY)

# recovery_deps: A phony target that's depended on by `recovery`, which
# builds additional modules conditionally based on Makefile variables.
# ======================================================================
include $(CLEAR_VARS)

LOCAL_MODULE := recovery_deps

ifeq ($(TARGET_USERIMAGES_USE_F2FS),true)
ifeq ($(HOST_OS),linux)
LOCAL_REQUIRED_MODULES += \
    make_f2fs.recovery \
    sload_f2fs.recovery
endif
endif

# On A/B devices recovery-persist reads the recovery related file from the persist storage and
# copies them into /data/misc/recovery. Then, for both A/B and non-A/B devices, recovery-persist
# parses the last_install file and reports the embedded update metrics. Also, the last_install file
# will be deteleted after the report.
LOCAL_REQUIRED_MODULES += recovery-persist
ifeq ($(BOARD_CACHEIMAGE_PARTITION_SIZE),)
LOCAL_REQUIRED_MODULES += recovery-refresh
endif

include $(BUILD_PHONY_PACKAGE)

include \
    $(LOCAL_PATH)/updater/Android.mk \
