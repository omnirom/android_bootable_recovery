LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := graphics_overlay.c events.c resources.c
ifneq ($(BOARD_CUSTOM_GRAPHICS),)
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_GRAPHICS)
else
  LOCAL_SRC_FILES += graphics.c
endif

LOCAL_C_INCLUDES +=\
    external/libpng \
    external/zlib \
    system/core/include/pixelflinger

ifeq ($(TW_TARGET_USES_QCOM_BSP), true)
  LOCAL_CFLAGS += -DMSM_BSP
  ifeq ($(TARGET_PREBUILT_KERNEL),)
    LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
  else
    ifeq ($(TARGET_CUSTOM_KERNEL_HEADERS),)
      LOCAL_C_INCLUDES += $(commands_recovery_local_path)/minui/include
    else
      LOCAL_C_INCLUDES += $(TARGET_CUSTOM_KERNEL_HEADERS)
    endif
  endif
else
  LOCAL_C_INCLUDES += $(commands_recovery_local_path)/minui/include
endif

ifeq ($(TW_NEW_ION_HEAP), true)
  LOCAL_CFLAGS += -DNEW_ION_HEAP
endif

LOCAL_STATIC_LIBRARY := libpng
LOCAL_WHOLE_STATIC_LIBRARIES := libpixelflinger_static
LOCAL_MODULE := libminui

# This used to compare against values in double-quotes (which are just
# ordinary characters in this context).  Strip double-quotes from the
# value so that either will work.

ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),RGBX_8888)
  LOCAL_CFLAGS += -DRECOVERY_RGBX
endif
ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),BGRA_8888)
  LOCAL_CFLAGS += -DRECOVERY_BGRA
endif

ifneq ($(TARGET_RECOVERY_OVERSCAN_PERCENT),)
  LOCAL_CFLAGS += -DOVERSCAN_PERCENT=$(TARGET_RECOVERY_OVERSCAN_PERCENT)
else
  LOCAL_CFLAGS += -DOVERSCAN_PERCENT=0
endif

ifneq ($(TW_BRIGHTNESS_PATH),)
  LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=\"$(TW_BRIGHTNESS_PATH)\"
endif
ifneq ($(TW_MAX_BRIGHTNESS),)
  LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=$(TW_MAX_BRIGHTNESS)
else
  LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=255
endif
ifneq ($(TW_NO_SCREEN_BLANK),)
  LOCAL_CFLAGS += -DTW_NO_SCREEN_BLANK
endif
ifneq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=$(BOARD_USE_CUSTOM_RECOVERY_FONT)
endif
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := graphics_overlay.c events.c resources.c
ifneq ($(BOARD_CUSTOM_GRAPHICS),)
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_GRAPHICS)
else
  LOCAL_SRC_FILES += graphics.c
endif

ifeq ($(TW_TARGET_USES_QCOM_BSP), true)
  LOCAL_CFLAGS += -DMSM_BSP
  ifeq ($(TARGET_PREBUILT_KERNEL),)
    LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
    LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
  else
    ifeq ($(TARGET_CUSTOM_KERNEL_HEADERS),)
      LOCAL_C_INCLUDES += $(commands_recovery_local_path)/minui/include
    else
      LOCAL_C_INCLUDES += $(TARGET_CUSTOM_KERNEL_HEADERS)
    endif
  endif
else
  LOCAL_C_INCLUDES += $(commands_recovery_local_path)/minui/include
endif

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE := libminui

LOCAL_ARM_MODE:= arm
LOCAL_SHARED_LIBRARIES := libpng libpixelflinger
# This used to compare against values in double-quotes (which are just
# ordinary characters in this context).  Strip double-quotes from the
# value so that either will work.

ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),RGBX_8888)
  LOCAL_CFLAGS += -DRECOVERY_RGBX
endif
ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),BGRA_8888)
  LOCAL_CFLAGS += -DRECOVERY_BGRA
endif

ifneq ($(TARGET_RECOVERY_OVERSCAN_PERCENT),)
  LOCAL_CFLAGS += -DOVERSCAN_PERCENT=$(TARGET_RECOVERY_OVERSCAN_PERCENT)
else
  LOCAL_CFLAGS += -DOVERSCAN_PERCENT=0
endif

ifneq ($(TW_BRIGHTNESS_PATH),)
  LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=\"$(TW_BRIGHTNESS_PATH)\"
endif
ifneq ($(TW_MAX_BRIGHTNESS),)
  LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=$(TW_MAX_BRIGHTNESS)
else
  LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=255
endif
ifneq ($(TW_NO_SCREEN_BLANK),)
  LOCAL_CFLAGS += -DTW_NO_SCREEN_BLANK
endif
ifneq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=$(BOARD_USE_CUSTOM_RECOVERY_FONT)
endif

LOCAL_CFLAGS += -DFASTMMI_FEATURE

include $(BUILD_SHARED_LIBRARY)
