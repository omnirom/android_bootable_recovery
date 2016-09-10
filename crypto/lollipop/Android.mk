LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libcryptfslollipop
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS :=
LOCAL_SRC_FILES = cryptfs.c
LOCAL_SHARED_LIBRARIES := libcrypto libhardware libcutils
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

ifneq ($(wildcard hardware/libhardware/include/hardware/keymaster0.h),)
    LOCAL_CFLAGS += -DTW_CRYPTO_HAVE_KEYMASTERX
    LOCAL_C_INCLUDES +=  external/boringssl/src/include
endif

LOCAL_WHOLE_STATIC_LIBRARIES += libscrypttwrp_static

include $(BUILD_SHARED_LIBRARY)



include $(CLEAR_VARS)
LOCAL_MODULE := twrpdec
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := main.c cryptfs.c
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

ifneq ($(wildcard hardware/libhardware/include/hardware/keymaster0.h),)
    LOCAL_CFLAGS += -DTW_CRYPTO_HAVE_KEYMASTERX
    LOCAL_C_INCLUDES +=  external/boringssl/src/include
endif

LOCAL_WHOLE_STATIC_LIBRARIES += libscrypttwrp_static
include $(BUILD_EXECUTABLE)

endif
