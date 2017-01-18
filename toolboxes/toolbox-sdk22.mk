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

TOOLBOXES_PATH := $(call my-dir)
LOCAL_PATH := system/core/toolbox

include $(CLEAR_VARS)

include $(TOOLBOXES_PATH)/toolbox-local.mk

BSD_TOOLS := \
    cat \
    chown \
    cp \
    dd \
    du \
    grep \
    kill \
    ln \
    mv \
    printenv \
    rm \
    rmdir \
    sleep \
    sync

OUR_TOOLS := \
    chcon \
    chmod \
    clear \
    cmp \
    date \
    df \
    dmesg \
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
    load_policy \
    log \
    ls \
    lsmod \
    lsof \
    md5 \
    mkdir \
    mknod \
    mkswap \
    mount \
    nandread \
    netstat \
    newfs_msdos \
    nohup \
    notify \
    prlimit \
    ps \
    r \
    readlink \
    renice \
    restorecon \
    rmmod \
    route \
    runcon \
    schedtop \
    sendevent \
    setenforce \
    setprop \
    setsebool \
    smd \
    start \
    stop \
    swapoff \
    swapon \
    top \
    touch \
    umount \
    uptime \
    vmstat \
    watchprops \
    wipe

BSD_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(BSD_TOOLS))
OUR_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(OUR_TOOLS))
TWRP_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS) \
                                $(BSD_TOOLS_USED) $(OUR_TOOLS_USED), $(TWRP_TOOLS))
RECOVERY_TOOLBOX_TOOLS := $(BSD_TOOLS_USED) $(OUR_TOOLS_USED) $(TWRP_TOOLS_USED)

# Check whether toolbox needs to be built
# and jump to dummy package if not
ifneq ($(RECOVERY_TOOLBOX_TOOLS),)

LOCAL_SRC_FILES := \
    upstream-netbsd/lib/libc/gen/getbsize.c \
    upstream-netbsd/lib/libc/gen/humanize_number.c \
    upstream-netbsd/lib/libc/stdlib/strsuftoll.c \
    upstream-netbsd/lib/libc/string/swab.c \
    upstream-netbsd/lib/libutil/raise_default_signal.c \
    dynarray.c \
    pwcache.c \
    toolbox.c \
    $(patsubst %,%.c,$(OUR_TOOLS_USED))

ifneq ($(TWRP_TOOLS_USED),)
    LOCAL_SRC_FILES += \
        $(patsubst %,../../../$(TOOLBOXES_PATH)/src/%.c,$(TWRP_TOOLS_USED))
endif

LOCAL_CFLAGS += \
    -std=gnu99

LOCAL_C_INCLUDES += \
    external/openssl/include

LOCAL_SHARED_LIBRARIES := \
    libcrypto \
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

BSD_TOOLS :=
BSD_TOOLS_USED :=
OUR_TOOLS :=
OUR_TOOLS_USED :=
# RECOVERY_TOOLBOX_TOOLS should not be cleared here
