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

# LOCAL_PATH := $(call my-dir)

# Unit tests
# include $(CLEAR_VARS)
# LOCAL_CFLAGS := -Wall -Werror
# LOCAL_MODULE := recovery_unit_test
# LOCAL_COMPATIBILITY_SUITE := device-tests
# LOCAL_STATIC_LIBRARIES := \
#     libverifier \
#     libminui \
#     libotautil \
#     libupdater \
#     libziparchive \
#     libutils \
#     libz \
#     libselinux \
#     libbase \
#     libBionicGtestMain

# LOCAL_SRC_FILES := \
#     unit/asn1_decoder_test.cpp \
#     unit/dirutil_test.cpp \
#     unit/locale_test.cpp \
#     unit/rangeset_test.cpp \
#     unit/sysutil_test.cpp \
#     unit/zip_test.cpp \
#     unit/ziputil_test.cpp

# LOCAL_C_INCLUDES := $(commands_recovery_local_path)
# LOCAL_SHARED_LIBRARIES := liblog
# include $(BUILD_NATIVE_TEST)

# Manual tests
# include $(CLEAR_VARS)
# LOCAL_CFLAGS := -Wall -Werror
# LOCAL_MODULE := recovery_manual_test
# LOCAL_STATIC_LIBRARIES := \
#     libminui \
#     libbase \
#     libBionicGtestMain

# LOCAL_SRC_FILES := manual/recovery_test.cpp
# LOCAL_SHARED_LIBRARIES := \
#     liblog \
#     libpng

# resource_files := $(call find-files-in-subdirs, bootable/recovery, \
#     "*_text.png", \
#     res-mdpi/images \
#     res-hdpi/images \
#     res-xhdpi/images \
#     res-xxhdpi/images \
#     res-xxxhdpi/images \
#     )

# # The resource image files that will go to $OUT/data/nativetest/recovery.
# testimage_out_path := $(TARGET_OUT_DATA)/nativetest/recovery
# GEN := $(addprefix $(testimage_out_path)/, $(resource_files))

# $(GEN): PRIVATE_PATH := $(LOCAL_PATH)
# $(GEN): PRIVATE_CUSTOM_TOOL = cp $< $@
# $(GEN): $(testimage_out_path)/% : bootable/recovery/%
# 	$(transform-generated-source)
# LOCAL_GENERATED_SOURCES += $(GEN)

# include $(BUILD_NATIVE_TEST)

# Component tests
# include $(CLEAR_VARS)
# LOCAL_CFLAGS := \
#     -Wall \
#     -Werror \
#     -D_FILE_OFFSET_BITS=64

# ifeq ($(AB_OTA_UPDATER),true)
# LOCAL_CFLAGS += -DAB_OTA_UPDATER=1
# endif

# ifeq ($(PRODUCTS.$(INTERNAL_PRODUCT).PRODUCT_SUPPORTS_VERITY),true)
# LOCAL_CFLAGS += -DPRODUCT_SUPPORTS_VERITY=1
# endif

# ifeq ($(BOARD_AVB_ENABLE),true)
# LOCAL_CFLAGS += -DBOARD_AVB_ENABLE=1
# endif

# LOCAL_MODULE := recovery_component_test
# LOCAL_COMPATIBILITY_SUITE := device-tests
# LOCAL_C_INCLUDES := $(commands_recovery_local_path)
# LOCAL_SRC_FILES := \
#     component/applypatch_test.cpp \
#     component/bootloader_message_test.cpp \
#     component/edify_test.cpp \
#     component/imgdiff_test.cpp \
#     component/install_test.cpp \
#     component/sideload_test.cpp \
#     component/uncrypt_test.cpp \
#     component/updater_test.cpp \
#     component/update_verifier_test.cpp \
#     component/verifier_test.cpp

# LOCAL_SHARED_LIBRARIES := \
#     libhidlbase

# tune2fs_static_libraries := \
#     libext2_com_err \
#     libext2_blkid \
#     libext2_quota \
#     libext2_uuid \
#     libext2_e2p \
#     libext2fs

# LOCAL_STATIC_LIBRARIES := \
#     libapplypatch_modes \
#     libapplypatch \
#     libedify \
#     libimgdiff \
#     libimgpatch \
#     libbsdiff \
#     libbspatch \
#     libfusesideload \
#     libotafault \
#     librecovery \
#     libupdater \
#     libbootloader_message \
#     libverifier \
#     libotautil \
#     libmounts \
#     libupdate_verifier \
#     libdivsufsort \
#     libdivsufsort64 \
#     libfs_mgr \
#     libvintf_recovery \
#     libvintf \
#     libhidl-gen-utils \
#     libtinyxml2 \
#     libselinux \
#     libext4_utils \
#     libsparse \
#     libcrypto_utils \
#     libcrypto \
#     libbz \
#     libziparchive \
#     liblog \
#     libutils \
#     libz \
#     libbase \
#     libtune2fs \
#     libfec \
#     libfec_rs \
#     libsquashfs_utils \
#     libcutils \
#     libbrotli \
#     libBionicGtestMain \
#     $(tune2fs_static_libraries)

# testdata_files := $(call find-subdir-files, testdata/*)

# # The testdata files that will go to $OUT/data/nativetest/recovery.
# testdata_out_path := $(TARGET_OUT_DATA)/nativetest/recovery
# GEN := $(addprefix $(testdata_out_path)/, $(testdata_files))
# $(GEN): PRIVATE_PATH := $(LOCAL_PATH)
# $(GEN): PRIVATE_CUSTOM_TOOL = cp $< $@
# $(GEN): $(testdata_out_path)/% : $(LOCAL_PATH)/%
# 	$(transform-generated-source)
# LOCAL_GENERATED_SOURCES += $(GEN)

# # A copy of the testdata to be packed into continuous_native_tests.zip.
# testdata_continuous_zip_prefix := \
#     $(call intermediates-dir-for,PACKAGING,recovery_component_test)/DATA
# testdata_continuous_zip_path := $(testdata_continuous_zip_prefix)/nativetest/recovery
# GEN := $(addprefix $(testdata_continuous_zip_path)/, $(testdata_files))
# $(GEN): PRIVATE_PATH := $(LOCAL_PATH)
# $(GEN): PRIVATE_CUSTOM_TOOL = cp $< $@
# $(GEN): $(testdata_continuous_zip_path)/% : $(LOCAL_PATH)/%
# 	$(transform-generated-source)
# LOCAL_GENERATED_SOURCES += $(GEN)
# LOCAL_PICKUP_FILES := $(testdata_continuous_zip_prefix)

# include $(BUILD_NATIVE_TEST)

# Host tests
# include $(CLEAR_VARS)
# LOCAL_CFLAGS := -Wall -Werror
# LOCAL_MODULE := recovery_host_test
# LOCAL_MODULE_HOST_OS := linux
# LOCAL_C_INCLUDES := bootable/recovery
# LOCAL_SRC_FILES := \
#     component/imgdiff_test.cpp
# LOCAL_STATIC_LIBRARIES := \
#     libimgdiff \
#     libimgpatch \
#     libotautil \
#     libbsdiff \
#     libbspatch \
#     libziparchive \
#     libutils \
#     libbase \
#     libcrypto \
#     libbrotli \
#     libbz \
#     libdivsufsort64 \
#     libdivsufsort \
#     libz \
#     libBionicGtestMain
# LOCAL_SHARED_LIBRARIES := \
#     liblog
# include $(BUILD_HOST_NATIVE_TEST)
