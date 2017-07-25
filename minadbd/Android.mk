# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)

minadbd_cflags := \
    -Wall -Werror \
    -Wno-unused-parameter \
    -Wno-missing-field-initializers \
    -DADB_HOST=0 \

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    fuse_adb_provider.cpp \
    minadbd.cpp \
    minadbd_services.cpp \

LOCAL_MODULE := libminadbd
LOCAL_CFLAGS := $(minadbd_cflags)
LOCAL_CONLY_FLAGS := -Wimplicit-function-declaration
LOCAL_C_INCLUDES := bootable/recovery system/core/adb
LOCAL_WHOLE_STATIC_LIBRARIES := libadbd
LOCAL_STATIC_LIBRARIES := libcrypto libbase

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := minadbd_test
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_SRC_FILES := fuse_adb_provider_test.cpp
LOCAL_CFLAGS := $(minadbd_cflags)
LOCAL_C_INCLUDES := $(LOCAL_PATH) system/core/adb
LOCAL_STATIC_LIBRARIES := libminadbd
LOCAL_SHARED_LIBRARIES := liblog libbase libcutils

include $(BUILD_NATIVE_TEST)
