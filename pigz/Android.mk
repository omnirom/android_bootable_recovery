LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := pigz
LOCAL_MODULE_TAGS := eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULES_TAGS = optional
LOCAL_CFLAGS = 
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

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
