# Build the scrypt unit tests

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_SRC_FILES:= \
    scrypt_test.cpp

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../lib/crypto

LOCAL_SHARED_LIBRARIES := \
    libcrypto

LOCAL_STATIC_LIBRARIES := \
    libscrypt_static \
    libgtest \
    libgtest_main

LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := scrypttwrp_test

include $(BUILD_NATIVE_TEST)
