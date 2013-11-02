# Copyright 2008 The Android Open Source Project
#
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libmincrypttwrp
LOCAL_C_INCLUDES := bootable/recovery/libmincrypt/includes
LOCAL_SRC_FILES := rsa.c sha.c sha256.c
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libmincrypttwrp
LOCAL_SRC_FILES := rsa.c sha.c sha256.c
include $(BUILD_HOST_STATIC_LIBRARY)

