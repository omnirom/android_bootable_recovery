# Copyright (C) 2007 The Android Open Source Project
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

ifeq ($(call my-dir),$(call project-path-for,recovery))

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := fuse_sideload.c

LOCAL_CFLAGS := -O2 -g -DADB_HOST=0 -Wall -Wno-unused-parameter
LOCAL_CFLAGS += -D_XOPEN_SOURCE -D_GNU_SOURCE

LOCAL_MODULE := libfusesideload

LOCAL_STATIC_LIBRARIES := libcutils libc libmincrypt
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    recovery.cpp \
    bootloader.cpp \
    install.cpp \
    roots.cpp \
    ui.cpp \
    screen_ui.cpp \
    messagesocket.cpp \
    asn1_decoder.cpp \
    verifier.cpp \
    adb_install.cpp \
    fuse_sdcard_provider.c

# External tools
LOCAL_SRC_FILES += \
	../../system/core/toolbox/dynarray.c \
    ../../system/core/toolbox/getprop.c \
    ../../system/core/toolbox/newfs_msdos.c \
    ../../system/core/toolbox/setprop.c \
    ../../system/core/toolbox/start.c \
    ../../system/core/toolbox/stop.c \
    ../../system/core/toolbox/wipe.c \
    ../../system/vold/vdc.c

LOCAL_MODULE := recovery

LOCAL_FORCE_STATIC_EXECUTABLE := true

ifeq ($(HOST_OS),linux)
LOCAL_REQUIRED_MODULES := mkfs.f2fs
endif

RECOVERY_API_VERSION := 3
RECOVERY_FSTAB_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)
LOCAL_CFLAGS += -Wno-unused-parameter

LOCAL_STATIC_LIBRARIES := \
    libext4_utils_static \
    libmake_ext4fs_static \
    libminizip_static \
    libsparse_static \
    libfsck_msdos \
    libminipigz \
    libreboot_static \
    libvoldclient \
    libsdcard \
    libminzip \
    libz \
    libmtdutils \
    libmincrypt \
    libminadbd \
    libbusybox \
    libfusesideload \
    libminui \
    libpng \
    libfs_mgr \
    libcutils \
    liblog \
    libselinux \
    libstdc++ \
    libm \
    libc

# OEMLOCK support requires a device specific liboemlock be supplied.
# See comments in recovery.cpp for the API.
ifeq ($(TARGET_HAVE_OEMLOCK), true)
    LOCAL_CFLAGS += -DHAVE_OEMLOCK
    LOCAL_STATIC_LIBRARIES += liboemlock
endif

ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    LOCAL_CFLAGS += -DUSE_EXT4
    LOCAL_C_INCLUDES += system/extras/ext4_utils system/vold
    LOCAL_STATIC_LIBRARIES += libext4_utils_static libz
endif

LOCAL_CFLAGS += -DUSE_EXT4 -DMINIVOLD
LOCAL_C_INCLUDES += system/extras/ext4_utils system/core/fs_mgr/include external/fsck_msdos
LOCAL_C_INCLUDES += system/vold

ifneq ($(BOARD_RECOVERY_BLDRMSG_OFFSET),)
    LOCAL_CFLAGS += -DBOARD_RECOVERY_BLDRMSG_OFFSET=$(BOARD_RECOVERY_BLDRMSG_OFFSET)
endif

# Handling for EV_REL is disabled by default because some accelerometers
# send EV_REL events.  Actual EV_REL devices are rare on modern hardware
# so it's cleaner just to disable it by default.
ifneq ($(BOARD_RECOVERY_NEEDS_REL_INPUT),)
    LOCAL_CFLAGS += -DBOARD_RECOVERY_NEEDS_REL_INPUT
endif

# This binary is in the recovery ramdisk, which is otherwise a copy of root.
# It gets copied there in config/Makefile.  LOCAL_MODULE_TAGS suppresses
# a (redundant) copy of the binary in /system/bin for user builds.
# TODO: Build the ramdisk image in a more principled way.
LOCAL_MODULE_TAGS := eng

#ifeq ($(TARGET_RECOVERY_UI_LIB),)
  LOCAL_SRC_FILES += default_device.cpp
#else
#  LOCAL_STATIC_LIBRARIES += $(TARGET_RECOVERY_UI_LIB)
#endif

LOCAL_LDFLAGS += -Wl,--no-fatal-warnings

LOCAL_C_INCLUDES += system/extras/ext4_utils
LOCAL_C_INCLUDES += external/openssl/include

# Symlinks
RECOVERY_LINKS := busybox getprop reboot sdcard setup_adbd setprop start stop vdc

RECOVERY_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(RECOVERY_LINKS))

BUSYBOX_LINKS := $(shell cat external/busybox/busybox-minimal.links)
exclude := tune2fs mke2fs
RECOVERY_BUSYBOX_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(filter-out $(exclude),$(notdir $(BUSYBOX_LINKS))))

