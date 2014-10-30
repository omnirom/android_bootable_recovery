LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := pigz
LOCAL_MODULE_TAGS := eng optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_CFLAGS = 
LOCAL_SRC_FILES = pigz.c yarn.c
LOCAL_C_INCLUDES += $(LOCAL_PATH) \
					external/zlib
LOCAL_SHARED_LIBRARIES += libz libc
LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker

include $(BUILD_EXECUTABLE)

PIGZ_TOOLS := unpigz
SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(PIGZ_TOOLS))
$(SYMLINKS): PIGZ_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE)
	@echo "Symlink: $@ -> $(PIGZ_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(PIGZ_BINARY) $@

include $(CLEAR_VARS)
LOCAL_MODULE := unpigz_symlink
LOCAL_MODULE_TAGS := optional
LOCAL_ADDITIONAL_DEPENDENCIES := $(SYMLINKS)
include $(BUILD_PHONY_PACKAGE)
SYMLINKS :=
