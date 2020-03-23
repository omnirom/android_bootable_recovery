LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := pigz
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS :=
LOCAL_SRC_FILES = pigz.c yarn.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					external/zlib
LOCAL_SHARED_LIBRARIES += libz libc

LOCAL_POST_INSTALL_CMD := \
    $(hide) mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin && \
    ln -sf pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gunzip && \
    ln -sf pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gzip && \
    ln -sf pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/unpigz

include $(BUILD_EXECUTABLE)
