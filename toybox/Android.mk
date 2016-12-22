#
# Copyright (C) 2014 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

ifneq ($(wildcard external/toybox/Android.mk),)

ifeq ($(TW_USE_TOOLBOX), true)

LOCAL_PATH := external/toybox

#
# To update:
#

#  git remote add toybox https://github.com/landley/toybox.git
#  git fetch toybox
#  git merge toybox/master
#  mm -j32
#  # (Make any necessary Android.mk changes and test the new toybox.)
#  git push aosp HEAD:master  # Push directly, avoiding gerrit.
#  git push aosp HEAD:refs/for/master  # Push to gerrit.
#
#  # Now commit any necessary Android.mk changes like normal:
#  repo start post-sync .
#  git commit -a


#
# To add a toy:
#

#  make menuconfig
#  # (Select the toy you want to add.)
#  make clean && make  # Regenerate the generated files.
#  # Edit LOCAL_SRC_FILES below to add the toy.
#  # If you just want to use it as "toybox x" rather than "x", you can stop now.
#  # If you want this toy to have a symbolic link in /system/bin, add the toy to ALL_TOOLS.

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    lib/args.c \
    lib/dirtree.c \
    lib/getmountlist.c \
    lib/help.c \
    lib/interestingtimes.c \
    lib/lib.c \
    lib/llist.c \
    lib/net.c \
    lib/portability.c \
    lib/xwrap.c \
    main.c \
    toys/android/getenforce.c \
    toys/android/getprop.c \
    toys/android/load_policy.c \
    toys/android/restorecon.c \
    toys/android/runcon.c \
    toys/android/setenforce.c \
    toys/android/setprop.c \
    toys/lsb/dmesg.c \
    toys/lsb/hostname.c \
    toys/lsb/killall.c \
    toys/lsb/md5sum.c \
    toys/lsb/mknod.c \
    toys/lsb/mktemp.c \
    toys/lsb/mount.c \
    toys/lsb/pidof.c \
    toys/lsb/seq.c \
    toys/lsb/sync.c \
    toys/lsb/umount.c \
    toys/other/acpi.c \
    toys/other/base64.c \
    toys/other/blkid.c \
    toys/other/blockdev.c \
    toys/other/bzcat.c \
    toys/other/chcon.c \
    toys/other/chroot.c \
    toys/other/clear.c \
    toys/other/dos2unix.c \
    toys/other/fallocate.c \
    toys/other/free.c \
    toys/other/freeramdisk.c \
    toys/other/fsfreeze.c \
    toys/other/help.c \
    toys/other/inotifyd.c \
    toys/other/insmod.c \
    toys/other/losetup.c \
    toys/other/lsattr.c \
    toys/other/lsmod.c \
    toys/other/lsusb.c \
    toys/other/makedevs.c \
    toys/other/mkswap.c \
    toys/other/modinfo.c \
    toys/other/mountpoint.c \
    toys/other/nbd_client.c \
    toys/other/partprobe.c \
    toys/other/pivot_root.c \
    toys/other/pmap.c \
    toys/other/printenv.c \
    toys/other/pwdx.c \
    toys/other/readlink.c \
    toys/other/realpath.c \
    toys/other/rev.c \
    toys/other/rmmod.c \
    toys/other/setsid.c \
    toys/other/stat.c \
    toys/other/swapoff.c \
    toys/other/swapon.c \
    toys/other/sysctl.c \
    toys/other/tac.c \
    toys/other/taskset.c \
    toys/other/timeout.c \
    toys/other/truncate.c \
    toys/other/usleep.c \
    toys/other/vconfig.c \
    toys/other/vmstat.c \
    toys/other/which.c \
    toys/other/yes.c \
    toys/pending/dd.c \
    toys/pending/expr.c \
    toys/pending/more.c \
    toys/pending/route.c \
    toys/pending/tar.c \
    toys/pending/tr.c \
    toys/pending/traceroute.c \
    toys/posix/basename.c \
    toys/posix/cal.c \
    toys/posix/cat.c \
    toys/posix/chgrp.c \
    toys/posix/chmod.c \
    toys/posix/cksum.c \
    toys/posix/cmp.c \
    toys/posix/comm.c \
    toys/posix/cp.c \
    toys/posix/cpio.c \
    toys/posix/cut.c \
    toys/posix/date.c \
    toys/posix/df.c \
    toys/posix/dirname.c \
    toys/posix/du.c \
    toys/posix/echo.c \
    toys/posix/env.c \
    toys/posix/expand.c \
    toys/posix/false.c \
    toys/posix/find.c \
    toys/posix/grep.c \
    toys/posix/head.c \
    toys/posix/id.c \
    toys/posix/kill.c \
    toys/posix/ln.c \
    toys/posix/ls.c \
    toys/posix/mkdir.c \
    toys/posix/mkfifo.c \
    toys/posix/nice.c \
    toys/posix/nl.c \
    toys/posix/nohup.c \
    toys/posix/od.c \
    toys/posix/paste.c \
    toys/posix/patch.c \
    toys/posix/printf.c \
    toys/posix/pwd.c \
    toys/posix/renice.c \
    toys/posix/rm.c \
    toys/posix/rmdir.c \
    toys/posix/sed.c \
    toys/posix/sleep.c \
    toys/posix/sort.c \
    toys/posix/split.c \
    toys/posix/strings.c \
    toys/posix/tail.c \
    toys/posix/tee.c \
    toys/posix/time.c \
    toys/posix/touch.c \
    toys/posix/true.c \
    toys/posix/tty.c \
    toys/posix/uname.c \
    toys/posix/uniq.c \
    toys/posix/wc.c \
    toys/posix/xargs.c

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 23; echo $$?),0)
# there are some conflicts here with AOSP-7.[01] and CM-14.[01]
# the following items have been removed for compatibility
# ifconfig, netcat, netstat, rfkill, switch_root
LOCAL_STATIC_LIBRARIES := libcrypto_static

