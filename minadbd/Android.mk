# Copyright 2005 The Android Open Source Project
#
# Android.mk for adb
#

LOCAL_PATH:= $(call my-dir)

# minadbd library
# =========================================================

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	adb.c \
	adb_main.c \
	fuse_adb_provider.c \
	sockets.c \
	services.c \

LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE
LOCAL_C_INCLUDES := bootable/recovery system/core/adb

LOCAL_MODULE := libminadbd

include $(BUILD_STATIC_LIBRARY)
