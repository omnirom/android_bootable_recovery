#
# Copyright (C) 2017 The Android Open Source Project
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

LOCAL_PATH := $(my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := bootctrl.bcb
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := boot_control.cpp
LOCAL_CFLAGS := \
  -D_FILE_OFFSET_BITS=64 \
  -Werror \
  -Wall \
  -Wextra
LOCAL_SHARED_LIBRARIES := liblog
LOCAL_STATIC_LIBRARIES := libbootloader_message libfs_mgr libbase
LOCAL_POST_INSTALL_CMD := \
  $(hide) mkdir -p $(TARGET_OUT_SHARED_LIBRARIES)/hw && \
  ln -sf bootctrl.bcb.so $(TARGET_OUT_SHARED_LIBRARIES)/hw/bootctrl.default.so
include $(BUILD_SHARED_LIBRARY)
