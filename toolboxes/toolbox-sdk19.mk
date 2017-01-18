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

TOOLS := \
    cat \
    chcon \
    chmod \
    chown \
    clear \
    cmp \
    date \
    dd \
    df \
    dmesg \
    du \
    getenforce \
    getevent \
    getprop \
    getsebool \
    hd \
    id \
    ifconfig \
    iftop \
    insmod \
    ioctl \
    ionice \
    kill \
    ln \
    load_policy \
    log \
    ls \
    lsmod \
    lsof \
    md5 \
    mkdir \
    mkswap \
    mount \
    mv \
    nandread \
    netstat \
    newfs_msdos \
    notify \
    printenv \
    ps \
    r \
    readlink \
    renice \
    restorecon \
    rm \
    rmdir \
    rmmod \
    route \
    runcon \
    schedtop \
    sendevent \
    setconsole \
    setenforce \
    setprop \
    setsebool \
    sleep \
    smd \
    start \
    stop \
    swapoff \
    swapon \
    sync \
    top \
    touch \
    umount \
    uptime \
    vmstat \
    watchprops \
    wipe

OTHER_TOOLS := \
    cp \
    grep

EXCLUDE_TOOLS := $(BUSYBOX_TWRP_TOOLS) $(TOYBOX_TWRP_TOOLS)

TOOLS_USED := $(filter-out $(EXCLUDE_TOOLS), $(TOOLS))
OTHER_TOOLS_USED := $(filter-out $(EXCLUDE_TOOLS), $(OTHER_TOOLS))
TOOLBOX_TOOLS_USED := $(TOOLS_USED) $(OTHER_TOOLS_USED)

TWRP_TOOLS_USED := $(filter-out $(EXCLUDE_TOOLS) $(TOOLBOX_TOOLS_USED), $(TWRP_TOOLS))

TOOLBOX_TWRP_TOOLS := $(TOOLBOX_TOOLS_USED) $(TWRP_TOOLS_USED)

# Check whether toolbox even needs to be built.
# If not, jump to dummy module.
ifneq ($(TOOLBOX_TWRP_TOOLS),)

LOCAL_SRC_FILES := \
    dynarray.c \
    toolbox.c \
    $(patsubst %,%.c,$(TOOLS_USED))

ifneq ($(TWRP_TOOLS_USED),)
    LOCAL_SRC_FILES += \
        $(patsubst %,../../../$(TOOLBOXES_PATH)/src/%.c,$(TWRP_TOOLS_USED))
endif

ifneq ($(filter $(OTHER_TOOLS_USED), cp),)
    LOCAL_SRC_FILES += \
        cp/cp.c \
        cp/utils.c
endif
ifneq ($(filter $(OTHER_TOOLS_USED), grep),)
    LOCAL_SRC_FILES += \
        grep/fastgrep.c \
        grep/file.c \
        grep/grep.c \
        grep/queue.c \
        grep/util.c
endif

LOCAL_C_INCLUDES += \
    bionic/libc/bionic

LOCAL_SHARED_LIBRARIES := \
    libc \
    libcutils \
    liblog \
    libselinux \
    libusbhost

LOCAL_MODULE := toolbox_twrp
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

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

else

# Other toolboxes provide all of these tools,
# so there is no need to build toolbox.
LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_TAGS := optional
LOCAL_POST_INSTALL_CMD := echo "toolbox not needed; skipped"
include $(BUILD_PHONY_PACKAGE)

endif

EXCLUDE_TOOLS :=
OTHER_TOOLS :=
OTHER_TOOLS_USED :=
TOOLBOX_TOOLS_USED :=
TOOLS :=
TOOLS_USED :=
TWRP_TOOLS_USED :=
# TOOLBOX_TWRP_TOOLS should not be cleared here
