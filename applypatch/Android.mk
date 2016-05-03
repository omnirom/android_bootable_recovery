# Copyright (C) 2008 The Android Open Source Project
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

# libapplypatch (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_SRC_FILES := \
    applypatch.cpp \
    bspatch.cpp \
    freecache.cpp \
    imgpatch.cpp \
    utils.cpp
LOCAL_MODULE := libapplypatch
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \
    bootable/recovery
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES += \
    libotafault \
    libmtdutils \
    libbase \
    libcrypto_static \
    libbz \
    libz
include $(BUILD_STATIC_LIBRARY)

# libimgpatch (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_SRC_FILES := bspatch.cpp imgpatch.cpp utils.cpp
LOCAL_MODULE := libimgpatch
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \
    bootable/recovery
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES += libcrypto_static libbz libz
include $(BUILD_STATIC_LIBRARY)

# libimgpatch (host static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_SRC_FILES := bspatch.cpp imgpatch.cpp utils.cpp
LOCAL_MODULE := libimgpatch
LOCAL_MODULE_HOST_OS := linux
LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/include \
    bootable/recovery
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES += libcrypto_static libbz libz
include $(BUILD_HOST_STATIC_LIBRARY)

# applypatch (executable)
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_SRC_FILES := main.cpp
LOCAL_MODULE := applypatch
LOCAL_C_INCLUDES += bootable/recovery
LOCAL_STATIC_LIBRARIES += \
    libapplypatch \
    libbase \
    libedify \
    libotafault \
    libminzip \
    libmtdutils \
    libcrypto_static \
    libbz
LOCAL_SHARED_LIBRARIES += libz libcutils libc
include $(BUILD_EXECUTABLE)

# imgdiff (host static executable)
# ===============================
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_SRC_FILES := imgdiff.cpp utils.cpp
LOCAL_MODULE := imgdiff
LOCAL_STATIC_LIBRARIES += \
    libbsdiff \
    libbz \
    libdivsufsort64 \
    libdivsufsort \
    libz
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_HOST_EXECUTABLE)
