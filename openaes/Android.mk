LOCAL_PATH := $(call my-dir)

# Executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := src/oaes.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/src/isaac

LOCAL_CFLAGS := -g -c -W

LOCAL_SHARED_LIBRARIES := \
    libopenaes \
    libc

LOCAL_MODULE := openaes
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)

# Shared library
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    src/ftime.c \
    src/isaac/rand.c \
    src/oaes_lib.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/src/isaac

LOCAL_SHARED_LIBRARIES := libc

LOCAL_MODULE := libopenaes
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# Static library
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    src/ftime.c \
    src/isaac/rand.c \
    src/oaes_lib.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/inc \
    $(LOCAL_PATH)/src/isaac

LOCAL_STATIC_LIBRARIES := libc

LOCAL_MODULE := libopenaes_static
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)
