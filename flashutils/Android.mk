LOCAL_PATH := $(call my-dir)

ifneq ($(TARGET_SIMULATOR),true)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := flashutils.c
LOCAL_MODULE := libflashutils
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES += $(commands_recovery_local_path)
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
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES += $(commands_recovery_local_path)
LOCAL_SHARED_LIBRARIES := libc libmtdutils libmmcutils libbmlutils libcrecovery

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := flash_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := flash_image.c
LOCAL_SHARED_LIBRARIES := libmtdutils libflashutils libmmcutils libbmlutils libcutils libc
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := dump_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := dump_image.c
LOCAL_SHARED_LIBRARIES := libmtdutils libflashutils libmmcutils libbmlutils libcutils libc
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := erase_image
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin
LOCAL_SRC_FILES := erase_image.c
LOCAL_SHARED_LIBRARIES := libmtdutils libflashutils libmmcutils libbmlutils libcutils libc
include $(BUILD_EXECUTABLE)

endif	# !TARGET_SIMULATOR
