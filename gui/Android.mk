LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS := -fno-strict-aliasing

LOCAL_SRC_FILES := \
    gui.cpp \
    resources.cpp \
    pages.cpp \
    text.cpp \
    image.cpp \
    action.cpp \
    console.cpp \
    fill.cpp \
    button.cpp \
    checkbox.cpp \
    fileselector.cpp \
    progressbar.cpp \
    animation.cpp \
    object.cpp \
    slider.cpp \
    slidervalue.cpp \
    listbox.cpp \
    keyboard.cpp \
    input.cpp \
    blanktimer.cpp \
    partitionlist.cpp \
    mousecursor.cpp \
    scrolllist.cpp \
    patternpassword.cpp \
    textbox.cpp \
    terminal.cpp \
    twmsg.cpp

ifneq ($(TWRP_CUSTOM_KEYBOARD),)
    LOCAL_SRC_FILES += $(TWRP_CUSTOM_KEYBOARD)
else
    LOCAL_SRC_FILES += hardwarekeyboard.cpp
endif

LOCAL_SHARED_LIBRARIES += libminuitwrp libc libstdc++ libminzip libaosprecovery
LOCAL_MODULE := libguitwrp

#TWRP_EVENT_LOGGING := true
ifeq ($(TWRP_EVENT_LOGGING), true)
    LOCAL_CFLAGS += -D_EVENT_LOGGING
endif
ifneq ($(TW_USE_KEY_CODE_TOUCH_SYNC),)
    LOCAL_CFLAGS += -DTW_USE_KEY_CODE_TOUCH_SYNC=$(TW_USE_KEY_CODE_TOUCH_SYNC)
endif

ifneq ($(TW_NO_SCREEN_BLANK),)
    LOCAL_CFLAGS += -DTW_NO_SCREEN_BLANK
endif
ifneq ($(TW_NO_SCREEN_TIMEOUT),)
    LOCAL_CFLAGS += -DTW_NO_SCREEN_TIMEOUT
endif
ifeq ($(HAVE_SELINUX), true)
    LOCAL_CFLAGS += -DHAVE_SELINUX
endif
ifeq ($(TW_OEM_BUILD), true)
    LOCAL_CFLAGS += -DTW_OEM_BUILD
endif
ifneq ($(TW_X_OFFSET),)
    LOCAL_CFLAGS += -DTW_X_OFFSET=$(TW_X_OFFSET)
endif
ifneq ($(TW_Y_OFFSET),)
    LOCAL_CFLAGS += -DTW_Y_OFFSET=$(TW_Y_OFFSET)
endif
ifeq ($(TW_ROUND_SCREEN), true)
    LOCAL_CFLAGS += -DTW_ROUND_SCREEN
endif

LOCAL_C_INCLUDES += bionic system/core/libpixelflinger/include
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    LOCAL_C_INCLUDES += external/stlport/stlport
endif

LOCAL_CFLAGS += -DTWRES=\"$(TWRES_PATH)\"

include $(BUILD_STATIC_LIBRARY)

# Transfer in the resources for the device
include $(CLEAR_VARS)
LOCAL_MODULE := twrp
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)$(TWRES_PATH)
TWRP_RES := $(commands_recovery_local_path)/gui/devices/common/res/*
# enable this to use new themes:
TWRP_NEW_THEME := true

ifeq ($(TW_CUSTOM_THEME),)
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

ifeq ($(TWRP_NEW_THEME),true)
    TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/theme/$(TW_THEME)
    TWRP_RES := $(commands_recovery_local_path)/gui/theme/common/fonts
    TWRP_RES += $(commands_recovery_local_path)/gui/theme/common/languages
    TWRP_RES += $(commands_recovery_local_path)/gui/theme/common/$(word 1,$(subst _, ,$(TW_THEME))).xml
ifeq ($(TW_EXTRA_LANGUAGES),true)
    TWRP_RES += $(commands_recovery_local_path)/gui/theme/extra-languages/fonts
    TWRP_RES += $(commands_recovery_local_path)/gui/theme/extra-languages/languages
endif
# for future copying of used include xmls and fonts:
# UI_XML := $(TWRP_THEME_LOC)/ui.xml
# TWRP_INCLUDE_XMLS := $(shell xmllint --xpath '/recovery/include/xmlfile/@name' $(UI_XML)|sed -n 's/[^\"]*\"\([^\"]*\)\"[^\"]*/\1\n/gp'|sort|uniq)
# TWRP_FONTS_TTF := $(shell xmllint --xpath '/recovery/resources/font/@filename' $(UI_XML)|sed -n 's/[^\"]*\"\([^\"]*\)\"[^\"]*/\1\n/gp'|sort|uniq)niq)
ifeq ($(wildcard $(TWRP_THEME_LOC)/ui.xml),)
    $(warning ****************************************************************************)
    $(warning * TW_THEME is not valid: '$(TW_THEME)')
    $(warning * Please choose an appropriate TW_THEME or create a new one for your device.)
    $(warning * Available themes:)
    $(warning * $(notdir $(wildcard $(commands_recovery_local_path)/gui/theme/*_*)))
    $(warning ****************************************************************************)
    $(error stopping)
endif
else
    TWRP_RES += $(commands_recovery_local_path)/gui/devices/$(word 1,$(subst _, ,$(TW_THEME)))/res/*
    ifeq ($(TW_THEME), portrait_mdpi)
        TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/devices/480x800/res
    else ifeq ($(TW_THEME), portrait_hdpi)
        TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/devices/1080x1920/res
    else ifeq ($(TW_THEME), watch_mdpi)
        TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/devices/320x320/res
    else ifeq ($(TW_THEME), landscape_mdpi)
        TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/devices/800x480/res
    else ifeq ($(TW_THEME), landscape_hdpi)
        TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/devices/1920x1200/res
    else
        $(warning ****************************************************************************)
        $(warning * TW_THEME ($(TW_THEME)) is not valid.)
        $(warning * Please choose an appropriate TW_THEME or create a new one for your device.)
        $(warning * Valid options are portrait_mdpi portrait_hdpi watch_mdpi)
        $(warning *                   landscape_mdpi landscape_hdpi)
        $(warning ****************************************************************************)
        $(error stopping)
    endif
endif
else
    TWRP_THEME_LOC := $(TW_CUSTOM_THEME)
endif
TWRP_RES += $(TW_ADDITIONAL_RES)

TWRP_RES_GEN := $(intermediates)/twrp
ifneq ($(TW_USE_TOOLBOX), true)
    TWRP_SH_TARGET := /sbin/busybox
else
    TWRP_SH_TARGET := /sbin/mksh
endif

$(TWRP_RES_GEN):
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)$(TWRES_PATH)
	cp -fr $(TWRP_RES) $(TARGET_RECOVERY_ROOT_OUT)$(TWRES_PATH)
	cp -fr $(TWRP_THEME_LOC)/* $(TARGET_RECOVERY_ROOT_OUT)$(TWRES_PATH)
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin/
ifneq ($(TW_USE_TOOLBOX), true)
	ln -sf $(TWRP_SH_TARGET) $(TARGET_RECOVERY_ROOT_OUT)/sbin/sh
endif
	ln -sf /sbin/pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gzip
	ln -sf /sbin/unpigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gunzip


LOCAL_GENERATED_SOURCES := $(TWRP_RES_GEN)
LOCAL_SRC_FILES := twrp $(TWRP_RES_GEN)
include $(BUILD_PREBUILT)
