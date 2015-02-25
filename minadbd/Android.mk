# Copyright 2005 The Android Open Source Project

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    adb_main.c \
    fuse_adb_provider.c \
    services.c \

LOCAL_CFLAGS := \
    -Wall -Werror \
    -Wno-unused-parameter \
    -Wimplicit-function-declaration \
    -DADB_HOST=0 \

LOCAL_C_INCLUDES := bootable/recovery system/core/adb
LOCAL_WHOLE_STATIC_LIBRARIES := libadbd

LOCAL_MODULE := libminadbd

include $(BUILD_STATIC_LIBRARY)
