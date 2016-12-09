LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libe4crypt
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS :=
LOCAL_SRC_FILES := Decrypt.cpp Ext4Crypt.cpp Keymaster.cpp KeyStorage.cpp ScryptParameters.cpp Utils.cpp HashPassword.cpp ext4_crypt.cpp
LOCAL_SHARED_LIBRARIES := libselinux libc libc++ libext4_utils libsoftkeymaster libbase libcrypto libcutils libkeymaster_messages libhardware libprotobuf-cpp-lite
LOCAL_STATIC_LIBRARIES := libscrypt_static
LOCAL_C_INCLUDES := system/extras/ext4_utils external/scrypt/lib/crypto system/security/keystore hardware/libhardware/include/hardware system/security/softkeymaster/include/keymaster system/keymaster/include

ifneq ($(wildcard hardware/libhardware/include/hardware/keymaster0.h),)
    LOCAL_CFLAGS += -DTW_CRYPTO_HAVE_KEYMASTERX
    LOCAL_C_INCLUDES +=  external/boringssl/src/include
endif

include $(BUILD_SHARED_LIBRARY)



include $(CLEAR_VARS)
LOCAL_MODULE := twrpfbe
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := main.cpp
LOCAL_SHARED_LIBRARIES := libe4crypt
#LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64

include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := e4policyget
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := e4policyget.cpp
LOCAL_SHARED_LIBRARIES := libe4crypt
LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64

include $(BUILD_EXECUTABLE)

endif
