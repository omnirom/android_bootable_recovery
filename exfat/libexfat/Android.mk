LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libexfat_twrp
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64 -Wno-sign-compare -Wno-address-of-packed-member -Wno-missing-field-initializers
LOCAL_SRC_FILES = cluster.c io.c log.c lookup.c mount.c node.c time.c utf.c utils.c
LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_SHARED_LIBRARIES += libc

include $(BUILD_SHARED_LIBRARY)