LOCAL_C_INCLUDES += \
    external/boringssl/include \
    bionic/libc/dns/include

LOCAL_SRC_FILES += \
    lib/linestack.c \
    lib/password.c \
    toys/other/flock.c \
    toys/other/hwclock.c \
    toys/other/ionice.c \
    toys/other/lspci.c \
    toys/other/readahead.c \
    toys/other/reset.c \
    toys/other/uptime.c \
    toys/other/xxd.c \
    toys/pending/arp.c \
    toys/pending/diff.c \
    toys/pending/ftpget.c \
    toys/pending/lsof.c \
    toys/pending/telnet.c \
    toys/pending/test.c \
    toys/pending/watch.c \
    toys/pending/xzcat.c \
    toys/posix/ps.c \
    toys/posix/ulimit.c

# Account for master branch changes pulld into CM14.1
ifneq ($(CM_BUILD),)
LOCAL_SRC_FILES += \
    toys/android/log.c \
    toys/android/sendevent.c \
    toys/android/start.c \
    toys/net/ifconfig.c \
    toys/net/netcat.c \
    toys/net/netstat.c \
    toys/net/rfkill.c \
    toys/net/tunctl.c \
    toys/other/setfattr.c \
    toys/pending/chrt.c \
    toys/pending/fdisk.c \
    toys/pending/getfattr.c \
    toys/pending/host.c \
    toys/pending/resize.c \
    toys/posix/file.c
else
LOCAL_SRC_FILES += \
    toys/other/ifconfig.c \
    toys/other/netcat.c \
    toys/other/rfkill.c \
    toys/other/switch_root.c \
    toys/pending/netstat.c
endif
else
LOCAL_SRC_FILES += \
    toys/other/ifconfig.c \
    toys/other/netcat.c \
    toys/other/rfkill.c \
    toys/other/switch_root.c \
    toys/pending/hwclock.c \
    toys/pending/netstat.c \
    toys/pending/pgrep.c \
    toys/pending/top.c
endif

LOCAL_CFLAGS += \
    -std=c99 \
    -Os \
    -Wno-char-subscripts \
    -Wno-sign-compare \
    -Wno-string-plus-int \
    -Wno-uninitialized \
    -Wno-unused-parameter \
    -funsigned-char \
    -ffunction-sections -fdata-sections \
    -fno-asynchronous-unwind-tables \

toybox_version := $(shell git -C $(LOCAL_PATH) rev-parse --short=12 HEAD 2>/dev/null)-android
LOCAL_CFLAGS += -DTOYBOX_VERSION='"$(toybox_version)"'

LOCAL_CLANG := true

