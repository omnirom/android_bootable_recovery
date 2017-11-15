# Copyright 2005 The Android Open Source Project
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

LOCAL_PATH:= $(call my-dir)

minadbd_cflags := \
    -Wall -Werror \
    -DADB_HOST=0 \

# libminadbd (static library)
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    fuse_adb_provider.cpp \
    minadbd.cpp \
    minadbd_services.cpp \

LOCAL_MODULE := libminadbd
LOCAL_CFLAGS := $(minadbd_cflags)
LOCAL_C_INCLUDES := bootable/recovery system/core/adb
LOCAL_WHOLE_STATIC_LIBRARIES := libadbd
LOCAL_STATIC_LIBRARIES := libcrypto libbase

include $(BUILD_STATIC_LIBRARY)

# minadbd_test (native test)
# ===============================
include $(CLEAR_VARS)

LOCAL_MODULE := minadbd_test
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_SRC_FILES := fuse_adb_provider_test.cpp
LOCAL_CFLAGS := $(minadbd_cflags)
LOCAL_C_INCLUDES := $(LOCAL_PATH) system/core/adb
LOCAL_STATIC_LIBRARIES := \
    libBionicGtestMain \
    libminadbd
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libbase \
    libcutils

include $(BUILD_NATIVE_TEST)
