ifneq ($(TARGET_SIMULATOR),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    src/boot.c \
    src/check.c \
    src/common.c \
    src/fat.c \
    src/file.c \
    src/io.c \
    src/lfn.c \
    src/fsck.fat.c

LOCAL_SHARED_LIBRARIES := libc
LOCAL_CFLAGS += -D_USING_BIONIC_
LOCAL_CFLAGS += -DUSE_ANDROID_RETVALS
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_MODULE = fsck.fat
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := \
    src/boot.c \
    src/check.c \
    src/common.c \
    src/fat.c \
    src/file.c \
    src/io.c \
    src/lfn.c \
    src/fatlabel.c

LOCAL_C_INCLUDES += bionic/libc/kernel/common
LOCAL_SHARED_LIBRARIES := libc
LOCAL_CFLAGS += -D_USING_BIONIC_
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_MODULE = fatlabel
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := src/mkfs.fat.c

LOCAL_SHARED_LIBRARIES := libc
LOCAL_CFLAGS += -D_USING_BIONIC_
LOCAL_CFLAGS += -Wno-sign-compare
LOCAL_MODULE = mkfs.fat
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
include $(BUILD_EXECUTABLE)

endif
