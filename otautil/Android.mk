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

libotautil_src_files := \
    SysUtil.cpp \
    DirUtil.cpp \
    ZipUtil.cpp \
    ThermalUtil.cpp \
    cache_location.cpp \
    rangeset.cpp

libotautil_static_libraries := \
    libselinux \
    libbase

libotautil_cflags := \
    -Werror \
    -Wall

libotautil_c_includes := include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 26; echo $$?),0)
# Android 8.1 header
libotautil_c_includes += \
    system/core/libziparchive/include
endif

include $(CLEAR_VARS)
LOCAL_MODULE := libotautil
LOCAL_C_INCLUDES := $(libotautil_c_includes)
LOCAL_CFLAGS := $(libotautil_cflags)
LOCAL_SRC_FILES := $(libotautil_src_files)
LOCAL_STATIC_LIBRARIES := $(libotautil_static_libraries)
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libotautil
LOCAL_C_INCLUDES := $(libotautil_c_includes)
LOCAL_CFLAGS := $(libotautil_cflags)
LOCAL_SRC_FILES := $(libotautil_src_files)
LOCAL_STATIC_LIBRARIES := $(libotautil_static_libraries)
include $(BUILD_HOST_STATIC_LIBRARY)
