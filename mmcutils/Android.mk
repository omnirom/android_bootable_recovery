LOCAL_PATH := $(call my-dir)

# Static library
include $(CLEAR_VARS)

LOCAL_SRC_FILES := mmcutils.c

LOCAL_MODULE := libmmcutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# Shared library
include $(CLEAR_VARS)

LOCAL_SRC_FILES := mmcutils.c

ifeq ($(BOARD_HAS_LARGE_FILESYSTEM),true)
    LOCAL_CFLAGS += -DBOARD_HAS_LARGE_FILESYSTEM
endif

LOCAL_MODULE := libmmcutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
