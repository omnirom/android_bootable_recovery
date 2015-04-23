LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := events.c resources.c graphics_overlay.c graphics_utils.c

ifneq ($(TW_BOARD_CUSTOM_GRAPHICS),)
    LOCAL_SRC_FILES += $(TW_BOARD_CUSTOM_GRAPHICS)
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
      LOCAL_C_INCLUDES += $(commands_recovery_local_path)/minuitwrp/include
    else
      LOCAL_C_INCLUDES += $(TARGET_CUSTOM_KERNEL_HEADERS)
    endif
  endif
else
  LOCAL_C_INCLUDES += $(commands_recovery_local_path)/minuitwrp/include
endif

ifeq ($(TW_NEW_ION_HEAP), true)
  LOCAL_CFLAGS += -DNEW_ION_HEAP
endif

LOCAL_C_INCLUDES += \
    external/libpng \
    external/zlib \
    system/core/include \
    external/jpeg

ifeq ($(RECOVERY_TOUCHSCREEN_SWAP_XY), true)
LOCAL_CFLAGS += -DRECOVERY_TOUCHSCREEN_SWAP_XY
endif

ifeq ($(RECOVERY_TOUCHSCREEN_FLIP_X), true)
LOCAL_CFLAGS += -DRECOVERY_TOUCHSCREEN_FLIP_X
endif

ifeq ($(RECOVERY_TOUCHSCREEN_FLIP_Y), true)
LOCAL_CFLAGS += -DRECOVERY_TOUCHSCREEN_FLIP_Y
endif

ifeq ($(RECOVERY_GRAPHICS_USE_LINELENGTH), true)
LOCAL_CFLAGS += -DRECOVERY_GRAPHICS_USE_LINELENGTH
endif

#Remove the # from the line below to enable event logging
#TWRP_EVENT_LOGGING := true
ifeq ($(TWRP_EVENT_LOGGING), true)
LOCAL_CFLAGS += -D_EVENT_LOGGING
endif

ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),RGBX_8888)
  LOCAL_CFLAGS += -DRECOVERY_RGBX
endif
ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),BGRA_8888)
  LOCAL_CFLAGS += -DRECOVERY_BGRA
endif
ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),RGB_565)
  LOCAL_CFLAGS += -DRECOVERY_RGB_565
endif

ifeq ($(TARGET_RECOVERY_PIXEL_FORMAT),"RGBX_8888")
  LOCAL_CFLAGS += -DRECOVERY_RGBX
endif
ifeq ($(TARGET_RECOVERY_PIXEL_FORMAT),"BGRA_8888")
  LOCAL_CFLAGS += -DRECOVERY_BGRA
endif
ifeq ($(TARGET_RECOVERY_PIXEL_FORMAT),"RGB_565")
  LOCAL_CFLAGS += -DRECOVERY_RGB_565
endif
ifeq ($(TW_SCREEN_BLANK_ON_BOOT), true)
    LOCAL_CFLAGS += -DTW_SCREEN_BLANK_ON_BOOT
endif

ifeq ($(BOARD_HAS_FLIPPED_SCREEN), true)
LOCAL_CFLAGS += -DBOARD_HAS_FLIPPED_SCREEN
endif

ifeq ($(TW_IGNORE_MAJOR_AXIS_0), true)
LOCAL_CFLAGS += -DTW_IGNORE_MAJOR_AXIS_0
endif

ifeq ($(TW_IGNORE_MT_POSITION_0), true)
LOCAL_CFLAGS += -DTW_IGNORE_MT_POSITION_0
endif

ifneq ($(TW_INPUT_BLACKLIST),)
  LOCAL_CFLAGS += -DTW_INPUT_BLACKLIST=$(TW_INPUT_BLACKLIST)
endif

ifneq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=$(BOARD_USE_CUSTOM_RECOVERY_FONT)
else
    ifeq ($(TW_THEME),)
        # This converts the old DEVICE_RESOLUTION flag to the new TW_THEME flag
        PORTRAIT_MDPI := 320x480 480x800 480x854 540x960
        PORTRAIT_HDPI := 720x1280 800x1280 1080x1920 1200x1920 1440x2560 1600x2560
        WATCH_MDPI := 240x240 280x280 320x320
        LANDSCAPE_MDPI := 800x480 1024x600 1024x768
        LANDSCAPE_HDPI := 1280x800 1920x1200 2560x1600
        ifneq ($(filter $(DEVICE_RESOLUTION), $(PORTRAIT_MDPI)),)
            TW_THEME := portrait_mdpi
        else ifneq ($(filter $(DEVICE_RESOLUTION), $(PORTRAIT_HDPI)),)
            TW_THEME := portrait_hdpi
        else ifneq ($(filter $(DEVICE_RESOLUTION), $(WATCH_MDPI)),)
            TW_THEME := watch_mdpi
        else ifneq ($(filter $(DEVICE_RESOLUTION), $(LANDSCAPE_MDPI)),)
            TW_THEME := landscape_mdpi
        else ifneq ($(filter $(DEVICE_RESOLUTION), $(LANDSCAPE_HDPI)),)
            TW_THEME := landscape_hdpi
        endif
    endif
    ifeq ($(TW_THEME), portrait_mdpi)
        LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=\"roboto_10x18.h\"
    else ifeq ($(TW_THEME), portrait_hdpi)
        LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=\"roboto_15x24.h\"
    else ifeq ($(TW_THEME), watch_mdpi)
        LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=\"font_7x16.h\"
    else ifeq ($(TW_THEME), landscape_mdpi)
        LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=\"roboto_10x18.h\"
    else ifeq ($(TW_THEME), landscape_hdpi)
        LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=\"roboto_15x24.h\"
    endif
endif

ifneq ($(TW_WHITELIST_INPUT),)
  LOCAL_CFLAGS += -DWHITELIST_INPUT=$(TW_WHITELIST_INPUT)
endif

ifeq ($(TW_DISABLE_TTF), true)
    LOCAL_CFLAGS += -DTW_DISABLE_TTF
else
    LOCAL_SHARED_LIBRARIES += libft2
    LOCAL_C_INCLUDES += external/freetype/include
    LOCAL_SRC_FILES += truetype.c
endif

LOCAL_CFLAGS += -DTWRES=\"$(TWRES_PATH)\"
LOCAL_SHARED_LIBRARIES += libz libc libcutils libjpeg libpng
LOCAL_STATIC_LIBRARIES += libpixelflinger_static
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := libminuitwrp

include $(BUILD_SHARED_LIBRARY)
