# Copyright 2014 TWRP/TeamWin Recovery Project
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

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := batteryled.cpp

LOCAL_C_INCLUDES += bionic external/stlport/stlport

LOCAL_STATIC_LIBRARIES += libguitwrp
LOCAL_SHARED_LIBRARIES += libstlport libstdc++

ifneq ($(TW_CUSTOM_BATTERY_PATH),)
	LOCAL_CFLAGS += -DTW_CUSTOM_BATTERY_PATH=$(TW_CUSTOM_BATTERY_PATH)
endif
ifeq ($(TW_BATTERY_LED), htc-legacy)
	LOCAL_CFLAGS += -DHTC_LEGACY_LED
endif

LOCAL_MODULE := batteryled
LOCAL_MODULE_TAGS := optional eng
include $(BUILD_STATIC_LIBRARY)
