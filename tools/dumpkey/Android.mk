# Copyright (C) 2008 The Android Open Source Project
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

# Determine whether to build dumpkey from system/core/libmincrypt or from
# bootable/recovery/tools. The dumpkey source is temporarily present in both
# locations during the process of moving the tool to the recovery repository.
# TODO(mnissler): Remove the guard after the transition is complete.
ifndef BUILD_DUMPKEY_FROM_RECOVERY
BUILD_DUMPKEY_FROM_RECOVERY := true
endif

ifeq ($(BUILD_DUMPKEY_FROM_RECOVERY),true)
include $(CLEAR_VARS)
LOCAL_MODULE := dumpkey
LOCAL_SRC_FILES := DumpPublicKey.java
LOCAL_JAR_MANIFEST := DumpPublicKey.mf
LOCAL_STATIC_JAVA_LIBRARIES := bouncycastle-host
include $(BUILD_HOST_JAVA_LIBRARY)
endif
