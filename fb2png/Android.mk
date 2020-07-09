# Makefile for Android to build fb2png
#
# Copyright (C) 2012  Kyan <kyan.ql.he@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#

#Ported to CWM source for PhilZ Touch recovery
#Special thanks to talustus for his help in cross compiling and the Makefile
#Thanks to McKael @xda for his help in fixing for Nexus 4

LOCAL_PATH:= $(call my-dir)

# We need to build this for both the device (as a shared library)
# and the host (as a static library for tools to use).

# <-- Build libpng
include $(CLEAR_VARS)

LOCAL_MODULE := libpng
LOCAL_SRC_FILES := libpng/lib/libpng.a

include $(PREBUILT_STATIC_LIBRARY)
# -->


# <-- Build libfb2png
include $(CLEAR_VARS)

LOCAL_MODULE := libfb2png
LOCAL_SRC_FILES := \
    fb2png.c \
    img_process.c \
    fb.c

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_CFLAGS += -DANDROID
LOCAL_STATIC_LIBRARIES := libpng libz

include $(BUILD_STATIC_LIBRARY)
# -->


# <-- Build fb2png bin
include $(CLEAR_VARS)

LOCAL_SRC_FILES := main.c
LOCAL_MODULE := fb2png
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -DANDROID
LOCAL_STATIC_LIBRARIES := libfb2png libpng libz libc

LOCAL_C_INCLUDES +=\
    external/libpng\
    external/zlib

LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_PACK_MODULE_RELOCATIONS := false

include $(BUILD_EXECUTABLE)
# -->
