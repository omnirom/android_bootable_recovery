LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    gpt.c \
    gptcrc32.c

LOCAL_SHARED_LIBRARIES := libc

LOCAL_CLANG := false
LOCAL_MODULE := libgpt_twrp
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
