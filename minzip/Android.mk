LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	Hash.c \
	SysUtil.c \
	DirUtil.c \
	Inlines.c \
	Zip.c

LOCAL_C_INCLUDES := \
	external/zlib \
	external/safe-iop/include

LOCAL_C_INCLUDES += external/libselinux/include
LOCAL_SHARED_LIBRARIES += libselinux

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE := libminzip

LOCAL_SHARED_LIBRARIES += libz

LOCAL_CLANG := true

LOCAL_CFLAGS += -Werror -Wall

include $(BUILD_SHARED_LIBRARY)


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

LOCAL_C_INCLUDES += external/libselinux/include
LOCAL_STATIC_LIBRARIES += libselinux

LOCAL_CFLAGS += -DPLATFORM_SDK_VERSION=$(PLATFORM_SDK_VERSION)

LOCAL_MODULE := libminzip

LOCAL_STATIC_LIBRARIES += libz

LOCAL_CLANG := true

LOCAL_CFLAGS += -Werror -Wall

include $(BUILD_STATIC_LIBRARY)
