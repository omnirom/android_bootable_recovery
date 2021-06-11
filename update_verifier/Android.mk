# Copyright (C) 2015 The Android Open Source Project
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

# libupdate_verifier (static library)
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    update_verifier.cpp

LOCAL_MODULE := libupdate_verifier

LOCAL_STATIC_LIBRARIES := \
    libotautil

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcutils \
    android.hardware.boot@1.0

LOCAL_CFLAGS := -Wall -Werror

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/include

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include

ifeq ($(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_SUPPORTS_VERITY),true)
LOCAL_CFLAGS += -DPRODUCT_SUPPORTS_VERITY=1
endif

ifeq ($(BOARD_AVB_ENABLE),true)
LOCAL_CFLAGS += -DBOARD_AVB_ENABLE=1
endif

include $(BUILD_STATIC_LIBRARY)

# update_verifier (executable)
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    update_verifier_main.cpp

LOCAL_MODULE := update_verifier
LOCAL_STATIC_LIBRARIES := \
    libupdate_verifier \
    libotautil

LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcutils \
    libhardware \
    liblog \
    libutils \
    libhidlbase \
    android.hardware.boot@1.0

LOCAL_CFLAGS := -Wall -Werror

LOCAL_INIT_RC := update_verifier.rc

include $(BUILD_EXECUTABLE)
