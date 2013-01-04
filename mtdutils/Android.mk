ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c 

ifeq ($(TARGET_BOARD_PLATFORM),rk30xx)
LOCAL_SRC_FILES += rk30hack.c
endif

LOCAL_MODULE := libmtdutils
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_STATIC_LIBRARY)

endif	# !TARGET_SIMULATOR

