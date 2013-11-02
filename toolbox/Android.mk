LOCAL_PATH:= system/core/toolbox/
include $(CLEAR_VARS)

TOOLS := \
	start \
	stop \
	getprop \
	setprop

ifeq ($(TWHAVE_SELINUX), true)
	TOOLS += \
		ls \
		getenforce \
		setenforce \
		chcon \
		restorecon \
		runcon \
		getsebool \
		setsebool \
		load_policy
endif

LOCAL_SRC_FILES := \
	toolbox.c \
	$(patsubst %,%.c,$(TOOLS))

ifneq ($(wildcard system/core/toolbox/dynarray.c),)
    LOCAL_SRC_FILES += dynarray.c
endif

# reboot.c was removed in 4.4 kitkat
#TOOLS += reboot

#ifeq ($(BOARD_USES_BOOTMENU),true)
#	LOCAL_SRC_FILES += ../../../external/bootmenu/libreboot/reboot.c
#else
#	LOCAL_SRC_FILES += reboot.c
#endif

LOCAL_C_INCLUDES := bionic/libc/bionic

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	liblog \
	libc

ifeq ($(TWHAVE_SELINUX), true)
	LOCAL_SHARED_LIBRARIES += libselinux
endif

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

# Including this will define $(intermediates).
#
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

# Make #!/system/bin/toolbox launchers for each tool.
#
SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(TOOLS))
$(SYMLINKS): TOOLBOX_BINARY := $(LOCAL_MODULE_STEM)
$(SYMLINKS): $(LOCAL_INSTALLED_MODULE) $(LOCAL_PATH)/Android.mk
	@echo "Symlink: $@ -> $(TOOLBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(TOOLBOX_BINARY) $@

ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
