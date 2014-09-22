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
include $(CLEAR_VARS)

BOARD_RECOVERY_DEFINES := BOARD_BML_BOOT BOARD_BML_RECOVERY

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

LOCAL_SRC_FILES := applypatch.c bspatch.c freecache.c imgpatch.c utils.c
LOCAL_MODULE := libapplypatch
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += \
    external/bzip2 \
    external/zlib \
    $(commands_recovery_local_path)
LOCAL_STATIC_LIBRARIES += libmtdutils libmincrypttwrp libbz libz

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := main.c
LOCAL_MODULE := applypatch
LOCAL_C_INCLUDES += $(commands_recovery_local_path)
LOCAL_STATIC_LIBRARIES += libapplypatch libmtdutils libmincrypttwrp libbz libminelf
LOCAL_SHARED_LIBRARIES += libz libcutils libstdc++ libc

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := main.c
LOCAL_MODULE := applypatch_static
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_TAGS := eng
LOCAL_C_INCLUDES += $(commands_recovery_local_path)
LOCAL_STATIC_LIBRARIES += libapplypatch libmtdutils libmincrypttwrp libbz libminelf
LOCAL_STATIC_LIBRARIES += libz libcutils libstdc++ libc

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := imgdiff.c utils.c bsdiff.c
LOCAL_MODULE := imgdiff
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_C_INCLUDES += external/zlib external/bzip2
LOCAL_STATIC_LIBRARIES += libz libbz
LOCAL_MODULE_TAGS := eng

include $(BUILD_HOST_EXECUTABLE)
