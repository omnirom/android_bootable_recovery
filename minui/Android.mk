LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := events.c resources.c
ifneq ($(BOARD_CUSTOM_GRAPHICS),)
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_GRAPHICS)
else
  LOCAL_SRC_FILES += graphics.c
endif

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib
LOCAL_STATIC_LIBRARY := libpng
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

ifneq ($(TARGET_RECOVERY_LCD_BACKLIGHT_PATH),)
  LOCAL_CFLAGS += -DRECOVERY_LCD_BACKLIGHT_PATH=$(TARGET_RECOVERY_LCD_BACKLIGHT_PATH)
endif

include $(BUILD_STATIC_LIBRARY)
