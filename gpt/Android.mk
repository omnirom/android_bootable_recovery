LOCAL_PATH := $(call my-dir)

# Build libgpt_twrp library

include $(CLEAR_VARS)
LOCAL_CLANG := false
LOCAL_MODULE := libgpt_twrp
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES = \
    gpt.c \
    gptcrc32.c

LOCAL_SHARED_LIBRARIES := libc
include $(BUILD_SHARED_LIBRARY)
