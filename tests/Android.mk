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

LOCAL_PATH := $(call my-dir)

# libapplypatch, libapplypatch_modes, libimgdiff, libimgpatch.
libapplypatch_static_libraries := \
    libapplypatch_modes \
    libapplypatch \
    libedify \
    libimgdiff \
    libimgpatch \
    libotafault \
    libotautil \
    libbsdiff \
    libbspatch \
    libdivsufsort \
    libdivsufsort64 \
    libutils \
    libbase \
    libbrotli \
    libbz \
    libcrypto \
    libz \
    libziparchive \

# Unit tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE := recovery_unit_test
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_STATIC_LIBRARIES := \
    $(libapplypatch_static_libraries) \
    libverifier \
    librecovery_ui \
    libminui \
    libotautil \
    libupdater \
    libgtest_prod \
    libpng \
    libziparchive \
    libutils \
    libz \
    libselinux \
    libbase \
    libBionicGtestMain

LOCAL_SRC_FILES := \
    unit/applypatch_test.cpp \
    unit/asn1_decoder_test.cpp \
    unit/commands_test.cpp \
    unit/dirutil_test.cpp \
    unit/locale_test.cpp \
    unit/rangeset_test.cpp \
    unit/screen_ui_test.cpp \
    unit/sysutil_test.cpp \
    unit/zip_test.cpp

LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_TEST_DATA := \
    $(call find-test-data-in-subdirs, $(LOCAL_PATH), "*", testdata)
include $(BUILD_NATIVE_TEST)

# Manual tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE := recovery_manual_test
LOCAL_STATIC_LIBRARIES := \
    libbase \
    libBionicGtestMain

LOCAL_SRC_FILES := manual/recovery_test.cpp
LOCAL_SHARED_LIBRARIES := \
    liblog

include $(BUILD_NATIVE_TEST)

# Component tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := \
    -Wall \
    -Werror \
    -D_FILE_OFFSET_BITS=64

LOCAL_MODULE := recovery_component_test
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SRC_FILES := \
    component/applypatch_modes_test.cpp \
    component/bootloader_message_test.cpp \
    component/edify_test.cpp \
    component/imgdiff_test.cpp \
    component/install_test.cpp \
    component/resources_test.cpp \
    component/sideload_test.cpp \
    component/uncrypt_test.cpp \
    component/updater_test.cpp \
    component/update_verifier_test.cpp \
    component/verifier_test.cpp

LOCAL_SHARED_LIBRARIES := \
    libhidlbase

tune2fs_static_libraries := \
    libext2_com_err \
    libext2_blkid \
    libext2_quota \
    libext2_uuid \
    libext2_e2p \
    libext2fs

libupdater_static_libraries := \
    libupdater \
    libapplypatch \
    libbspatch \
    libedify \
    libziparchive \
    libotautil \
    libbootloader_message \
    libutils \
    libotafault \
    libext4_utils \
    libfec \
    libfec_rs \
    libfs_mgr \
    libgtest_prod \
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

health_hal_static_libraries := \
    android.hardware.health@2.0-impl \
    android.hardware.health@2.0 \
    android.hardware.health@1.0 \
    android.hardware.health@1.0-convert \
    libhealthstoragedefault \
    libhidltransport \
    libhidlbase \
    libhwbinder_noltopgo \
    libvndksupport \
    libbatterymonitor

librecovery_static_libraries := \
    librecovery \
    $(TARGET_RECOVERY_UI_LIB) \
    libbootloader_message \
    libfusesideload \
    libminadbd \
    librecovery_ui \
    libminui \
    libverifier \
    libotautil \
    $(health_hal_static_libraries) \
    libasyncio \
    libcrypto_utils \
    libcrypto \
    libext4_utils \
    libfs_mgr \
    libpng \
    libsparse \
    libvintf_recovery \
    libvintf \
    libhidl-gen-utils \
    libtinyxml2 \
    libziparchive \
    libbase \
    libutils \
    libcutils \
    liblog \
    libselinux \
    libz \

libupdate_verifier_static_libraries := \
    libupdate_verifier \

LOCAL_STATIC_LIBRARIES := \
    $(libapplypatch_static_libraries) \
    $(librecovery_static_libraries) \
    $(libupdate_verifier_static_libraries) \
    $(libupdater_static_libraries) \
    libBionicGtestMain

LOCAL_TEST_DATA := \
    $(call find-test-data-in-subdirs, $(LOCAL_PATH), "*", testdata) \
    $(call find-test-data-in-subdirs, bootable/recovery, "*_text.png", res-*)
include $(BUILD_NATIVE_TEST)

# Host tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE := recovery_host_test
LOCAL_MODULE_HOST_OS := linux
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SRC_FILES := \
    component/imgdiff_test.cpp
LOCAL_STATIC_LIBRARIES := \
    libimgdiff \
    libimgpatch \
    libotautil \
    libbsdiff \
    libbspatch \
    libziparchive \
    libutils \
    libbase \
    libcrypto \
    libbrotli \
    libbz \
    libdivsufsort64 \
    libdivsufsort \
    libz \
    libBionicGtestMain
LOCAL_SHARED_LIBRARIES := \
    liblog
LOCAL_TEST_DATA := \
    $(call find-test-data-in-subdirs, $(LOCAL_PATH), "*", testdata)
include $(BUILD_HOST_NATIVE_TEST)
