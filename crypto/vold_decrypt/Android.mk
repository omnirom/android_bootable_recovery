# Copyright (C) 2015 TeamWin Recovery Project
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

ifeq ($(TW_INCLUDE_CRYPTO), true)
    ifeq ($(TW_CRYPTO_USE_SYSTEM_VOLD), true)

        include $(CLEAR_VARS)
        LOCAL_MODULE := init.recovery.vold_decrypt.rc
        LOCAL_MODULE_TAGS := eng
        LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES

        # Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
        # during ramdisk creation and only allows init.recovery.*.rc files to be copied
        # from TARGET_ROOT_OUT thereafter
        LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

        LOCAL_SRC_FILES := $(LOCAL_MODULE)
        include $(BUILD_PREBUILT)


        include $(CLEAR_VARS)
        LOCAL_MODULE := init.vold_decrypt.sh
        LOCAL_MODULE_TAGS := eng
        LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
        LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)
        LOCAL_SRC_FILES := $(LOCAL_MODULE)
        include $(BUILD_PREBUILT)
    endif
endif
