# Copyright 2008 The Android Open Source Project
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := libmincrypttwrp
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(commands_recovery_local_path)/libmincrypt/includes
LOCAL_SRC_FILES := dsa_sig.c p256.c p256_ec.c p256_ecdsa.c rsa.c sha.c sha256.c
LOCAL_CFLAGS := -Wall -Werror
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libmincrypttwrp
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(commands_recovery_local_path)/libmincrypt/includes
LOCAL_SRC_FILES := dsa_sig.c p256.c p256_ec.c p256_ecdsa.c rsa.c sha.c sha256.c
LOCAL_CFLAGS := -Wall -Werror
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := libmincrypttwrp
LOCAL_MODULE_TAGS := optional
LOCAL_C_INCLUDES := $(commands_recovery_local_path)/libmincrypt/includes
LOCAL_SRC_FILES := dsa_sig.c p256.c p256_ec.c p256_ecdsa.c rsa.c sha.c sha256.c
LOCAL_CFLAGS := -Wall -Werror
include $(BUILD_HOST_STATIC_LIBRARY)
