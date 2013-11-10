# Build the scrypt unit tests

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk

LOCAL_SRC_FILES:= \
    scrypt_test.cpp

LOCAL_C_INCLUDES := \
    external/gtest/include \
    external/scrypt/lib/crypto

LOCAL_SHARED_LIBRARIES := \
    libcrypto

LOCAL_STATIC_LIBRARIES := \
    libscrypt_static \
    libgtest \
    libgtest_main

LOCAL_MODULE := scrypt_test

include $(BUILD_NATIVE_TEST)
