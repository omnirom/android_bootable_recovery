LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := graphics.c events.c resources.c

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE := libminui

ifneq ($(RECOVERY_24_BIT),)
  LOCAL_CFLAGS += -DRECOVERY_24_BIT
endif

include $(BUILD_STATIC_LIBRARY)
