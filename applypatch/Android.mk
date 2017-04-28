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

ifdef project-path-for
    RECOVERY_PATH := $(call project-path-for,recovery)
else
    RECOVERY_PATH := bootable/recovery
endif

include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_C_INCLUDES += \
    external/bzip2 \
    external/zlib \
    $(commands_recovery_local_path)

LOCAL_CLANG := true
LOCAL_SRC_FILES := applypatch.cpp bspatch.cpp freecache.cpp imgpatch.cpp utils.cpp
LOCAL_MODULE := libapplypatch
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += $(RECOVERY_PATH)
LOCAL_STATIC_LIBRARIES += libbase libotafault libmtdutils libcrypto_static libbz libz

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

ifeq ($(HOST_OS),linux)
include $(CLEAR_VARS)

LOCAL_CLANG := true
LOCAL_SRC_FILES := bspatch.cpp imgpatch.cpp utils.cpp
LOCAL_MODULE := libimgpatch
LOCAL_C_INCLUDES += $(RECOVERY_PATH)
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES += libcrypto_static libbz libz

include $(BUILD_HOST_STATIC_LIBRARY)
endif  # HOST_OS == linux

include $(CLEAR_VARS)

LOCAL_CLANG := true
LOCAL_SRC_FILES := main.cpp
LOCAL_MODULE := applypatch
LOCAL_C_INCLUDES += $(RECOVERY_PATH)
LOCAL_STATIC_LIBRARIES += libapplypatch libbase libotafault libmtdutils libcrypto_static libbz \
                          libedify \

LOCAL_SHARED_LIBRARIES += libz libcutils libc

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_CLANG := true
LOCAL_SRC_FILES := imgdiff.cpp utils.cpp bsdiff.cpp
LOCAL_MODULE := imgdiff
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_C_INCLUDES += external/zlib external/bzip2
LOCAL_STATIC_LIBRARIES += libz libbz

include $(BUILD_HOST_EXECUTABLE)
