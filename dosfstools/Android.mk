ifneq ($(TARGET_SIMULATOR),true)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := src/boot.c src/check.c src/common.c \
	src/fat.c src/file.c src/io.c src/lfn.c src/dosfsck.c
LOCAL_C_INCLUDES := $(KERNEL_HEADERS)
LOCAL_SHARED_LIBRARIES := libc
LOCAL_CFLAGS += -D_USING_BIONIC_
LOCAL_CFLAGS += -DUSE_ANDROID_RETVALS
LOCAL_MODULE = dosfsck
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
include $(BUILD_EXECUTABLE)

# build symlink
SYMLINKS := $(addprefix $(TARGET_OUT)/bin/,fsck_msdos)
$(SYMLINKS): DOSFSCK_BINARY := $(LOCAL_MODULE)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE) $(LOCAL_PATH)/Android.mk
	@echo "Symlink: $@ -> $(DOSFSCK_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(DOSFSCK_BINARY) $@

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)



include $(CLEAR_VARS)
LOCAL_SRC_FILES := src/boot.c src/check.c src/common.c src/fat.c \
	src/file.c src/io.c src/lfn.c src/dosfslabel.c
LOCAL_C_INCLUDES := $(KERNEL_HEADERS) \
	bionic/libc/kernel/common
LOCAL_SHARED_LIBRARIES := libc
LOCAL_CFLAGS += -D_USING_BIONIC_
LOCAL_MODULE = dosfslabel
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES := src/mkdosfs.c
LOCAL_C_INCLUDES := $(KERNEL_HEADERS)
LOCAL_SHARED_LIBRARIES := libc
LOCAL_CFLAGS += -D_USING_BIONIC_
LOCAL_MODULE = mkdosfs
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
include $(BUILD_EXECUTABLE)

endif
