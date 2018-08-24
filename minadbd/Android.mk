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
    -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

# libminadbd (static library)
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    fuse_adb_provider.cpp \
    ../fuse_sideload.cpp \
    minadbd.cpp \
    minadbd_services.cpp \

LOCAL_MODULE := libminadbd
LOCAL_CFLAGS := $(minadbd_cflags) -Wno-unused-parameter
LOCAL_CONLY_FLAGS := -Wimplicit-function-declaration
LOCAL_C_INCLUDES := $(LOCAL_PATH)/.. system/core/adb
LOCAL_WHOLE_STATIC_LIBRARIES := libadbd
LOCAL_SHARED_LIBRARIES := libbase liblog libcutils libc

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/libmincrypt/includes
    LOCAL_SHARED_LIBRARIES += libmincrypttwrp
    LOCAL_CFLAGS += -DUSE_MINCRYPT
else
    LOCAL_SHARED_LIBRARIES += libcrypto \
    $(if $(WITH_CRYPTO_UTILS),libcrypto_utils)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 27; echo $$?),0)
        # Needed in Android 9.0
        LOCAL_WHOLE_STATIC_LIBRARIES += libasyncio
    endif
endif

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    fuse_adb_provider.cpp \
    ../fuse_sideload.cpp \
    minadbd.cpp \
    minadbd_services.cpp \

LOCAL_CLANG := true
LOCAL_MODULE := libminadbd
LOCAL_CFLAGS := $(minadbd_cflags) -Wno-unused-parameter
LOCAL_CONLY_FLAGS := -Wimplicit-function-declaration
LOCAL_C_INCLUDES := $(LOCAL_PATH)/.. system/core/adb
LOCAL_WHOLE_STATIC_LIBRARIES := libadbd
LOCAL_STATIC_LIBRARIES := libbase liblog libcutils libc

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 24; echo $$?),0)
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/libmincrypt/includes
    LOCAL_SHARED_LIBRARIES += libmincrypttwrp
    LOCAL_CFLAGS += -DUSE_MINCRYPT
else
    LOCAL_SHARED_LIBRARIES += libcrypto \
    $(if $(WITH_CRYPTO_UTILS),libcrypto_utils)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 27; echo $$?),0)
        # Needed in Android 9.0
        LOCAL_WHOLE_STATIC_LIBRARIES += libasyncio
    endif
endif

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
