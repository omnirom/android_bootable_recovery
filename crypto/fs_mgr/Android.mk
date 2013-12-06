# Copyright 2011 The Android Open Source Project
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= fs_mgr.c fs_mgr_verity.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_MODULE:= libfs_mgrtwrp
LOCAL_SHARED_LIBRARIES := libext4_utils
LOCAL_STATIC_LIBRARIES := liblogwraptwrp libmincrypttwrp
LOCAL_C_INCLUDES += system/extras/ext4_utils bootable/recovery/libmincrypt/includes
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

include $(BUILD_STATIC_LIBRARY)


include $(CLEAR_VARS)

LOCAL_SRC_FILES:= fs_mgr_main.c

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_MODULE:= fs_mgrtwrp

LOCAL_MODULE_TAGS := optional
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)/sbin
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)

LOCAL_STATIC_LIBRARIES := libfs_mgrtwrp liblogwraptwrp libcutils liblog libc libmincrypttwrp libext4_utils_static

include $(BUILD_EXECUTABLE)

endif
