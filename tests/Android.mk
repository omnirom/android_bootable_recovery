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
LOCAL_CFLAGS := -Werror
LOCAL_MODULE := recovery_unit_test
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk
LOCAL_STATIC_LIBRARIES := \
    libverifier \
    libminui \
    libotautil \
    libziparchive \
    libutils \
    libz \
    libselinux \
    libbase

LOCAL_SRC_FILES := \
    unit/asn1_decoder_test.cpp \
    unit/locale_test.cpp \
    unit/sysutil_test.cpp \
    unit/zip_test.cpp

LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SHARED_LIBRARIES := liblog
include $(BUILD_NATIVE_TEST)

# Manual tests
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_CFLAGS := -Werror
LOCAL_MODULE := recovery_manual_test
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk
LOCAL_STATIC_LIBRARIES := libbase

LOCAL_SRC_FILES := manual/recovery_test.cpp
LOCAL_SHARED_LIBRARIES := liblog
include $(BUILD_NATIVE_TEST)

# Component tests
include $(CLEAR_VARS)
LOCAL_CFLAGS := -Werror
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk
LOCAL_MODULE := recovery_component_test
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SRC_FILES := \
    component/applypatch_test.cpp \
    component/edify_test.cpp \
    component/uncrypt_test.cpp \
    component/updater_test.cpp \
    component/verifier_test.cpp

LOCAL_FORCE_STATIC_EXECUTABLE := true

tune2fs_static_libraries := \
    libext2_com_err \
    libext2_blkid \
    libext2_quota \
    libext2_uuid_static \
    libext2_e2p \
    libext2fs

LOCAL_STATIC_LIBRARIES := \
    libapplypatch_modes \
    libapplypatch \
    libedify \
    libotafault \
    libupdater \
    libbootloader_message \
    libverifier \
    libminui \
    libotautil \
    libmounts \
    libfs_mgr \
    liblog \
    libselinux \
    libext4_utils_static \
    libsparse_static \
    libcrypto_utils \
    libcrypto \
    libcutils \
    libbz \
    libziparchive \
    libutils \
    libz \
    libbase \
    libtune2fs \
    $(tune2fs_static_libraries)

testdata_files := $(call find-subdir-files, testdata/*)

# The testdata files that will go to $OUT/data/nativetest/recovery.
testdata_out_path := $(TARGET_OUT_DATA)/nativetest/recovery
GEN := $(addprefix $(testdata_out_path)/, $(testdata_files))
$(GEN): PRIVATE_PATH := $(LOCAL_PATH)
$(GEN): PRIVATE_CUSTOM_TOOL = cp $< $@
$(GEN): $(testdata_out_path)/% : $(LOCAL_PATH)/%
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)

# A copy of the testdata to be packed into continuous_native_tests.zip.
testdata_continuous_zip_prefix := \
    $(call intermediates-dir-for,PACKAGING,recovery_component_test)/DATA
testdata_continuous_zip_path := $(testdata_continuous_zip_prefix)/nativetest/recovery
GEN := $(addprefix $(testdata_continuous_zip_path)/, $(testdata_files))
$(GEN): PRIVATE_PATH := $(LOCAL_PATH)
$(GEN): PRIVATE_CUSTOM_TOOL = cp $< $@
$(GEN): $(testdata_continuous_zip_path)/% : $(LOCAL_PATH)/%
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)
LOCAL_PICKUP_FILES := $(testdata_continuous_zip_prefix)

include $(BUILD_NATIVE_TEST)
