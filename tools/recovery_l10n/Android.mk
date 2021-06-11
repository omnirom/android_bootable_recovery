# Copyright 2012 Google Inc. All Rights Reserved.


# Prevent RecoveryLocalizer already defined errors in older trees
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_PACKAGE_NAME := RecoveryLocalizer
LOCAL_SDK_VERSION := current
LOCAL_MODULE_TAGS := optional

LOCAL_SRC_FILES := $(call all-java-files-under, src)

include $(BUILD_PACKAGE)

endif
