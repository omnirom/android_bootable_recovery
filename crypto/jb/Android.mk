LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_JB_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libcryptfsjb
LOCAL_MODULE_TAGS := eng
LOCAL_MODULES_TAGS = optional
LOCAL_CFLAGS = 
LOCAL_SRC_FILES = cryptfs.c
LOCAL_C_INCLUDES += system/extras/ext4_utils external/openssl/include bootable/recovery/crypto/scrypt/lib/crypto
LOCAL_SHARED_LIBRARIES += libc liblog libcutils libcrypto libext4_utils
LOCAL_STATIC_LIBRARIES += libfs_mgrtwrp libscrypttwrp_static liblogwraptwrp libmincrypttwrp

include $(BUILD_SHARED_LIBRARY)
endif