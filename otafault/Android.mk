# Copyright 2015 The ANdroid Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# 	   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific languae governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

empty :=
space := $(empty) $(empty)
comma := ,

ifneq ($(TARGET_INJECT_FAULTS),)
TARGET_INJECT_FAULTS := $(subst $(comma),$(space),$(strip $(TARGET_INJECT_FAULTS)))
endif

include $(CLEAR_VARS)

LOCAL_SRC_FILES := ota_io.cpp
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE := libotafault
LOCAL_CLANG := true

ifneq ($(TARGET_INJECT_FAULTS),)
$(foreach ft,$(TARGET_INJECT_FAULTS),\
	$(eval LOCAL_CFLAGS += -DTARGET_$(ft)_FAULT=$(TARGET_$(ft)_FAULT_FILE)))
LOCAL_CFLAGS += -Wno-unused-parameter
LOCAL_CFLAGS += -DTARGET_INJECT_FAULTS
endif

LOCAL_STATIC_LIBRARIES := libc

include $(BUILD_STATIC_LIBRARY)

# otafault_test (static executable)
# ===============================
include $(CLEAR_VARS)

LOCAL_SRC_FILES := ota_io.cpp test.cpp
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE := otafault_test
LOCAL_STATIC_LIBRARIES := libc
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_CFLAGS += -Wno-unused-parameter -Wno-writable-strings

ifneq ($(TARGET_INJECT_FAULTS),)
$(foreach ft,$(TARGET_INJECT_FAULTS),\
	$(eval LOCAL_CFLAGS += -DTARGET_$(ft)_FAULT=$(TARGET_$(ft)_FAULT_FILE)))
LOCAL_CFLAGS += -DTARGET_INJECT_FAULTS
endif

include $(BUILD_EXECUTABLE)
