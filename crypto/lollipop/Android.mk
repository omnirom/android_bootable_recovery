LOCAL_PATH := $(call my-dir)
ifeq ($(TW_INCLUDE_CRYPTO), true)
include $(CLEAR_VARS)

LOCAL_MODULE := libcryptfslollipop
LOCAL_MODULE_TAGS := eng optional
LOCAL_CFLAGS :=
LOCAL_SRC_FILES = cryptfs.c
LOCAL_SHARED_LIBRARIES := libcrypto libhardware libcutils
LOCAL_C_INCLUDES := external/openssl/include $(commands_recovery_local_path)/crypto/scrypt/lib/crypto
LOCAL_WHOLE_STATIC_LIBRARIES += libscrypttwrp_static

include $(BUILD_SHARED_LIBRARY)

endif
