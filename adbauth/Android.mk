LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libadbauth
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES = \
    adb_auth.cpp
LOCAL_SHARED_LIBRARIES +=

include $(BUILD_STATIC_LIBRARY)
