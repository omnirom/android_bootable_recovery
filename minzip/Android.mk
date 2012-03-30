LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	Hash.c \
	SysUtil.c \
	DirUtil.c \
	Inlines.c \
	Zip.c

LOCAL_C_INCLUDES += \
	external/zlib \
	external/safe-iop/include

ifeq ($(HAVE_SELINUX),true)
LOCAL_C_INCLUDES += external/libselinux/include
LOCAL_STATIC_LIBRARIES += libselinux
LOCAL_CFLAGS += -DHAVE_SELINUX
endif

LOCAL_MODULE := libminzip

LOCAL_CFLAGS += -Wall

include $(BUILD_STATIC_LIBRARY)
