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

include $(CLEAR_VARS)

TWRPBOX_TOOLS := \
    getprop \
    lsof \
    setprop \
    watchprops

RECOVERY_TWRPBOX_TOOLS := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS) $(RECOVERY_TOOLBOX_TOOLS), $(TWRPBOX_TOOLS))

# Check whether toolbox needs to be built
# and jump to dummy package if not
ifneq ($(RECOVERY_TWRPBOX_TOOLS),)

LOCAL_SRC_FILES := \
    dynarray.c \
    toolbox.c \
    $(patsubst %,%.c,$(RECOVERY_TWRPBOX_TOOLS))

LOCAL_CFLAGS += \
    -std=gnu99

LOCAL_SHARED_LIBRARIES += \
    libcutils

LOCAL_C_INCLUDES += \
    system/core/include

LOCAL_CLANG := false

LOCAL_MODULE := twrpbox
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

LOCAL_POST_INSTALL_CMD := \
    $(hide) $(foreach t,$(RECOVERY_TWRPBOX_TOOLS),ln -sf $(LOCAL_MODULE) $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(RECOVERY_TWRPBOX_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(RECOVERY_TWRPBOX_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

else

# Other toolboxes provide all of these tools,
# so there is no need to build twrpbox
LOCAL_MODULE := twrpbox
LOCAL_MODULE_TAGS := optional
LOCAL_POST_INSTALL_CMD := echo "twrpbox not needed; skipped"
include $(BUILD_PHONY_PACKAGE)

endif

TWRPBOX_TOOLS :=
# RECOVERY_TWRPBOX_TOOLS should not be cleared here
