LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    mounts.c \
    mtdutils.c

ifneq ($(filter rk30xx rk3188,$(TARGET_BOARD_PLATFORM)),)
    LOCAL_SRC_FILES += rk3xhack.c
    LOCAL_CFLAGS += -DRK3X
endif

LOCAL_STATIC_LIBRARIES := \
    libc \
    libcutils

LOCAL_CLANG := true

LOCAL_MODULE := libmtdutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    mounts.c \
    mtdutils.c

ifneq ($(filter rk30xx rk3188,$(TARGET_BOARD_PLATFORM)),)
    LOCAL_SRC_FILES += rk3xhack.c
    LOCAL_CFLAGS += -DRK3X
endif

LOCAL_SHARED_LIBRARIES := \
    libc \
    libcutils

LOCAL_CLANG := true

LOCAL_MODULE := libmtdutils
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

ifeq ($(BOARD_USES_BML_OVER_MTD),true)
    include $(CLEAR_VARS)

    LOCAL_SRC_FILES := bml_over_mtd.c

    LOCAL_CFLAGS := -Dmain=bml_over_mtd_main

    LOCAL_MODULE := libbml_over_mtd
    LOCAL_MODULE_TAGS := optional

    include $(BUILD_STATIC_LIBRARY)

    include $(CLEAR_VARS)

    LOCAL_SRC_FILES := bml_over_mtd.c

    LOCAL_SHARED_LIBRARIES := \
        libc \
        libcutils \
        liblog \
        libmtdutils

    LOCAL_UNSTRIPPED_PATH := $(PRODUCT_OUT)/symbols/utilities

    LOCAL_MODULE := bml_over_mtd
    LOCAL_MODULE_STEM := bml_over_mtd
    LOCAL_MODULE_TAGS := optional
    LOCAL_MODULE_CLASS := UTILITY_EXECUTABLES
    LOCAL_MODULE_PATH := $(PRODUCT_OUT)/utilities

    include $(BUILD_EXECUTABLE)
endif
