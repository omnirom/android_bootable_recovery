LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := pigz
LOCAL_MODULE_TAGS := eng optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS :=
LOCAL_SRC_FILES = pigz.c yarn.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					external/zlib
LOCAL_SHARED_LIBRARIES += libz libc

LOCAL_ADDITIONAL_DEPENDENCIES := pigz_symlinks

include $(BUILD_EXECUTABLE)

# Symlinks for pigz utilities
include $(CLEAR_VARS)

PIGZ_TOOLS := gunzip gzip unpigz

pigz_symlinks:
	@mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sbin
	@echo "Generate pigz symlinks:" $(PIGZ_TOOLS)
	$(hide) $(foreach t,$(PIGZ_TOOLS),ln -sf pigz $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

ifneq (,$(filter $(PLATFORM_SDK_VERSION),16 17 18))
ALL_DEFAULT_INSTALLED_MODULES += pigz_symlinks

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
	$(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) pigz_symlinks
endif
