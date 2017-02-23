LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    popen.c \
    system.c

LOCAL_MODULE := libcrecovery
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    popen.c \
    system.c

LOCAL_MODULE := libcrecovery
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
