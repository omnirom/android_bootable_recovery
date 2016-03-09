ifeq ($(TW_INCLUDE_CRYPTO_SAMSUNG), true)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libcrypt_samsung
LOCAL_SRC_FILES := libcrypt_samsung.c
LOCAL_MODULE_TAGS := eng

include $(BUILD_STATIC_LIBRARY)
endif