LOCAL_SHARED_LIBRARIES := libcutils libselinux

LOCAL_MODULE := toybox_recovery
LOCAL_MODULE_STEM := toybox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

# dupes: dd df du ls mount renice
# useless?: freeramdisk fsfreeze install makedevs mkfifo nbd-client
#           partprobe pivot_root pwdx rev rfkill switch_root tty vconfig
# prefer BSD netcat instead?: nc netcat
# prefer efs2progs instead?: blkid chattr lsattr

ALL_TOOLS := \
    acpi \
    basename \
    blkid \
    blockdev \
    bzcat \
    cal \
    cat \
    chcon \
    chgrp \
    chmod \
    chown \
    chroot \
    cksum \
    clear \
    comm \
    cmp \
    cp \
    cpio \
    cut \
    date \
    dirname \
    dmesg \
    dos2unix \
    echo \
    env \
    expand \
    expr \
    fallocate \
    false \
    find \
    free \
    getenforce \
    getprop \
    groups \
    head \
    hostname \
    hwclock \
    id \
    ifconfig \
    inotifyd \
    insmod \
    kill \
    load_policy \
    ln \
    logname \
    losetup \
    lsmod \
    lsusb \
    md5sum \
    mkdir \
    mknod \
    mkswap \
    mktemp \
    modinfo \
    more \
    mountpoint \
    mv \
    netstat \
    nice \
    nl \
    nohup \
    od \
    paste \
    patch \
    pgrep \
    pidof \
    pkill \
    pmap \
    printenv \
    printf \
    pwd \
    readlink \
    realpath \
    restorecon \
    rm \
    rmdir \
    rmmod \
    route \
    runcon \
    sed \
    seq \
    setenforce \
    setprop \
    setsid \
    sha1sum \
    sleep \
    sort \
    split \
    stat \
    strings \
    swapoff \
    swapon \
    sync \
    sysctl \
    tac \
    tail \
    tar \
    taskset \
    tee \
    time \
    timeout \
    touch \
    tr \
    true \
    truncate \
    umount \
    uname \
    uniq \
    unix2dos \
    usleep \
    vmstat \
    wc \
    which \
    whoami \
    xargs \
    yes

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 23; echo $$?),0)
ALL_TOOLS += \
    arp \
    base64 \
    chattr \
    dd \
    df \
    diff \
    egrep \
    fgrep \
    flock \
    freeramdisk \
    fsfreeze \
    fstype \
    ftpget \
    ftpput \
    grep \
    help \
    install \
    ionice \
    iorenice \
    iotop \
    killall \
    ls \
    lsattr \
    lsof \
    lspci \
    makedevs \
    mkfifo \
    mount \
    nbd-client \
    nc \
    netcat \
    nproc \
    partprobe \
    pivot_root \
    ps \
    pwdx \
    readahead \
    renice \
    reset \
    rev \
    rfkill \
    sha224sum \
    sha256sum \
    sha384sum \
    sha512sum \
    telnet \
    test \
    top \
    traceroute \
    traceroute6 \
    tty \
    tunctl \
    ulimit \
    uptime \
    vconfig \
    watch \
    xxd \
    xzcat
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 24; echo $$?),0)
ALL_TOOLS += \
    du
endif
# Account for master branch changes pulld into CM14.1
ifneq ($(CM_BUILD),)
ALL_TOOLS += \
    chrt \
    fdisk \
    file \
    getfattr \
    host \
    log \
    resize \
    setfattr
endif
endif

# Install the symlinks.
LOCAL_POST_INSTALL_CMD := $(hide) $(foreach t,$(ALL_TOOLS),ln -sf toybox $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

include $(BUILD_EXECUTABLE)

# Make /sbin/toolbox launchers for each tool
SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(ALL_TOOLS))
$(SYMLINKS): TOYBOX_BINARY := $(LOCAL_MODULE_STEM)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE) $(LOCAL_PATH)/Android.mk
	@echo "Symlink: $@ -> $(TOYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(TOYBOX_BINARY) $@

include $(CLEAR_VARS)
LOCAL_MODULE := toybox_symlinks
LOCAL_MODULE_TAGS := optional
LOCAL_ADDITIONAL_DEPENDENCIES := $(SYMLINKS)
include $(BUILD_PHONY_PACKAGE)

endif

endif
