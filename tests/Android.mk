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

# Unit tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := -Wall -Werror
LOCAL_MODULE := recovery_unit_test
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_STATIC_LIBRARIES := \
    libverifier \
    librecovery_ui \
    libminui \
    libotautil \
    libupdater \
    libziparchive \
    libutils \
    libz \
    libselinux \
    libbase \
    libBionicGtestMain

LOCAL_SRC_FILES := \
    unit/asn1_decoder_test.cpp \
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
    libminui \
    libbase \
    libBionicGtestMain

LOCAL_SRC_FILES := manual/recovery_test.cpp
LOCAL_SHARED_LIBRARIES := \
    liblog \
    libpng

LOCAL_TEST_DATA := \
    $(call find-test-data-in-subdirs, bootable/recovery, "*_text.png", res-*)
include $(BUILD_NATIVE_TEST)

# Component tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := \
    -Wall \
    -Werror \
    -D_FILE_OFFSET_BITS=64

ifeq ($(AB_OTA_UPDATER),true)
LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
endif

ifeq ($(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_SUPPORTS_VERITY),true)
LOCAL_CFLAGS += -DPRODUCT_SUPPORTS_VERITY=1
endif

ifeq ($(BOARD_AVB_ENABLE),true)
LOCAL_CFLAGS += -DBOARD_AVB_ENABLE=1
endif

LOCAL_MODULE := recovery_component_test
LOCAL_COMPATIBILITY_SUITE := device-tests
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SRC_FILES := \
    component/applypatch_test.cpp \
    component/bootloader_message_test.cpp \
    component/edify_test.cpp \
    component/imgdiff_test.cpp \
    component/install_test.cpp \
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

LOCAL_STATIC_LIBRARIES := \
    libapplypatch_modes \
    libapplypatch \
    libedify \
    libimgdiff \
    libimgpatch \
    libbsdiff \
    libbspatch \
    libfusesideload \
    libotafault \
    librecovery \
    libupdater \
    libbootloader_message \
    libverifier \
    libotautil \
    libmounts \
    libupdate_verifier \
    libdivsufsort \
    libdivsufsort64 \
    libfs_mgr \
    libvintf_recovery \
    libvintf \
    libhidl-gen-utils \
    libtinyxml2 \
    libselinux \
    libext4_utils \
    libsparse \
    libcrypto_utils \
    libcrypto \
    libbz \
    libziparchive \
    liblog \
    libutils \
    libz \
    libbase \
    libtune2fs \
    libfec \
    libfec_rs \
    libsquashfs_utils \
    libcutils \
    libbrotli \
    libBionicGtestMain \
    $(tune2fs_static_libraries)

LOCAL_TEST_DATA := \
    $(call find-test-data-in-subdirs, $(LOCAL_PATH), "*", testdata)
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
