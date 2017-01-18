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

LOCAL_PATH := $(call my-dir)
TOOLBOX_PATH := $(LOCAL_PATH)

include $(CLEAR_VARS)

# Inheritance order of these included makefiles is important
# 1) Primary tool package (busybox or toybox) is included first
# 2) Additional tools (toolbox + locally provided tools) are 
#    only built if they are missing from the primary tool package

ifeq ($(TW_USE_TOOLBOX), true)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 23; echo $$?),0)
        include $(TOOLBOX_PATH)/toybox.mk
    endif
else
    include $(TOOLBOX_PATH)/busybox.mk
endif

ifneq ($(filter $(PLATFORM_SDK_VERSION), 19 22 23 25),)
    include $(TOOLBOX_PATH)/toolbox-sdk$(PLATFORM_SDK_VERSION).mk
else
    LOCAL_MODULE := toolbox_recovery
    LOCAL_MODULE_TAGS := optional
    LOCAL_POST_INSTALL_CMD := echo "No SDK matched, toolbox utility skipped"
    include $(BUILD_PHONY_PACKAGE)
endif
