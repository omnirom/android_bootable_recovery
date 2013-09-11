LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libtar
LOCAL_MODULE_TAGS := eng
LOCAL_MODULES_TAGS = optional
LOCAL_CFLAGS = 
LOCAL_SRC_FILES = append.c block.c decode.c encode.c extract.c handle.c output.c util.c wrapper.c basename.c strmode.c libtar_hash.c libtar_list.c dirname.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					external/zlib
LOCAL_SHARED_LIBRARIES += libz libc

ifeq ($(TWHAVE_SELINUX), true)
	LOCAL_C_INCLUDES += external/libselinux/include
	LOCAL_SHARED_LIBRARIES += libselinux
	LOCAL_CFLAGS += -DHAVE_SELINUX
endif

include $(BUILD_SHARED_LIBRARY)

