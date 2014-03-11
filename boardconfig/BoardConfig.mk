############################################
# Device Specific Config #
# These can go under device BoardConfig.mk #
# By chintan for TWRP recovery        #
############################################
#

#Samsung GT-I5500 (galaxy5)
ifeq ($(TARGET_PRODUCT), cm_galaxy5)
    TARGET_COMMON_NAME := galaxy5
    TARGET_SCREEN_HEIGHT := 320
    TARGET_SCREEN_WIDTH := 240
    RECOVERY_GRAPHICS_USE_LINELENGTH := true
    TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
    DEVICE_RESOLUTION := 240x320

#Samsung GT-I5510 (callisto)
else ifeq ($(TARGET_PRODUCT), cm_callisto)
    TARGET_COMMON_NAME := callisto
    TARGET_SCREEN_HEIGHT := 320
    TARGET_SCREEN_WIDTH := 240
    RECOVERY_GRAPHICS_USE_LINELENGTH := true
    TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
    DEVICE_RESOLUTION := 240x320

#Samsung GT-S5570 (tass)
else ifeq ($(TARGET_PRODUCT), cm_tass)
    TARGET_COMMON_NAME := tass
    TARGET_SCREEN_HEIGHT := 320
    TARGET_SCREEN_WIDTH := 240
    RECOVERY_GRAPHICS_USE_LINELENGTH := true
    TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
    DEVICE_RESOLUTION := 240x320

#Samsung GT-S5660 (gio)
else ifeq ($(TARGET_PRODUCT), cm_gio)
    TARGET_COMMON_NAME := gio
    TARGET_SCREEN_HEIGHT := 480
    TARGET_SCREEN_WIDTH := 320
    RECOVERY_GRAPHICS_USE_LINELENGTH := true
    TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
    DEVICE_RESOLUTION := 320x480

#Samsung GT-S5670 (beni)
else ifeq ($(TARGET_PRODUCT), cm_beni)
    TARGET_COMMON_NAME := beni
    TARGET_SCREEN_HEIGHT := 320
    TARGET_SCREEN_WIDTH := 240
    RECOVERY_GRAPHICS_USE_LINELENGTH := true
    TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
    DEVICE_RESOLUTION := 240x320

#Samsung Galaxy Ace GT-S5830 (cooper)
else ifeq ($(TARGET_PRODUCT), cm_cooper)
    TARGET_COMMON_NAME := cooper
    TARGET_SCREEN_HEIGHT := 480
    TARGET_SCREEN_WIDTH := 320
    RECOVERY_GRAPHICS_USE_LINELENGTH := true
    TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
    DEVICE_RESOLUTION := 320x480

endif
#---- end device specific config

# The below flags must always be defined as default in BoardConfig.mk, unless defined above:
# device name to display in About dialog
ifndef TARGET_COMMON_NAME
    TARGET_COMMON_NAME := $(TARGET_PRODUCT)
endif

LOCAL_CFLAGS += -DTARGET_COMMON_NAME="$(TARGET_COMMON_NAME)"


