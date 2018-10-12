# Copyright (C) 2016 The Android Open Source Project
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
LOCAL_CLANG := true
LOCAL_SRC_FILES := bootloader_message.cpp
LOCAL_MODULE := libbootloader_message_twrp
LOCAL_C_INCLUDES += bionic $(LOCAL_PATH)/include
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 21; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_C_INCLUDES += external/libcxx/include
    LOCAL_SHARED_LIBRARIES += libc++
endif
LOCAL_CFLAGS := -Werror -std=c++11
# ignore bootloader's factory reset command even when written to /misc
ifeq ($(TW_IGNORE_MISC_WIPE_DATA), true)
    LOCAL_CFLAGS += -DIGNORE_MISC_WIPE_DATA
endif
ifeq ($(BOOTLOADER_MESSAGE_OFFSET),)
    LOCAL_CFLAGS += -DBOARD_RECOVERY_BLDRMSG_OFFSET=0
else
    LOCAL_CFLAGS += -DBOARD_RECOVERY_BLDRMSG_OFFSET=$(BOOTLOADER_MESSAGE_OFFSET)
endif
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
include $(BUILD_SHARED_LIBRARY)
