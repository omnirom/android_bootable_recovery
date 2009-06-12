# Copyright 2009 The Android Open Source Project

LOCAL_PATH := $(call my-dir)

updater_src_files := \
	install.c \
	updater.c

#
# Build the device-side library
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(updater_src_files)

LOCAL_STATIC_LIBRARIES := libapplypatch libedify libmtdutils libminzip libz
LOCAL_STATIC_LIBRARIES += libmincrypt libbz
LOCAL_STATIC_LIBRARIES += libcutils libstdc++ libc
LOCAL_C_INCLUDES += $(LOCAL_PATH)/..

LOCAL_MODULE := updater

LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
