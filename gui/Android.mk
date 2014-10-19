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
    mousecursor.cpp

ifneq ($(TWRP_CUSTOM_KEYBOARD),)
  LOCAL_SRC_FILES += $(TWRP_CUSTOM_KEYBOARD)
else
  LOCAL_SRC_FILES += hardwarekeyboard.cpp
endif

LOCAL_SHARED_LIBRARIES += libminuitwrp libc libstdc++
LOCAL_MODULE := libguitwrp

# Use this flag to create a build that simulates threaded actions like installing zips, backups, restores, and wipes for theme testing
#TWRP_SIMULATE_ACTIONS := true
ifeq ($(TWRP_SIMULATE_ACTIONS), true)
LOCAL_CFLAGS += -D_SIMULATE_ACTIONS
endif

#TWRP_EVENT_LOGGING := true
ifeq ($(TWRP_EVENT_LOGGING), true)
LOCAL_CFLAGS += -D_EVENT_LOGGING
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
ifeq ($(TW_DISABLE_TTF), true)
    LOCAL_CFLAGS += -DTW_DISABLE_TTF
endif

ifeq ($(DEVICE_RESOLUTION),)
$(warning ********************************************************************************)
$(warning * DEVICE_RESOLUTION is NOT SET in BoardConfig.mk )
$(warning * Please see http://tinyw.in/nP7d for details    )
$(warning ********************************************************************************)
$(error stopping)
endif

ifeq ($(TW_CUSTOM_THEME),)
	ifeq "$(wildcard $(commands_recovery_local_path)/gui/devices/$(DEVICE_RESOLUTION))" ""
	$(warning ********************************************************************************)
	$(warning * DEVICE_RESOLUTION ($(DEVICE_RESOLUTION)) does NOT EXIST in $(commands_recovery_local_path)/gui/devices )
	$(warning * Please choose an existing theme or create a new one for your device )
	$(warning ********************************************************************************)
	$(error stopping)
	endif
endif

LOCAL_C_INCLUDES += bionic external/stlport/stlport $(commands_recovery_local_path)/gui/devices/$(DEVICE_RESOLUTION)

include $(BUILD_STATIC_LIBRARY)

# Transfer in the resources for the device
include $(CLEAR_VARS)
LOCAL_MODULE := twrp
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/res
TWRP_RES_LOC := $(commands_recovery_local_path)/gui/devices/common/res
TWRP_COMMON_XML := $(hide) echo "No common TWRP XML resources"

ifeq ($(TW_CUSTOM_THEME),)
	PORTRAIT := 320x480 480x800 480x854 540x960 720x1280 800x1280 1080x1920 1200x1920 1440x2560 1600x2560
	LANDSCAPE := 800x480 1024x600 1024x768 1280x800 1920x1200 2560x1600
	WATCH := 240x240 280x280 320x320
	TWRP_THEME_LOC := $(commands_recovery_local_path)/gui/devices/$(DEVICE_RESOLUTION)/res
	ifneq ($(filter $(DEVICE_RESOLUTION), $(PORTRAIT)),)
		TWRP_COMMON_XML := cp -fr $(commands_recovery_local_path)/gui/devices/portrait/res/* $(TARGET_RECOVERY_ROOT_OUT)/res/
	else ifneq ($(filter $(DEVICE_RESOLUTION), $(LANDSCAPE)),)
		TWRP_COMMON_XML := cp -fr $(commands_recovery_local_path)/gui/devices/landscape/res/* $(TARGET_RECOVERY_ROOT_OUT)/res/
	else ifneq ($(filter $(DEVICE_RESOLUTION), $(WATCH)),)
		TWRP_COMMON_XML := cp -fr $(commands_recovery_local_path)/gui/devices/watch/res/* $(TARGET_RECOVERY_ROOT_OUT)/res/
	endif
else
	TWRP_THEME_LOC := $(TW_CUSTOM_THEME)
endif

ifeq ($(TW_DISABLE_TTF), true)
	TWRP_REMOVE_FONT := rm -f $(TARGET_RECOVERY_ROOT_OUT)/res/fonts/*.ttf
else
	TWRP_REMOVE_FONT := rm -f $(TARGET_RECOVERY_ROOT_OUT)/res/fonts/*.dat
endif

TWRP_RES_GEN := $(intermediates)/twrp
ifneq ($(TW_USE_TOOLBOX), true)
	TWRP_SH_TARGET := /sbin/busybox
else
	TWRP_SH_TARGET := /sbin/mksh
endif

$(TWRP_RES_GEN):
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res/
	cp -fr $(TWRP_RES_LOC)/* $(TARGET_RECOVERY_ROOT_OUT)/res/
	cp -fr $(TWRP_THEME_LOC)/* $(TARGET_RECOVERY_ROOT_OUT)/res/
	$(TWRP_COMMON_XML)
	$(TWRP_REMOVE_FONT)
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin/
	ln -sf $(TWRP_SH_TARGET) $(TARGET_RECOVERY_ROOT_OUT)/sbin/sh
	ln -sf /sbin/pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gzip
	ln -sf /sbin/unpigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/gunzip


LOCAL_GENERATED_SOURCES := $(TWRP_RES_GEN)
LOCAL_SRC_FILES := twrp $(TWRP_RES_GEN)
include $(BUILD_PREBUILT)
