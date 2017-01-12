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
    libext2_uuid_static \
    libext2_e2p \
    libext2fs

updater_common_static_libraries := \
    libapplypatch \
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
    libsparse_static \
    libsquashfs_utils \
    libbz \
    libz \
    libbase \
    libcrypto \
    libcrypto_utils \
    libcutils \
    libtune2fs \
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
    -Wno-unused-parameter \
    -Werror

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
    -Wno-unused-parameter \
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

inc := $(call intermediates-dir-for,PACKAGING,updater_extensions)/register.inc

# Encode the value of TARGET_RECOVERY_UPDATER_LIBS into the filename of the dependency.
# So if TARGET_RECOVERY_UPDATER_LIBS is changed, a new dependency file will be generated.
# Note that we have to remove any existing depency files before creating new one,
# so no obsolete dependecy file gets used if you switch back to an old value.
inc_dep_file := $(inc).dep.$(subst $(space),-,$(sort $(TARGET_RECOVERY_UPDATER_LIBS)))
$(inc_dep_file): stem := $(inc).dep
$(inc_dep_file) :
	$(hide) mkdir -p $(dir $@)
	$(hide) rm -f $(stem).*
	$(hide) touch $@

$(inc) : libs := $(TARGET_RECOVERY_UPDATER_LIBS)
$(inc) : $(inc_dep_file)
	$(hide) mkdir -p $(dir $@)
	$(hide) echo "" > $@
	$(hide) $(foreach lib,$(libs),echo "extern void Register_$(lib)(void);" >> $@;)
	$(hide) echo "void RegisterDeviceExtensions() {" >> $@
	$(hide) $(foreach lib,$(libs),echo "  Register_$(lib)();" >> $@;)
	$(hide) echo "}" >> $@

$(call intermediates-dir-for,EXECUTABLES,updater,,,$(TARGET_PREFER_32_BIT))/updater.o : $(inc)
LOCAL_C_INCLUDES += $(dir $(inc))

inc :=
inc_dep_file :=

LOCAL_FORCE_STATIC_EXECUTABLE := true

include $(BUILD_EXECUTABLE)
