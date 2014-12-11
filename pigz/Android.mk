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

include $(BUILD_EXECUTABLE)

PIGZ_TOOLS := unpigz
SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(PIGZ_TOOLS))
$(SYMLINKS): PIGZ_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "Symlink: $@ -> $(PIGZ_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(PIGZ_BINARY) $@

ifneq (,$(filter $(PLATFORM_SDK_VERSION),16 17 18))
ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
	$(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := unpigz_symlink
LOCAL_MODULE_TAGS := optional
LOCAL_ADDITIONAL_DEPENDENCIES := $(SYMLINKS)
include $(BUILD_PHONY_PACKAGE)
SYMLINKS :=
