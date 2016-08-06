LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	Hash.cpp \
	SysUtil.cpp \
	DirUtil.cpp \
	Inlines.c \
	Zip.cpp

LOCAL_C_INCLUDES := \
	external/zlib \
	external/safe-iop/include

LOCAL_STATIC_LIBRARIES := libselinux libbase

LOCAL_MODULE := libminzip

LOCAL_CLANG := true

LOCAL_CFLAGS += -Werror -Wall

include $(BUILD_STATIC_LIBRARY)
