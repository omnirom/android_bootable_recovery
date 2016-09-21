# Copyright 2009 The Android Open Source Project

LOCAL_PATH := $(call my-dir)

edify_src_files := \
	lexer.ll \
	parser.yy \
	expr.cpp

#
# Build the host-side command line tool
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
		$(edify_src_files) \
		main.cpp

LOCAL_CPPFLAGS := -g -O0
LOCAL_MODULE := edify
LOCAL_YACCFLAGS := -v
LOCAL_CPPFLAGS += -Wno-unused-parameter
LOCAL_CPPFLAGS += -Wno-deprecated-register
LOCAL_CLANG := true
LOCAL_C_INCLUDES += $(LOCAL_PATH)/..
LOCAL_STATIC_LIBRARIES += libbase

include $(BUILD_HOST_EXECUTABLE)

#
# Build the device-side library
#
include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(edify_src_files)

LOCAL_CPPFLAGS := -Wno-unused-parameter
LOCAL_CPPFLAGS += -Wno-deprecated-register
LOCAL_MODULE := libedify
LOCAL_CLANG := true
LOCAL_C_INCLUDES += $(LOCAL_PATH)/..
LOCAL_STATIC_LIBRARIES += libbase

include $(BUILD_STATIC_LIBRARY)
