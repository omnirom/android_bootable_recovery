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

LOCAL_PATH := system/core/toolbox

include $(CLEAR_VARS)

BSD_TOOLS := \
    dd \
    du

OUR_TOOLS := \
    df \
    getevent \
    iftop \
    ioctl \
    ionice \
    log \
    ls \
    lsof \
    mount \
    nandread \
    newfs_msdos \
    prlimit \
    ps \
    renice \
    sendevent \
    start \
    stop \
    top \
    uptime \
    watchprops

BSD_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(BSD_TOOLS))
OUR_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(OUR_TOOLS))
RECOVERY_TOOLBOX_TOOLS := $(BSD_TOOLS_USED) $(OUR_TOOLS_USED)

ifeq ($(RECOVERY_TOOLBOX_TOOLS),)

# other toolboxes provide all of these tools,
# so there is no need to build toolbox
$(toolbox_recovery):
	@echo "toolbox utility skipped"

else

LOCAL_SRC_FILES := \
    toolbox.c \
    $(patsubst %,%.c,$(OUR_TOOLS_USED))

LOCAL_CFLAGS += \
    -std=gnu99

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libselinux

ifneq ($(BSD_TOOLS_USED),)
    LOCAL_WHOLE_STATIC_LIBRARIES := \
        $(patsubst %,libtoolbox_%,$(BSD_TOOLS_USED))
endif

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

LOCAL_ADDITIONAL_DEPENDENCIES := r_recovery

LOCAL_POST_INSTALL_CMD := \
    $(hide) $(foreach t,$(RECOVERY_TOOLBOX_TOOLS),ln -sf $(LOCAL_MODULE_STEM) $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

# Including this will define $(intermediates) below
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(RECOVERY_TOOLBOX_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

$(LOCAL_PATH)/getevent.c: $(intermediates)/input.h-labels.h

INPUT_H_LABELS_H := $(intermediates)/input.h-labels.h
$(INPUT_H_LABELS_H): PRIVATE_LOCAL_PATH := $(LOCAL_PATH)
$(INPUT_H_LABELS_H): PRIVATE_CUSTOM_TOOL = $(PRIVATE_LOCAL_PATH)/generate-input.h-labels.py > $@
$(INPUT_H_LABELS_H): $(LOCAL_PATH)/Android.mk $(LOCAL_PATH)/generate-input.h-labels.py
$(INPUT_H_LABELS_H):
	$(transform-generated-source)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := r.c
LOCAL_MODULE := r_recovery
LOCAL_MODULE_STEM := r
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)

endif

BSD_TOOLS :=
BSD_TOOLS_USED :=
OUR_TOOLS :=
OUR_TOOLS_USED :=
RECOVERY_TOOLBOX_TOOLS :=
