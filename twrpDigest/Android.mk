LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := libtwrpdigest
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS = -fno-strict-aliasing
LOCAL_C_INCLUDES := external/openssl/include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
	LOCAL_C_INCLUDES += external/stlport/stlport
endif

LOCAL_SHARED_LIBRARIES += libc libstdc++ 

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
	LOCAL_SHARED_LIBRARIES += libstlport
else
	LOCAL_SHARED_LIBRARIES += libc++ libcrypto
endif

LOCAL_SRC_FILES = \
	twrpDigest.cpp \
	twrpMD5.cpp \
	twrpSHA.cpp \
	digest/md5/md5.c \

include $(BUILD_SHARED_LIBRARY)
