LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libtwlogging
LOCAL_MODULE_TAGS := optional
LOCAL_SRC_FILES := twlogging.c
LOCAL_CFLAGS := -Werror
include $(BUILD_SHARED_LIBRARY)
