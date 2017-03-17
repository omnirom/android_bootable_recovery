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
LOCAL_SRC_FILES := \
    applypatch.cpp \
    bspatch.cpp \
    freecache.cpp \
    imgpatch.cpp
LOCAL_MODULE := libapplypatch
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    bootable/recovery
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES := \
    libotafault \
    libbase \
    libcrypto \
    libbspatch \
    libbz \
    libz
LOCAL_CFLAGS := \
    -DZLIB_CONST \
    -Werror
include $(BUILD_STATIC_LIBRARY)

# libimgpatch (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    bspatch.cpp \
    imgpatch.cpp
LOCAL_MODULE := libimgpatch
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    bootable/recovery
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES := \
    libcrypto \
    libbspatch \
    libbase \
    libbz \
    libz
LOCAL_CFLAGS := \
    -DZLIB_CONST \
    -Werror
include $(BUILD_STATIC_LIBRARY)

# libimgpatch (host static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    bspatch.cpp \
    imgpatch.cpp
LOCAL_MODULE := libimgpatch
LOCAL_MODULE_HOST_OS := linux
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    bootable/recovery
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES := \
    libcrypto \
    libbspatch \
    libbase \
    libbz \
    libz
LOCAL_CFLAGS := \
    -DZLIB_CONST \
    -Werror
include $(BUILD_HOST_STATIC_LIBRARY)

# libapplypatch_modes (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    applypatch_modes.cpp
LOCAL_MODULE := libapplypatch_modes
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_STATIC_LIBRARIES := \
    libapplypatch \
    libbase \
    libedify \
    libcrypto
LOCAL_CFLAGS := -Werror
include $(BUILD_STATIC_LIBRARY)

# applypatch (target executable)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := applypatch_main.cpp
LOCAL_MODULE := applypatch
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_STATIC_LIBRARIES := \
    libapplypatch_modes \
    libapplypatch \
    libbase \
    libedify \
    libotafault \
    libcrypto \
    libbspatch \
    libbz
LOCAL_SHARED_LIBRARIES := \
    libbase \
    libz \
    libcutils
LOCAL_CFLAGS := -Werror
include $(BUILD_EXECUTABLE)

libimgdiff_src_files := imgdiff.cpp

# libbsdiff is compiled with -D_FILE_OFFSET_BITS=64.
libimgdiff_cflags := \
    -Werror \
    -D_FILE_OFFSET_BITS=64

libimgdiff_static_libraries := \
    libbsdiff \
    libdivsufsort \
    libdivsufsort64 \
    libziparchive \
    libutils \
    liblog \
    libbase \
    libz

# libimgdiff (static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    $(libimgdiff_src_files)
LOCAL_MODULE := libimgdiff
LOCAL_CFLAGS := \
    $(libimgdiff_cflags)
LOCAL_STATIC_LIBRARIES := \
    $(libimgdiff_static_libraries)
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
include $(BUILD_STATIC_LIBRARY)

# libimgdiff (host static library)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    $(libimgdiff_src_files)
LOCAL_MODULE := libimgdiff
LOCAL_CFLAGS := \
    $(libimgdiff_cflags)
LOCAL_STATIC_LIBRARIES := \
    $(libimgdiff_static_libraries)
LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
include $(BUILD_HOST_STATIC_LIBRARY)

# imgdiff (host static executable)
# ===============================
include $(CLEAR_VARS)
LOCAL_SRC_FILES := imgdiff_main.cpp
LOCAL_MODULE := imgdiff
LOCAL_CFLAGS := -Werror
LOCAL_STATIC_LIBRARIES := \
    libimgdiff \
    $(libimgdiff_static_libraries) \
    libbz
include $(BUILD_HOST_EXECUTABLE)
