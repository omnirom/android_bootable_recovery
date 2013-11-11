LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
LOCAL_SRC_FILES:= \
	getfooter.c
LOCAL_CFLAGS:= -g -c -W
LOCAL_MODULE:=getfooter
LOCAL_MODULE_TAGS:= eng
LOCAL_STATIC_LIBRARIES += libfs_mgrtwrp libc libcutils
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_C_INCLUDES := bootable/recovery/crypto/jb/
include $(BUILD_EXECUTABLE)
endif