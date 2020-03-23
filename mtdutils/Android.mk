ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c

ifneq ($(filter rk30xx rk3188,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SRC_FILES += rk3xhack.c
LOCAL_CFLAGS += -DRK3X
endif

ifeq ($(TARGET_MTD_BY_NAME),true)
LOCAL_CFLAGS += -DBYNAME
endif

LOCAL_MODULE := libmtdutils
LOCAL_STATIC_LIBRARIES := libcutils libc
LOCAL_CLANG := true

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	mtdutils.c \
	mounts.c

ifneq ($(filter rk30xx rk3188,$(TARGET_BOARD_PLATFORM)),)
LOCAL_SRC_FILES += rk3xhack.c
LOCAL_CFLAGS += -DRK3X
endif

ifeq ($(TARGET_MTD_BY_NAME),true)
LOCAL_CFLAGS += -DBYNAME
endif

LOCAL_MODULE := libmtdutils
LOCAL_SHARED_LIBRARIES := libcutils libc
LOCAL_CLANG := true

include $(BUILD_SHARED_LIBRARY)

ifeq ($(BOARD_USES_BML_OVER_MTD),true)
include $(CLEAR_VARS)
LOCAL_SRC_FILES := bml_over_mtd.c
LOCAL_C_INCLUDES += $(commands_recovery_local_path)/mtdutils
LOCAL_MODULE := libbml_over_mtd
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -Dmain=bml_over_mtd_main
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := bml_over_mtd.c
LOCAL_MODULE := bml_over_mtd
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities
LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities
LOCAL_MODULE_STEM := bml_over_mtd
LOCAL_C_INCLUDES += $(commands_recovery_local_path)/mtdutils
LOCAL_SHARED_LIBRARIES := libmtdutils libcutils liblog libc
include $(BUILD_EXECUTABLE)
endif

endif	# !TARGET_SIMULATOR

