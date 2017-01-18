# Copyright 2017 TeamWin
# This file is part of TWRP/TeamWin Recovery Project.
#
# TWRP is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TWRP is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TWRP.  If not, see <http://www.gnu.org/licenses/>.

LOCAL_PATH := external/toybox

include $(CLEAR_VARS)

LOCAL_MODULE := toybox_recovery
LOCAL_MODULE_STEM := toybox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

LOCAL_WHOLE_STATIC_LIBRARIES := libtoybox

LOCAL_SHARED_LIBRARIES := \
    libc \
    libcrypto \
    libcutils \
    liblog \
    libselinux

LOCAL_CFLAGS := \
    -fdata-sections \
    -ffunction-sections \
    -fno-asynchronous-unwind-tables \
    -funsigned-char \
    -Os \
    -std=c99 \
    -Wno-char-subscripts \
    -Wno-sign-compare \
    -Wno-string-plus-int \
    -Wno-uninitialized \
    -Wno-unused-parameter

toybox_upstream_version := $(shell grep -o 'TOYBOX_VERSION.*\".*\"' $(LOCAL_PATH)/main.c | cut -d'"' -f2)
toybox_sha := $(shell git -C $(LOCAL_PATH) rev-parse --short=12 HEAD 2>/dev/null)
toybox_version := $(toybox_upstream_version)-$(toybox_sha)-android
LOCAL_CFLAGS += -DTOYBOX_VERSION='"$(toybox_version)"'

LOCAL_CXX_STL := none
LOCAL_CLANG := true
LOCAL_PACK_MODULE_RELOCATIONS := false

TOYBOX_INSTLIST := $(HOST_OUT_EXECUTABLES)/toybox-instlist

include $(BUILD_EXECUTABLE)

toybox_symlinks: $(TOYBOX_INSTLIST)
toybox_symlinks:
	@mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin
	$(hide) $(TOYBOX_INSTLIST) | xargs -I'{}' ln -sf toybox '$(TARGET_RECOVERY_ROOT_OUT)/sbin/{}'
