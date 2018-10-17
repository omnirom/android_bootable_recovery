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

ifneq ($(wildcard hardware/libhardware/include/hardware/keymaster0.h),)
    LOCAL_CFLAGS += -DTW_CRYPTO_HAVE_KEYMASTERX
    LOCAL_C_INCLUDES +=  external/boringssl/src/include
endif
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 26; echo $$?),0)
    #8.0 or higher
    LOCAL_SHARED_LIBRARIES += android.hardware.keymaster@3.0 libkeystore_binder libhidlbase libutils libbinder
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
        #9.0 rules
        LOCAL_CFLAGS += -DUSE_KEYMASTER_4 -Wno-unused-variable -Wno-sign-compare -Wno-unused-parameter -Wno-comment
        #LOCAL_SRC_FILES += Keymaster4.cpp
        LOCAL_SHARED_LIBRARIES += android.hardware.keymaster@4.0 libkeymaster4support
        LOCAL_CFLAGS += -DHAVE_LIBKEYUTILS
        LOCAL_SHARED_LIBRARIES += libkeyutils
    else
        #8.0 rules
        LOCAL_CFLAGS += -DUSE_KEYMASTER_3
        #LOCAL_SRC_FILES += Keymaster3.cpp
        ifneq ($(wildcard system/core/libkeyutils/Android.bp),)
            #only present in some 8.0 trees and should be in all 8.1 trees
            LOCAL_CFLAGS += -DHAVE_LIBKEYUTILS
            LOCAL_SHARED_LIBRARIES += libkeyutils
        endif
    endif
else
    #7.x rules
    #LOCAL_SRC_FILES += Keymaster.cpp KeyStorage.cpp
    LOCAL_CFLAGS += -DUSE_KEYMASTER_1
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
