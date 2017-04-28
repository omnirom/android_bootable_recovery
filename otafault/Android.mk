# Copyright 2015 The Android Open Source Project
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
# See the License for the specific languae governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

ifdef project-path-for
    RECOVERY_PATH := $(call project-path-for,recovery)
else
    RECOVERY_PATH := bootable/recovery
endif

include $(CLEAR_VARS)

otafault_static_libs := \
    libbase \
    libminzip \
    libz \
    libselinux

LOCAL_SRC_FILES := config.cpp ota_io.cpp
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := libotafault
LOCAL_CLANG := true
LOCAL_C_INCLUDES := $(RECOVERY_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)
LOCAL_WHOLE_STATIC_LIBRARIES := $(otafault_static_libs)

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := config.cpp ota_io.cpp test.cpp
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE := otafault_test
LOCAL_STATIC_LIBRARIES := $(otafault_static_libs)
LOCAL_C_INCLUDES := $(RECOVERY_PATH)
LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
