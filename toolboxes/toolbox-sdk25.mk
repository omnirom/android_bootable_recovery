LOCAL_PATH := system/core/toolbox

include $(CLEAR_VARS)

ifeq ($(TW_USE_TOOLBOX), true)
    BSD_TOOLS := \
        dd

    OUR_TOOLS := \
        getevent \
        iftop \
        ioctl \
        log \
        nandread \
        newfs_msdos \
        prlimit \
        ps \
        restart \
        sendevent \
        start \
        stop \
        top
endif

BSD_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(BSD_TOOLS))
OUR_TOOLS_USED := $(filter-out $(RECOVERY_BUSYBOX_TOOLS) $(RECOVERY_TOYBOX_TOOLS), $(OUR_TOOLS))
RECOVERY_TOOLBOX_TOOLS := $(BSD_TOOLS_USED) $(OUR_TOOLS_USED)

ifeq ($(RECOVERY_TOOLBOX_TOOLS),)

# busybox or toybox provides all of these tools,
# so there is no need to build toolbox
$(toolbox_recovery):

else

LOCAL_SRC_FILES := \
    start_stop.cpp \
    toolbox.c \
    $(patsubst %,%.c,$(OUR_TOOLS_USED))

LOCAL_CONLYFLAGS += \
    -std=gnu99

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libselinux

ifneq ($(BSD_TOOLS_USED),)
    LOCAL_WHOLE_STATIC_LIBRARIES := \
        libtoolbox_dd
endif

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

LOCAL_ADDITIONAL_DEPENDENCIES := r_recovery

LOCAL_POST_INSTALL_CMD := \
    $(hide) $(foreach t,$(RECOVERY_TOOLBOX_TOOLS),ln -sf $(LOCAL_MODULE_STEM) $(TARGET_RECOVERY_ROOT_OUT)/sbin/$(t);)

# Including this will define $(intermediates) below
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(RECOVERY_TOOLBOX_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

$(LOCAL_PATH)/getevent.c: $(intermediates)/input.h-labels.h

UAPI_INPUT_EVENT_CODES_H := bionic/libc/kernel/uapi/linux/input-event-codes.h
INPUT_H_LABELS_H := $(intermediates)/input.h-labels.h
$(INPUT_H_LABELS_H): PRIVATE_LOCAL_PATH := $(LOCAL_PATH)
# The PRIVATE_CUSTOM_TOOL line uses = to evaluate the output path late.
# We copy the input path so it can't be accidentally modified later.
$(INPUT_H_LABELS_H): PRIVATE_UAPI_INPUT_EVENT_CODES_H := $(UAPI_INPUT_EVENT_CODES_H)
$(INPUT_H_LABELS_H): PRIVATE_CUSTOM_TOOL = $(PRIVATE_LOCAL_PATH)/generate-input.h-labels.py $(PRIVATE_UAPI_INPUT_EVENT_CODES_H) > $@
# The dependency line though gets evaluated now, so the PRIVATE_ copy doesn't exist yet,
# and the original can't yet have been modified, so this is both sufficient and necessary.
$(INPUT_H_LABELS_H): $(LOCAL_PATH)/Android.mk $(LOCAL_PATH)/generate-input.h-labels.py $(UAPI_INPUT_EVENT_CODES_H)
$(INPUT_H_LABELS_H):
	$(transform-generated-source)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := r.c
LOCAL_MODULE := r_recovery
LOCAL_MODULE_STEM := r
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

include $(BUILD_EXECUTABLE)

endif

BSD_TOOLS :=
OUR_TOOLS :=
BSD_TOOLS_USED :=
OUR_TOOLS_USED :=
RECOVERY_TOOLBOX_TOOLS :=
