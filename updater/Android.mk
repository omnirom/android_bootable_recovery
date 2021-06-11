# Copyright 2009 The Android Open Source Project
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

LOCAL_PATH := $(call my-dir)

ifneq ($(wildcard external/e2fsprogs/misc/tune2fs.h),)
    tune2fs_static_libraries := \
        libext2_com_err \
        libext2_blkid \
        libext2_quota \
        libext2_uuid \
        libext2_e2p \
        libext2fs
    LOCAL_CFLAGS += -DHAVE_LIBTUNE2FS
else
    tune2fs_static_libraries :=
endif

updater_common_static_libraries := \
    libapplypatch \
    libbspatch \
    libedify \
    libziparchive \
    libotautil \
    libbootloader_message \
    libutils \
    libmounts \
    libotafault \
    libext4_utils \
    libfec \
    libfec_rs \
    libfs_mgr \
    liblog \
    libselinux \
    libsparse \
    libsquashfs_utils \
    libbz \
    libz \
    libbase \
    libcrypto \
    libcrypto_utils \
    libcutils \
    libtune2fs \
    libbrotli \
    $(tune2fs_static_libraries)

# libupdater (static library)
# ===============================
include $(CLEAR_VARS)

LOCAL_MODULE := libupdater

LOCAL_SRC_FILES := \
    install.cpp \
    blockimg.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/.. \
    $(LOCAL_PATH)/include \
    external/e2fsprogs/misc

LOCAL_CFLAGS := \
    -Wall \
    -Werror

ifeq ($(BOARD_SUPPRESS_EMMC_WIPE),true)
    LOCAL_CFLAGS += -DSUPPRESS_EMMC_WIPE
endif

LOCAL_EXPORT_C_INCLUDE_DIRS := \
    $(LOCAL_PATH)/include

LOCAL_STATIC_LIBRARIES := \
    $(updater_common_static_libraries)

include $(BUILD_STATIC_LIBRARY)

# updater (static executable)
# ===============================
include $(CLEAR_VARS)

LOCAL_MODULE := updater

LOCAL_SRC_FILES := \
    updater.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/.. \
    $(LOCAL_PATH)/include

LOCAL_CFLAGS := \
    -Wall \
    -Werror

LOCAL_STATIC_LIBRARIES := \
    libupdater \
    $(TARGET_RECOVERY_UPDATER_LIBS) \
    $(TARGET_RECOVERY_UPDATER_EXTRA_LIBS) \
    $(updater_common_static_libraries)

# Each library in TARGET_RECOVERY_UPDATER_LIBS should have a function
# named "Register_<libname>()".  Here we emit a little C function that
# gets #included by updater.c.  It calls all those registration
# functions.

# Devices can also add libraries to TARGET_RECOVERY_UPDATER_EXTRA_LIBS.
# These libs are also linked in with updater, but we don't try to call
# any sort of registration function for these.  Use this variable for
# any subsidiary static libraries required for your registered
# extension libs.

LOCAL_MODULE_CLASS := EXECUTABLES
inc := $(call local-generated-sources-dir)/register.inc

$(inc) : libs := $(TARGET_RECOVERY_UPDATER_LIBS)
$(inc) :
	$(hide) mkdir -p $(dir $@)
	$(hide) echo "" > $@
	$(hide) $(foreach lib,$(libs),echo "extern void Register_$(lib)(void);" >> $@;)
	$(hide) echo "void RegisterDeviceExtensions() {" >> $@
	$(hide) $(foreach lib,$(libs),echo "  Register_$(lib)();" >> $@;)
	$(hide) echo "}" >> $@

LOCAL_GENERATED_SOURCES := $(inc)

inc :=

LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
