LOCAL_PATH := $(call my-dir)

# Build libgpt_twrp library

include $(CLEAR_VARS)
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 26; echo $$?),0)
LOCAL_CLANG := false
endif
LOCAL_MODULE := libgpt_twrp
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES = \
    gpt.c \
    gptcrc32.c

LOCAL_CFLAGS := -Wno-format -Wno-format-security

LOCAL_SHARED_LIBRARIES := libc
include $(BUILD_SHARED_LIBRARY)