LOCAL_ADDITIONAL_DEPENDENCIES += \
    minivold \
    recovery_e2fsck \
    recovery_mke2fs \
    recovery_tune2fs \
    mount.exfat_static

LOCAL_ADDITIONAL_DEPENDENCIES += \
    bu_recovery

LOCAL_ADDITIONAL_DEPENDENCIES += $(RECOVERY_SYMLINKS) $(RECOVERY_BUSYBOX_SYMLINKS)

include $(BUILD_EXECUTABLE)

$(RECOVERY_SYMLINKS): RECOVERY_BINARY := $(LOCAL_MODULE)
$(RECOVERY_SYMLINKS):
	@echo "Symlink: $@ -> $(RECOVERY_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(RECOVERY_BINARY) $@

# Now let's do recovery symlinks
$(RECOVERY_BUSYBOX_SYMLINKS): BUSYBOX_BINARY := busybox
$(RECOVERY_BUSYBOX_SYMLINKS):
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@

include $(CLEAR_VARS)
LOCAL_MODULE := bu_recovery
LOCAL_MODULE_STEM := bu
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_SRC_FILES := \
    bu.cpp \
    backup.cpp \
    restore.cpp \
    messagesocket.cpp \
    roots.cpp
LOCAL_CFLAGS += -DMINIVOLD
LOCAL_CFLAGS += -Wno-unused-parameter
ifeq ($(TARGET_USERIMAGES_USE_EXT4), true)
    LOCAL_CFLAGS += -DUSE_EXT4
    LOCAL_C_INCLUDES += system/extras/ext4_utils
    LOCAL_STATIC_LIBRARIES += libext4_utils_static libz
endif
LOCAL_STATIC_LIBRARIES += \
    libsparse_static \
    libvoldclient \
    libz \
    libmtdutils \
    libminadbd \
    libminui \
    libfs_mgr \
    libtar \
    libcrypto_static \
    libselinux \
    libutils \
    libcutils \
    liblog \
    libm \
    libc

LOCAL_C_INCLUDES +=         	\
    system/core/fs_mgr/include	\
    system/core/include     	\
    system/core/libcutils       \
    system/vold                 \
    external/libtar             \
    external/libtar/listhash    \
    external/openssl/include    \
    external/zlib               \
    bionic/libc/bionic


include $(BUILD_EXECUTABLE)

# make_ext4fs
include $(CLEAR_VARS)
LOCAL_MODULE := libmake_ext4fs_static
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Dmain=make_ext4fs_main
LOCAL_SRC_FILES := ../../system/extras/ext4_utils/make_ext4fs_main.c
include $(BUILD_STATIC_LIBRARY)

# Minizip static library
include $(CLEAR_VARS)
LOCAL_MODULE := libminizip_static
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Dmain=minizip_main -D__ANDROID__ -DIOAPI_NO_64
LOCAL_C_INCLUDES := external/zlib
LOCAL_SRC_FILES := \
    ../../external/zlib/src/contrib/minizip/ioapi.c \
    ../../external/zlib/src/contrib/minizip/minizip.c \
    ../../external/zlib/src/contrib/minizip/zip.c
include $(BUILD_STATIC_LIBRARY)

# Reboot static library
include $(CLEAR_VARS)
LOCAL_MODULE := libreboot_static
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -Dmain=reboot_main
LOCAL_SRC_FILES := ../../system/core/reboot/reboot.c
include $(BUILD_STATIC_LIBRARY)

# All the APIs for testing
include $(CLEAR_VARS)
LOCAL_MODULE := libverifier
LOCAL_MODULE_TAGS := tests
LOCAL_SRC_FILES := \
    asn1_decoder.cpp
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := verifier_test
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS := tests
LOCAL_CFLAGS += -DNO_RECOVERY_MOUNT
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_SRC_FILES := \
    verifier_test.cpp \
    asn1_decoder.cpp \
    verifier.cpp \
    ui.cpp \
    messagesocket.cpp
LOCAL_STATIC_LIBRARIES := \
    libvoldclient \
    libmincrypt \
    libminui \
    libminzip \
    libcutils \
    libstdc++ \
    libc
include $(BUILD_EXECUTABLE)


include $(LOCAL_PATH)/minui/Android.mk \
    $(LOCAL_PATH)/minzip/Android.mk \
    $(LOCAL_PATH)/minadbd/Android.mk \
    $(LOCAL_PATH)/mtdutils/Android.mk \
    $(LOCAL_PATH)/tests/Android.mk \
    $(LOCAL_PATH)/tools/Android.mk \
    $(LOCAL_PATH)/edify/Android.mk \
    $(LOCAL_PATH)/uncrypt/Android.mk \
    $(LOCAL_PATH)/updater/Android.mk \
    $(LOCAL_PATH)/applypatch/Android.mk \
    $(LOCAL_PATH)/voldclient/Android.mk

endif
