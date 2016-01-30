LOCAL_PATH := system/core/libsparse

include $(CLEAR_VARS)
LOCAL_SRC_FILES := simg2img.c \
	sparse_crc32.c
LOCAL_MODULE := simg2img_twrp
LOCAL_MODULE_STEM := simg2img
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := \
    libsparse \
    libz
LOCAL_CFLAGS := -Werror
include $(BUILD_EXECUTABLE)
