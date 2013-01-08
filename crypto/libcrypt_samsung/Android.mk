LOCAL_PATH := $(call my-dir)

ifneq ($(TARGET_SIMULATOR),true)

include $(CLEAR_VARS)
LOCAL_MODULE := libcrypt_samsung
LOCAL_SRC_FILES := $(LOCAL_MODULE).c
LOCAL_MODULE_TAGS := eng
include $(BUILD_STATIC_LIBRARY)

endif
