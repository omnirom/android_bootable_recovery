# Copyright (C) 2015 TeamWin Recovery Project
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

ifneq ($(TW_EXCLUDE_DEFAULT_USB_INIT), true)

include $(CLEAR_VARS)
LOCAL_MODULE := init.recovery.usb.rc
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES

# Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
# during ramdisk creation and only allows init.recovery.*.rc files to be copied
# from TARGET_ROOT_OUT thereafter
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

LOCAL_SRC_FILES := $(LOCAL_MODULE)
include $(BUILD_PREBUILT)

endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 22; echo $$?),0)
    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.service.rc
    LOCAL_MODULE_TAGS := eng
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := init.recovery.service22.rc
    include $(BUILD_PREBUILT)
else
    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.service.rc
    LOCAL_MODULE_TAGS := eng
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := init.recovery.service21.rc
    include $(BUILD_PREBUILT)
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.hlthchrg.rc
    LOCAL_MODULE_TAGS := eng
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := init.recovery.hlthchrg26.rc
    include $(BUILD_PREBUILT)

    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.ldconfig.rc
    LOCAL_MODULE_TAGS := eng
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := init.recovery.ldconfig.rc
    include $(BUILD_PREBUILT)
else
    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.hlthchrg.rc
    LOCAL_MODULE_TAGS := eng
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := init.recovery.hlthchrg25.rc
    include $(BUILD_PREBUILT)
endif

ifeq ($(TWRP_INCLUDE_LOGCAT), true)
    ifeq ($(TARGET_USES_LOGD), true)

        include $(CLEAR_VARS)
        LOCAL_MODULE := init.recovery.logd.rc
        LOCAL_MODULE_TAGS := eng
        LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES

        # Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
        # during ramdisk creation and only allows init.recovery.*.rc files to be copied
        # from TARGET_ROOT_OUT thereafter
        LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

        LOCAL_SRC_FILES := $(LOCAL_MODULE)
        include $(BUILD_PREBUILT)
    endif
endif

ifeq ($(TW_USE_TOOLBOX), true)
    include $(CLEAR_VARS)
    LOCAL_MODULE := init.recovery.mksh.rc
    LOCAL_MODULE_TAGS := eng
    LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES

    # Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
    # during ramdisk creation and only allows init.recovery.*.rc files to be copied
    # from TARGET_ROOT_OUT thereafter
    LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

    LOCAL_SRC_FILES := $(LOCAL_MODULE)
    include $(BUILD_PREBUILT)
endif
