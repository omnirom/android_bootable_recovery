ifneq (,$(filter $(CM_PLATFORM_SDK_VERSION),4))

LOCAL_PATH := external/exfat/fuse
include $(CLEAR_VARS)

LOCAL_MODULE := mount.exfat
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64
LOCAL_SRC_FILES = main.c 
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
                    external/exfat/libexfat \
                    $(commands_recovery_local_path)/fuse/include
LOCAL_SHARED_LIBRARIES := libexfat
LOCAL_STATIC_LIBRARIES += libfusetwrp
include $(BUILD_EXECUTABLE)

else

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := exfat-fuse
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS = -D_FILE_OFFSET_BITS=64
LOCAL_SRC_FILES = main.c 
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
                    $(commands_recovery_local_path)/exfat/libexfat \
                    $(commands_recovery_local_path)/fuse/include
LOCAL_SHARED_LIBRARIES += libz libc libexfat libdl 
LOCAL_STATIC_LIBRARIES += libfusetwrp

include $(BUILD_EXECUTABLE)

endif
