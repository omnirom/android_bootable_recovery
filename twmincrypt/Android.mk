# This provides a rule to make libmincrypt as a shared library.
# As a static library, zip signature verification was crashing.
# Making it as a shared library seems to fix that issue.

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS:= eng
LOCAL_MODULE := libmincrypt
LOCAL_SRC_FILES := ../../../system/core/libmincrypt/rsa.c ../../../system/core/libmincrypt/sha.c

ifneq ($(wildcard system/core/libmincrypt/rsa_e_3.c),)
LOCAL_SRC_FILES += ../../../system/core/libmincrypt/rsa_e_3.c ../../../system/core/libmincrypt/rsa_e_f4.c
endif

include $(BUILD_SHARED_LIBRARY)
