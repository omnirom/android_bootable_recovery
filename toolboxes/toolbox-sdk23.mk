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

include $(TOOLBOXES_PATH)/toolbox-local.mk

BSD_TOOLS := \
    dd

OUR_TOOLS := \
    getevent \
    iftop \
    ioctl \
    log \
    ls \
    nandread \
    newfs_msdos \
    prlimit \
    ps \
    restart \
    sendevent \
    start \
    stop \
    top

EXCLUDE_TOOLS := $(BUSYBOX_TWRP_TOOLS) $(TOYBOX_TWRP_TOOLS)

BSD_TOOLS_USED := $(filter-out $(EXCLUDE_TOOLS), $(BSD_TOOLS))
OUR_TOOLS_USED := $(filter-out $(EXCLUDE_TOOLS), $(OUR_TOOLS))
TOOLBOX_TOOLS_USED := $(BSD_TOOLS_USED) $(OUR_TOOLS_USED)

TWRP_TOOLS_USED := $(filter-out $(EXCLUDE_TOOLS) $(TOOLBOX_TOOLS_USED), $(TWRP_TOOLS))

TOOLBOX_TWRP_TOOLS := $(TOOLBOX_TOOLS_USED) $(TWRP_TOOLS_USED)

# Check whether toolbox even needs to be built.
# If not, jump to dummy module.
ifneq ($(TOOLBOX_TWRP_TOOLS),)

LOCAL_SRC_FILES := \
    toolbox.c \
    $(patsubst %,%.c,$(OUR_TOOLS_USED))

ifneq ($(TWRP_TOOLS_USED),)
    LOCAL_SRC_FILES += \
        $(patsubst %,../../../$(TOOLBOXES_PATH)/src/%.c,$(TWRP_TOOLS_USED))
endif
ifneq ($(filter $(TWRP_TOOLS_USED), getprop),)
    LOCAL_SRC_FILES += \
        ../../../$(TOOLBOXES_PATH)/src/dynarray.c
endif

LOCAL_CFLAGS += \
    -std=gnu99

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libselinux

ifneq ($(BSD_TOOLS_USED),)
    LOCAL_WHOLE_STATIC_LIBRARIES := \
        $(patsubst %,libtoolbox_%,$(BSD_TOOLS_USED))
endif

LOCAL_MODULE := toolbox_twrp
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

ifneq ($(filter-out $(EXCLUDE_TOOLS), r),)
    LOCAL_ADDITIONAL_DEPENDENCIES := r_twrp
endif

LOCAL_POST_INSTALL_CMD := \
    $(hide) mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin && \
    $(hide) $(foreach t,$(TOOLBOX_TWRP_TOOLS),ln -sf $(LOCAL_MODULE_STEM) $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

# Including this will define $(intermediates) below
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(TOOLBOX_TWRP_TOOLS)
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
LOCAL_MODULE := r_twrp
LOCAL_MODULE_STEM := r
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)

else

# Other toolboxes provide all of these tools,
# so there is no need to build toolbox.
LOCAL_MODULE := toolbox_twrp
LOCAL_MODULE_TAGS := optional
LOCAL_POST_INSTALL_CMD := echo "toolbox not needed; skipped"
include $(BUILD_PHONY_PACKAGE)

endif

BSD_TOOLS :=
BSD_TOOLS_USED :=
EXCLUDE_TOOLS :=
OUR_TOOLS :=
OUR_TOOLS_USED :=
TOOLBOX_TOOLS_USED :=
TWRP_TOOLS_USED :=
# TOOLBOX_TWRP_TOOLS should not be cleared here
