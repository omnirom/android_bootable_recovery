LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libcryptfsfde
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS :=
LOCAL_SRC_FILES := cryptfs.cpp
LOCAL_SHARED_LIBRARIES := libselinux libc libc++ libbase libcrypto libcutils libkeymaster_messages libhardware libprotobuf-cpp-lite libe4crypt
LOCAL_STATIC_LIBRARIES := libscrypt_static
LOCAL_C_INCLUDES := system/extras/ext4_utils system/extras/ext4_utils/include/ext4_utils external/scrypt/lib/crypto system/security/keystore hardware/libhardware/include/hardware system/security/softkeymaster/include/keymaster system/keymaster/include

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
    #8.0 or higher
    LOCAL_C_INCLUDES +=  external/boringssl/src/include
    LOCAL_SHARED_LIBRARIES += android.hardware.keymaster@3.0 libkeystore_binder libhidlbase libutils libbinder
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
        #9.0 rules
        LOCAL_CFLAGS += -Wno-unused-variable -Wno-sign-compare -Wno-unused-parameter -Wno-comment
        LOCAL_SHARED_LIBRARIES += android.hardware.keymaster@4.0 libkeymaster4support libkeyutils
        LOCAL_CFLAGS += -DTW_KEYMASTER_MAX_API=4
    else
        #8.x rules
        ifneq ($(wildcard system/core/libkeyutils/Android.bp),)
            #only present in some 8.0 trees and should be in all 8.1 trees
            LOCAL_SHARED_LIBRARIES += libkeyutils
        endif
        LOCAL_CFLAGS += -DTW_KEYMASTER_MAX_API=3
    endif
else
    # <= 7.x rules
    ifneq ($(wildcard hardware/libhardware/include/hardware/keymaster0.h),)
        LOCAL_C_INCLUDES +=  external/boringssl/src/include
        LOCAL_CFLAGS += -DTW_KEYMASTER_MAX_API=1
    else
        LOCAL_CFLAGS += -DTW_KEYMASTER_MAX_API=0
    endif
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 28; echo $$?),0)
    LOCAL_SHARED_LIBRARIES += libsoftkeymaster
endif
ifeq ($(TARGET_HW_DISK_ENCRYPTION),true)
    ifeq ($(TARGET_CRYPTFS_HW_PATH),)
        LOCAL_C_INCLUDES += device/qcom/common/cryptfs_hw
    else
        LOCAL_C_INCLUDES += $(TARGET_CRYPTFS_HW_PATH)
    endif
    LOCAL_SHARED_LIBRARIES += libcryptfs_hw
    LOCAL_CFLAGS += -DCONFIG_HW_DISK_ENCRYPTION
endif

include $(BUILD_SHARED_LIBRARY)



include $(CLEAR_VARS)
LOCAL_MODULE := twrpdec
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := main.c cryptfs.cpp Keymaster3.cpp
LOCAL_SHARED_LIBRARIES := libcrypto libhardware libcutils libc
LOCAL_C_INCLUDES := external/openssl/include $(commands_recovery_local_path)/crypto/scrypt/lib/crypto

ifeq ($(TARGET_HW_DISK_ENCRYPTION),true)
    ifeq ($(TARGET_CRYPTFS_HW_PATH),)
        LOCAL_C_INCLUDES += device/qcom/common/cryptfs_hw
    else
        LOCAL_C_INCLUDES += $(TARGET_CRYPTFS_HW_PATH)
    endif
    LOCAL_SHARED_LIBRARIES += libcryptfs_hw
    LOCAL_CFLAGS += -DCONFIG_HW_DISK_ENCRYPTION
endif

LOCAL_WHOLE_STATIC_LIBRARIES += libscrypttwrp_static
include $(BUILD_EXECUTABLE)

endif
