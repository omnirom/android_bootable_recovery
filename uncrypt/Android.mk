# Copyright (C) 2014 The Android Open Source Project
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
LOCAL_SRC_FILES := bootloader_message_writer.cpp
LOCAL_MODULE := libbootloader_message_writer
LOCAL_STATIC_LIBRARIES := libbase libfs_mgr
LOCAL_C_INCLUDES := $(LOCAL_PATH)/..
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_CLANG := true

LOCAL_SRC_FILES := uncrypt.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)/..

LOCAL_MODULE := uncrypt

LOCAL_STATIC_LIBRARIES := libbootloader_message_writer libbase \
                          liblog libfs_mgr libcutils \

LOCAL_INIT_RC := uncrypt.rc

include $(BUILD_EXECUTABLE)
