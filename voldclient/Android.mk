LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libvoldclient
LOCAL_SRC_FILES := commands.cpp dispatcher.cpp event_loop.cpp
LOCAL_CFLAGS := -DMINIVOLD -Werror -Wno-unused-parameter
LOCAL_C_INCLUDES :=         	\
    $(KERNEL_HEADERS)			\
    $(LOCAL_PATH)/..    	\
    system/core/fs_mgr/include	\
    system/core/include     	\
    system/core/libcutils   	\
    system/vold
LOCAL_MODULE_TAGS := optional
include $(BUILD_STATIC_LIBRARY)
