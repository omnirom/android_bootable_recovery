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

tune2fs_static_libraries := \
    libext2_com_err \
    libext2_blkid \
    libext2_quota \
    libext2_uuid \
    libext2_e2p \
    libext2fs

updater_common_static_libraries := \
    libapplypatch \
    libbootloader_message \
    libbspatch \
    libedify \
    libotautil \
    libext4_utils \
    libdm \
    libfec \
    libfec_rs \
    libavb \
    libverity_tree \
    libgtest_prod \
    liblog \
    liblp \
    libselinux \
    libsparse \
    libsquashfs_utils \
    libbrotli \
    libbz \
    libziparchive \
    libz \
    libbase \
    libcrypto_static \
    libcrypto_utils \
    libcutils \
    libutils


# Each library in TARGET_RECOVERY_UPDATER_LIBS should have a function
# named "Register_<libname>()".  Here we emit a little C function that
# gets #included by updater.cpp.  It calls all those registration
# functions.
# $(1): the path to the register.inc file
# $(2): a list of TARGET_RECOVERY_UPDATER_LIBS
define generate-register-inc
    $(hide) mkdir -p $(dir $(1))
    $(hide) echo "" > $(1)
    $(hide) $(foreach lib,$(2),echo "extern void Register_$(lib)(void);" >> $(1);)
    $(hide) echo "void RegisterDeviceExtensions() {" >> $(1)
    $(hide) $(foreach lib,$(2),echo "  Register_$(lib)();" >> $(1);)
    $(hide) echo "}" >> $(1)
endef


# updater (static executable)
# ===============================
include $(CLEAR_VARS)

LOCAL_MODULE := updater

LOCAL_SRC_FILES := \
    updater_main.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include

LOCAL_CFLAGS := \
    -Wall \
    -Werror

LOCAL_STATIC_LIBRARIES := \
    libupdater_device \
    libupdater_core \
    $(TARGET_RECOVERY_UPDATER_LIBS) \
    $(TARGET_RECOVERY_UPDATER_EXTRA_LIBS) \
    $(updater_common_static_libraries) \
    libfs_mgr \
    libtune2fs \
    $(tune2fs_static_libraries)

LOCAL_MODULE_CLASS := EXECUTABLES
inc := $(call local-generated-sources-dir)/register.inc

# Devices can also add libraries to TARGET_RECOVERY_UPDATER_EXTRA_LIBS.
# These libs are also linked in with updater, but we don't try to call
# any sort of registration function for these.  Use this variable for
# any subsidiary static libraries required for your registered
# extension libs.
$(inc) : libs := $(TARGET_RECOVERY_UPDATER_LIBS)
$(inc) :
	$(call generate-register-inc,$@,$(libs))

LOCAL_GENERATED_SOURCES := $(inc)

inc :=

LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
