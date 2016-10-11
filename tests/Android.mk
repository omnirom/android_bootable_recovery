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
LOCAL_CLANG := true
LOCAL_CFLAGS := -Werror
LOCAL_MODULE := recovery_unit_test
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk
LOCAL_STATIC_LIBRARIES := \
    libverifier \
    libminui

LOCAL_SRC_FILES := unit/asn1_decoder_test.cpp
LOCAL_SRC_FILES += unit/recovery_test.cpp
LOCAL_SRC_FILES += unit/locale_test.cpp
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SHARED_LIBRARIES := liblog
include $(BUILD_NATIVE_TEST)

# Component tests
include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_CFLAGS += -Wno-unused-parameter -Werror
LOCAL_ADDITIONAL_DEPENDENCIES := $(LOCAL_PATH)/Android.mk
LOCAL_MODULE := recovery_component_test
LOCAL_C_INCLUDES := bootable/recovery
LOCAL_SRC_FILES := \
    component/applypatch_test.cpp \
    component/edify_test.cpp \
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
    libapplypatch \
    libedify \
    libotafault \
    libupdater \
    libverifier \
    libminui \
    libminzip \
    libmounts \
    liblog \
    libselinux \
    libext4_utils_static \
    libsparse_static \
    libcrypto_utils \
    libcrypto \
    libcutils \
    libbz \
    libz \
    libbase \
    libtune2fs \
    $(tune2fs_static_libraries)

testdata_out_path := $(TARGET_OUT_DATA_NATIVE_TESTS)/recovery
testdata_files := $(call find-subdir-files, testdata/*)

GEN := $(addprefix $(testdata_out_path)/, $(testdata_files))
$(GEN): PRIVATE_PATH := $(LOCAL_PATH)
$(GEN): PRIVATE_CUSTOM_TOOL = cp $< $@
$(GEN): $(testdata_out_path)/% : $(LOCAL_PATH)/%
	$(transform-generated-source)
LOCAL_GENERATED_SOURCES += $(GEN)
include $(BUILD_NATIVE_TEST)
