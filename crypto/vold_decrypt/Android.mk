# Copyright (C) 2017 TeamWin Recovery Project
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
    ifneq ($(TW_CRYPTO_USE_SYSTEM_VOLD),)

        # Parse TW_CRYPTO_USE_SYSTEM_VOLD
        ifeq ($(TW_CRYPTO_USE_SYSTEM_VOLD),true)
            # Just enabled, so only vold + vdc
            services :=
        else
            # Additional services needed by vold
            services := $(TW_CRYPTO_USE_SYSTEM_VOLD)
        endif

        # Parse TW_CRYPTO_SYSTEM_VOLD_MOUNT
        ifneq ($(TW_CRYPTO_SYSTEM_VOLD_MOUNT),)
            # Per device additional partitions to mount
            partitions := $(TW_CRYPTO_SYSTEM_VOLD_MOUNT)
        endif

        # List of .rc files for each additional service
        rc_files := $(foreach item,$(services),init.recovery.vold_decrypt.$(item).rc)


        include $(CLEAR_VARS)
        LOCAL_MODULE := init.recovery.vold_decrypt.rc
        LOCAL_MODULE_TAGS := eng
        LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES

        # Cannot send to TARGET_RECOVERY_ROOT_OUT since build system wipes init*.rc
        # during ramdisk creation and only allows init.recovery.*.rc files to be copied
        # from TARGET_ROOT_OUT thereafter
        LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)

        LOCAL_SRC_FILES := $(LOCAL_MODULE)

        # Add additional .rc files and imports into init.recovery.vold_decrypt.rc
        # Note: any init.recovery.vold_decrypt.{service}.rc that are not default
        #       in crypto/vold_decrypt should be in the device tree
        LOCAL_POST_INSTALL_CMD := $(hide) \
            $(foreach item, $(rc_files), \
                sed -i '1iimport \/$(item)' "$(TARGET_ROOT_OUT)/$(LOCAL_MODULE)"; \
                if [ -f "$(LOCAL_PATH)/$(item)" ]; then \
                    cp -f "$(LOCAL_PATH)/$(item)" "$(TARGET_ROOT_OUT)"/; \
                fi; \
            )

        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 26; echo $$?),0)
            # Truncate service_name to max 16 characters
            LOCAL_POST_INSTALL_CMD += \
                $(foreach item, $(rc_files), \
                    if [ -f "$(TARGET_ROOT_OUT)/$(item)" ]; then \
                        sed -i 's/\([ \t]*service[ \t]*\)\(.\{16\}\).*\([ \t].*\)/\1\2\3/' "$(TARGET_ROOT_OUT)/$(item)"; \
                    fi; \
                )
        endif

        include $(BUILD_PREBUILT)


        include $(CLEAR_VARS)
        LOCAL_MODULE := libvolddecrypt
        LOCAL_MODULE_TAGS := eng optional
        LOCAL_CFLAGS := -Wall
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
            LOCAL_C_INCLUDES += external/stlport/stlport bionic bionic/libstdc++/include
        endif

        ifneq ($(services),)
            ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 26; echo $$?),0)
                # Truncate service_name to max 12 characters due to the 4 character prefix
                truncated_services := $(foreach item,$(services),$(shell echo -n "$(item)" | sed 's/\(.\{12\}\).*/\1/'))
                LOCAL_CFLAGS += -DTW_CRYPTO_SYSTEM_VOLD_SERVICES='"$(truncated_services)"'
                LOCAL_CFLAGS += -D_USING_SHORT_SERVICE_NAMES
            else
                LOCAL_CFLAGS += -DTW_CRYPTO_SYSTEM_VOLD_SERVICES='"$(services)"'
            endif
        endif

        ifneq ($(partitions),)
            LOCAL_CFLAGS += -DTW_CRYPTO_SYSTEM_VOLD_MOUNT='"$(partitions)"'
        endif

        ifeq ($(TW_CRYPTO_SYSTEM_VOLD_DEBUG),true)
            # Enabling strace will expose the password in the strace logs!!
            LOCAL_CFLAGS += -DTW_CRYPTO_SYSTEM_VOLD_DEBUG
        else
            ifneq ($(TW_CRYPTO_SYSTEM_VOLD_DEBUG),)
                # Specify strace path
                LOCAL_CFLAGS += -DTW_CRYPTO_SYSTEM_VOLD_DEBUG
                LOCAL_CFLAGS += -DVD_STRACE_BIN=\"$(TW_CRYPTO_SYSTEM_VOLD_DEBUG)\"
            endif
        endif

        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
            ifeq ($(TW_INCLUDE_LIBRESETPROP), true)
                LOCAL_CFLAGS += -DTW_INCLUDE_LIBRESETPROP
            endif
        endif

        LOCAL_SRC_FILES = vold_decrypt.cpp
        LOCAL_SHARED_LIBRARIES := libcutils
        LOCAL_C_INCLUDES += system/extras/ext4_utils/include
        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 29; echo $$?),0)
            LOCAL_C_INCLUDES += bootable/recovery/crypto/fscrypt
        else
            LOCAL_C_INCLUDES += bootable/recovery/crypto/ext4crypt
        endif
        include $(BUILD_STATIC_LIBRARY)

        ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
            include $(CLEAR_VARS)
            LOCAL_MODULE := vdc_pie
            LOCAL_SRC_FILES := vdc_pie.cpp
            LOCAL_MODULE_TAGS := eng
            LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
            LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
            LOCAL_CLANG := true
            LOCAL_TIDY := true
            LOCAL_TIDY_FLAGS := -warnings-as-errors=clang-analyzer-security*,cert-*
            LOCAL_TIDY_CHECKS := -*,cert-*,clang,-analyzer-security*
            LOCAL_STATIC_LIBRARIES := libvold_binder
            LOCAL_SHARED_LIBRARIES := libbase libcutils libutils libbinder
            LOCAL_CFLAGS := -Wall
            ifeq ($(TWRP_INCLUDE_LOGCAT), true)
                LOCAL_CFLAGS += -DTWRP_INCLUDE_LOGCAT
            endif
            ifneq ($(TARGET_ARCH), arm64)
                ifneq ($(TARGET_ARCH), x86_64)
                    LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker
                else
                    LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
                endif
            else
                LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
            endif

            include $(BUILD_EXECUTABLE)
        endif

    endif
endif
