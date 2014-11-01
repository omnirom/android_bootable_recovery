LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libcryptfsics
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS = 
LOCAL_CFLAGS += -DCRYPTO_FS_TYPE=\"$(TW_CRYPTO_FS_TYPE)\"
ifeq ($(TW_INCLUDE_CRYPTO_SAMSUNG), true)
    LOCAL_CFLAGS += -DTW_INCLUDE_CRYPTO_SAMSUNG=\"$(TW_INCLUDE_CRYPTO_SAMSUNG)\"
    LOCAL_LDFLAGS += -ldl
    LOCAL_STATIC_LIBRARIES += libcrypt_samsung
endif
ifneq ($(TW_INTERNAL_STORAGE_PATH),)
	LOCAL_CFLAGS += -DTW_INTERNAL_STORAGE_PATH=$(TW_INTERNAL_STORAGE_PATH)
endif
ifneq ($(TW_EXTERNAL_STORAGE_PATH),)
	LOCAL_CFLAGS += -DTW_EXTERNAL_STORAGE_PATH=$(TW_EXTERNAL_STORAGE_PATH)
endif
LOCAL_SRC_FILES = cryptfs.c
LOCAL_C_INCLUDES += system/extras/ext4_utils external/openssl/include
LOCAL_SHARED_LIBRARIES += libc liblog libcutils libcrypto
LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker

include $(BUILD_SHARED_LIBRARY)
endif
