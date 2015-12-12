LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
	orscmd.cpp
LOCAL_CFLAGS:= -c -W
LOCAL_MODULE:=orscmd
LOCAL_MODULE_STEM := twrp
LOCAL_MODULE_TAGS:= eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
	twrpback.cpp ../twrpDigest.cpp ../digest/md5.c
LOCAL_SHARED_LIBRARIES += libstdc++ libz
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libstlport
else
	LOCAL_SHARED_LIBRARIES += libc++
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
	LOCAL_C_INCLUDES += external/stlport/stlport
endif   
LOCAL_C_INCLUDES += bionic external/zlib
LOCAL_CFLAGS:= -c -W
LOCAL_MODULE:= twrpbu
LOCAL_MODULE_STEM := bu
LOCAL_MODULE_TAGS:= eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
include $(BUILD_EXECUTABLE)
