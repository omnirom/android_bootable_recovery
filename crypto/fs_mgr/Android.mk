# Copyright 2011 The Android Open Source Project
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= fs_mgr.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_MODULE:= libfs_mgrtwrp
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

include $(BUILD_STATIC_LIBRARY)



#include $(CLEAR_VARS)

#LOCAL_SRC_FILES:= fs_mgr_main.c

#LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

#LOCAL_MODULE:= fs_mgr

#LOCAL_MODULE_TAGS := optional
#LOCAL_FORCE_STATIC_EXECUTABLE := true
#LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)/sbin
#LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

#LOCAL_STATIC_LIBRARIES := libfs_mgr libcutils libc

#include $(BUILD_EXECUTABLE)

endif
