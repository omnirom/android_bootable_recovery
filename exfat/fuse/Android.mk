LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := exfat-fuse
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64 -Wno-sign-compare -Wno-unused-parameter
LOCAL_SRC_FILES = main.c 
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					$(commands_recovery_local_path)/exfat/libexfat \
					$(commands_recovery_local_path)/fuse/include \
					$(commands_recovery_local_path)/fuse/android
LOCAL_SHARED_LIBRARIES := libexfat_twrp
LOCAL_STATIC_LIBRARIES := libfusetwrp

include $(BUILD_EXECUTABLE)
