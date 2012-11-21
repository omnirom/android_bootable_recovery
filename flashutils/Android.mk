LOCAL_PATH := $(call my-dir)

ifneq ($(TARGET_SIMULATOR),true)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flashutils.c
LOCAL_MODULE := libflashutils
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES += bootable/recovery
LOCAL_STATIC_LIBRARIES := libmmcutils libmtdutils libbmlutils libcrecovery

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flash_image.c
LOCAL_MODULE := flash_image
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libflashutils libmtdutils libmmcutils libbmlutils libcrecovery
LOCAL_SHARED_LIBRARIES := libcutils libc
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := dump_image.c
LOCAL_MODULE := dump_image
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libflashutils libmtdutils libmmcutils libbmlutils libcrecovery
LOCAL_SHARED_LIBRARIES := libcutils libc
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := erase_image.c
LOCAL_MODULE := erase_image
LOCAL_MODULE_TAGS := optional
LOCAL_STATIC_LIBRARIES := libflashutils libmtdutils libmmcutils libbmlutils libcrecovery
LOCAL_SHARED_LIBRARIES := libcutils libc
include $(BUILD_EXECUTABLE)

ALL_DEFAULT_INSTALLED_MODULES += $(addprefix $(TARGET_OUT)/bin/, flash_image dump_image erase_image)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flash_image.c
LOCAL_MODULE := libflash_image
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -Dmain=flash_image_main
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := dump_image.c
LOCAL_MODULE := libdump_image
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -Dmain=dump_image_main
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := erase_image.c
LOCAL_MODULE := liberase_image
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -Dmain=erase_image_main
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := dump_image.c
LOCAL_MODULE := utility_dump_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities
LOCAL_MODULE_STEM := dump_image
LOCAL_STATIC_LIBRARIES := libflashutils libmtdutils libmmcutils libbmlutils libcutils libc
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flash_image.c
LOCAL_MODULE := utility_flash_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities
LOCAL_MODULE_STEM := flash_image
LOCAL_STATIC_LIBRARIES := libflashutils libmtdutils libmmcutils libbmlutils libcutils libc
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := erase_image.c
LOCAL_MODULE := utility_erase_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities
LOCAL_MODULE_STEM := erase_image
LOCAL_STATIC_LIBRARIES := libflashutils libmtdutils libmmcutils libbmlutils libcutils libc
LOCAL_FORCE_STATIC_EXECUTABLE := true
include $(BUILD_EXECUTABLE)

#Added for dynamic building for TWRP:

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flashutils.c
LOCAL_MODULE := libflashutils
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += bootable/recovery
LOCAL_SHARED_LIBRARIES := libc libmmcutils libbmlutils
LOCAL_STATIC_LIBRARIES := libmtdutils

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := flash_image
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := flash_image.c
LOCAL_SHARED_LIBRARIES := libflashutils libmmcutils libbmlutils libcutils libc
LOCAL_STATIC_LIBRARIES := libmtdutils
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := dump_image
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := dump_image.c
LOCAL_SHARED_LIBRARIES := libflashutils libmmcutils libbmlutils libcutils libc
LOCAL_STATIC_LIBRARIES := libmtdutils
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := erase_image
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := erase_image.c
LOCAL_SHARED_LIBRARIES := libflashutils libmmcutils libbmlutils libcutils libc
LOCAL_STATIC_LIBRARIES := libmtdutils
include $(BUILD_EXECUTABLE)

endif	# !TARGET_SIMULATOR
