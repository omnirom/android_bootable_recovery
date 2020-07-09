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
	fdevent.c \
	fuse_adb_provider.c \
	transport.c \
	transport_usb.c \
	sockets.c \
	services.c \
	usb_linux_client.c \
	utils.c

LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE
LOCAL_CFLAGS += -DUSE_FUSE_SIDELOAD22
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := libminadbd
LOCAL_C_INCLUDES += $(LOCAL_PATH)/../
LOCAL_SHARED_LIBRARIES := libfusesideload libcutils libc
include $(BUILD_SHARED_LIBRARY)
