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

TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(TOOLS))
OTHER_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(OTHER_TOOLS))
RECOVERY_TOOLBOX_TOOLS := $(TOOLS_USED) $(OTHER_TOOLS_USED)

# Check whether toolbox needs to be built
# and jump to dummy package if not
ifneq ($(RECOVERY_TOOLBOX_TOOLS),)

LOCAL_SRC_FILES := \
    dynarray.c \
    toolbox.c \
    $(patsubst %,%.c,$(TOOLS_USED))

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

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

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

else

# Other toolboxes provide all of these tools,
# so there is no need to build toolbox
LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_TAGS := optional
LOCAL_POST_INSTALL_CMD := echo "toolbox not needed; skipped"
include $(BUILD_PHONY_PACKAGE)

endif

OTHER_TOOLS :=
OTHER_TOOLS_USED :=
TOOLS :=
TOOLS_USED :=
# RECOVERY_TOOLBOX_TOOLS should not be cleared here
