ifneq ($(TARGET_SIMULATOR),true)
ifeq ($(TARGET_ARCH),arm)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c 

LOCAL_MODULE := libmtdutils
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_STATIC_LIBRARY)

endif	# TARGET_ARCH == arm
endif	# !TARGET_SIMULATOR

