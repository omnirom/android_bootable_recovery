# Copyright (C) 2007 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

# libminui (static library)
# ===============================
# include $(CLEAR_VARS)

# LOCAL_SRC_FILES := \
#     events.cpp \
#     graphics.cpp \
#     graphics_drm.cpp \
#     graphics_fbdev.cpp \
#     graphics_overlay.cpp \
#     resources.cpp

# LOCAL_C_INCLUDES := external/libcxx/include external/libpng

# ifeq ($(TW_TARGET_USES_QCOM_BSP), true)
#   LOCAL_CFLAGS += -DMSM_BSP
#   LOCAL_SRC_FILES += graphics_overlay.cpp
#   ifeq ($(TARGET_PREBUILT_KERNEL),)
#     ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
#       LOCAL_REQUIRED_MODULES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
#     else
#       LOCAL_ADDITIONAL_DEPENDENCIES := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr
#     endif
#     LOCAL_C_INCLUDES += $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ/usr/include
#   else
#     ifeq ($(TARGET_CUSTOM_KERNEL_HEADERS),)
#       LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
#     else
#       LOCAL_C_INCLUDES += $(TARGET_CUSTOM_KERNEL_HEADERS)
#     endif
#   endif
# else
#   LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
#   # The header files required for adf graphics can cause compile errors
#   # with adf graphics.
#   LOCAL_SRC_FILES += graphics_adf.cpp
#   LOCAL_WHOLE_STATIC_LIBRARIES += libadf
# endif

# ifeq ($(TW_NEW_ION_HEAP), true)
#   LOCAL_CFLAGS += -DNEW_ION_HEAP
# endif

# LOCAL_STATIC_LIBRARIES += libpng libbase
# ifneq ($(wildcard external/libdrm/Android.common.mk),)
# LOCAL_WHOLE_STATIC_LIBRARIES += libdrm_platform
# else
# LOCAL_WHOLE_STATIC_LIBRARIES += libdrm
# endif
# ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
#     LOCAL_CFLAGS += -DHAS_LIBSYNC
#     LOCAL_WHOLE_STATIC_LIBRARIES += libsync_recovery
# endif

# LOCAL_CFLAGS += -Wall -Werror -std=c++14 -Wno-unused-private-field
# LOCAL_C_INCLUDES += $(LOCAL_PATH)/include
# LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include

# LOCAL_MODULE := libminui

# LOCAL_CLANG := true

# # This used to compare against values in double-quotes (which are just
# # ordinary characters in this context).  Strip double-quotes from the
# # value so that either will work.

# ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),ABGR_8888)
#   LOCAL_CFLAGS += -DRECOVERY_ABGR
# endif
# ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),RGBA_8888)
#   LOCAL_CFLAGS += -DRECOVERY_RGBA
# endif
# ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),RGBX_8888)
#   LOCAL_CFLAGS += -DRECOVERY_RGBX
# endif
# ifeq ($(subst ",,$(TARGET_RECOVERY_PIXEL_FORMAT)),BGRA_8888)
#   LOCAL_CFLAGS += -DRECOVERY_BGRA
# endif

# ifneq ($(TARGET_RECOVERY_OVERSCAN_PERCENT),)
#   LOCAL_CFLAGS += -DOVERSCAN_PERCENT=$(TARGET_RECOVERY_OVERSCAN_PERCENT)
# else
#   LOCAL_CFLAGS += -DOVERSCAN_PERCENT=0
# endif

# ifneq ($(TW_BRIGHTNESS_PATH),)
#   LOCAL_CFLAGS += -DTW_BRIGHTNESS_PATH=\"$(TW_BRIGHTNESS_PATH)\"
# endif
# ifneq ($(TW_MAX_BRIGHTNESS),)
#   LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=$(TW_MAX_BRIGHTNESS)
# else
#   LOCAL_CFLAGS += -DTW_MAX_BRIGHTNESS=255
# endif
# ifneq ($(TW_NO_SCREEN_BLANK),)
#   LOCAL_CFLAGS += -DTW_NO_SCREEN_BLANK
# endif
# ifneq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
#   LOCAL_CFLAGS += -DBOARD_USE_CUSTOM_RECOVERY_FONT=$(BOARD_USE_CUSTOM_RECOVERY_FONT)
# endif
# ifeq ($(wildcard system/core/healthd/animation.h),)
#     TARGET_GLOBAL_CFLAGS += -DTW_NO_MINUI_CUSTOM_FONTS
#     CLANG_TARGET_GLOBAL_CFLAGS += -DTW_NO_MINUI_CUSTOM_FONTS
# endif
# ifneq ($(TARGET_RECOVERY_DEFAULT_ROTATION),)
#   LOCAL_CFLAGS += -DDEFAULT_ROTATION=$(TARGET_RECOVERY_DEFAULT_ROTATION)
# else
#   LOCAL_CFLAGS += -DDEFAULT_ROTATION=ROTATION_NONE
# endif

# include $(BUILD_STATIC_LIBRARY)

# libminui (shared library)
# ===============================
# Used by OEMs for factory test images.
# include $(CLEAR_VARS)
# LOCAL_CLANG := true
# LOCAL_MODULE := libminui
# LOCAL_WHOLE_STATIC_LIBRARIES += libminui
# LOCAL_SHARED_LIBRARIES := \
#     libpng \
#     libbase

# LOCAL_CFLAGS := -Wall -Werror -std=c++14 -Wno-unused-private-field
# LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
# LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
# include $(BUILD_SHARED_LIBRARY)

# include $(CLEAR_VARS)
# LOCAL_MODULE := minuitest
# LOCAL_MODULE_TAGS := optional
# LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
# LOCAL_SRC_FILES := main.cpp
# LOCAL_SHARED_LIBRARIES := libbinder libminui libpng libz libutils libstdc++ libcutils liblog libm libc
# LOCAL_C_INCLUDES := external/libcxx/include external/libpng
# ifneq ($(TARGET_ARCH), arm64)
#     ifneq ($(TARGET_ARCH), x86_64)
#         LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker
#     else
#         LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
#     endif
# else
#     LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
# endif
# include $(BUILD_EXECUTABLE)
