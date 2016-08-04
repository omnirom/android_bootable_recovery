LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	twrpback.cpp \
	../twrpDigest.cpp \
	../digest/md5.c
LOCAL_SHARED_LIBRARIES += libstdc++ libz
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif

LOCAL_C_INCLUDES += bionic external/zlib
LOCAL_CFLAGS:= -c -W
LOCAL_MODULE:= twrpbu
LOCAL_MODULE_STEM := bu
LOCAL_MODULE_TAGS:= eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := libtwadbbu
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64 -fno-strict-aliasing
LOCAL_C_INCLUDES += bionic external/zlib
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
endif

LOCAL_SRC_FILES = \
    libtwadbbu.cpp

LOCAL_SHARED_LIBRARIES += libz libc libstdc++

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
else
    LOCAL_SHARED_LIBRARIES += libc++
endif

include $(BUILD_SHARED_LIBRARY)
