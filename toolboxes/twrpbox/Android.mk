LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

TWRPBOX_TOOLS := \
    getprop \
    lsof \
    setprop \
    watchprops

RECOVERY_TWRPBOX_TOOLS := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS) $(RECOVERY_TOOLBOX_TOOLS), $(TWRPBOX_TOOLS))

ifeq ($(RECOVERY_TWRPBOX_TOOLS),)

# busybox or toybox provides all of these tools,
# so there is no need to build toolbox
$(twrpbox):

else

LOCAL_SRC_FILES := \
    dynarray.c \
    toolbox.c \
    $(patsubst %,%.c,$(RECOVERY_TWRPBOX_TOOLS))

LOCAL_CFLAGS += \
    -std=gnu99

LOCAL_SHARED_LIBRARIES += \
    libcutils

LOCAL_C_INCLUDES += \
    system/core/include

LOCAL_CLANG := false

LOCAL_MODULE := twrpbox
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

LOCAL_POST_INSTALL_CMD := \
    $(hide) $(foreach t,$(RECOVERY_TWRPBOX_TOOLS),ln -sf $(LOCAL_MODULE) $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(RECOVERY_TWRPBOX_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(RECOVERY_TWRPBOX_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

endif
