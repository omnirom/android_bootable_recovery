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

include $(CLEAR_VARS)

BUSYBOX_LINKS := $(shell cat external/busybox/busybox-full.links)

# Exclusions:
#  fstools provides tune2fs and mke2fs
#  pigz provides gzip gunzip
#  dosfstools provides equivalents of mkdosfs mkfs.vfat
BUSYBOX_EXCLUDE := \
    gunzip \
    gzip \
    mkdosfs \
    mke2fs \
    mkfs.vfat \
    tune2fs

#  sh isn't working well in android 7.1, so relink mksh instead
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
    BUSYBOX_EXCLUDE += sh
endif

# Having /sbin/modprobe present on 32 bit devices can cause a massive
# performance problem if the kernel has CONFIG_MODULES=y
ifeq ($(filter $(TARGET_ARCH), arm64 x86_64),)
    BUSYBOX_EXCLUDE += modprobe
endif

RECOVERY_BUSYBOX_TOOLS := $(filter-out $(BUSYBOX_EXCLUDE), $(notdir $(BUSYBOX_LINKS)))

LOCAL_MODULE := busybox_symlinks
LOCAL_MODULE_TAGS := optional
LOCAL_POST_INSTALL_CMD := \
    $(hide) mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin && \
    $(foreach t,$(RECOVERY_BUSYBOX_TOOLS),ln -sf busybox $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

include $(BUILD_PHONY_PACKAGE)

BUSYBOX_EXCLUDE :=
BUSYBOX_LINKS :=
# RECOVERY_BUSYBOX_TOOLS should not be cleared
