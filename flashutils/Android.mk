LOCAL_PATH := $(call my-dir)

# Static libflashutils
include $(CLEAR_VARS)

LOCAL_SRC_FILES := flashutils.c

LOCAL_STATIC_LIBRARIES := \
    libbmlutils \
    libcrecovery \
    libmmcutils \
    libmtdutils

BOARD_RECOVERY_DEFINES := \
    BOARD_BML_BOOT \
    BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
)

LOCAL_MODULE := libflashutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# Shared libflashutils
include $(CLEAR_VARS)

LOCAL_SRC_FILES := flashutils.c

LOCAL_C_INCLUDES := $(commands_recovery_local_path)

LOCAL_SHARED_LIBRARIES := \
    libbmlutils \
    libc \
    libcrecovery \
    libmmcutils \
    libmtdutils

BOARD_RECOVERY_DEFINES := \
    BOARD_BML_BOOT \
    BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
)

LOCAL_MODULE := libflashutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

# Static libflash_image
include $(CLEAR_VARS)

LOCAL_SRC_FILES := flash_image.c

LOCAL_CFLAGS := -Dmain=flash_image_main

LOCAL_MODULE := libflash_image
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# Static libdump_image
include $(CLEAR_VARS)

LOCAL_SRC_FILES := dump_image.c

LOCAL_CFLAGS := -Dmain=dump_image_main

LOCAL_MODULE := libdump_image
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# Static liberase_image
include $(CLEAR_VARS)

LOCAL_SRC_FILES := erase_image.c

LOCAL_CFLAGS := -Dmain=erase_image_main

LOCAL_MODULE := liberase_image
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

# dump_image static executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := dump_image.c

LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities

LOCAL_STATIC_LIBRARIES := \
    libbmlutils \
    libc \
    libcutils \
    libflashutils \
    libmmcutils \
    libmtdutils

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE := utility_dump_image
LOCAL_MODULE_STEM := dump_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities

include $(BUILD_EXECUTABLE)

# dump_image shared  executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := dump_image.c

LOCAL_SHARED_LIBRARIES := \
    libbmlutils \
    libc \
    libcutils \
    libflashutils \
    libmmcutils \
    libmtdutils

LOCAL_MODULE := dump_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)

# flash_image static executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := flash_image.c

LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities

LOCAL_STATIC_LIBRARIES := \
    libbmlutils \
    libc \
    libcutils \
    libflashutils \
    libmmcutils \
    libmtdutils

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE := utility_flash_image
LOCAL_MODULE_STEM := flash_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities

include $(BUILD_EXECUTABLE)

# flash_image shared executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := flash_image.c

LOCAL_SHARED_LIBRARIES := \
    libbmlutils \
    libc \
    libcutils \
    libflashutils \
    libmmcutils \
    libmtdutils

LOCAL_MODULE := flash_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)

# erase_image static executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := erase_image.c

LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities

LOCAL_STATIC_LIBRARIES := \
    libbmlutils \
    libc \
    libcutils \
    libflashutils \
    libmmcutils \
    libmtdutils

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE := utility_erase_image
LOCAL_MODULE_STEM := erase_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities

include $(BUILD_EXECUTABLE)

# erase_image static executable
include $(CLEAR_VARS)

LOCAL_SRC_FILES := erase_image.c

LOCAL_SHARED_LIBRARIES := \
    libbmlutils \
    libc \
    libcutils \
    libflashutils \
    libmmcutils \
    libmtdutils

LOCAL_MODULE := erase_image
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)
