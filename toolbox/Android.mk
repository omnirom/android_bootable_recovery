LOCAL_PATH:= system/core/toolbox/
include $(CLEAR_VARS)

TOOLS := \
	start \
	stop \
	getprop \
	setprop

# If busybox does not have SELinux support, provide these tools with toolbox.
# Note that RECOVERY_BUSYBOX_TOOLS will be empty if TW_USE_TOOLBOX == true.
ifeq ($(TWHAVE_SELINUX), true)
	TOOLS_FOR_SELINUX := \
		ls \
		getenforce \
		setenforce \
		chcon \
		restorecon \
		runcon \
		getsebool \
		setsebool \
		load_policy
	TOOLS += $(filter-out $(RECOVERY_BUSYBOX_TOOLS), $(TOOLS_FOR_SELINUX))
endif

ifeq ($(TW_USE_TOOLBOX), true)
	TOOLS += \
		mount \
		cat \
		ps \
		kill \
		ln \
		insmod \
		rmmod \
		lsmod \
		ifconfig \
		setconsole \
		rm \
		mkdir \
		rmdir \
		getevent \
		sendevent \
		date \
		wipe \
		sync \
		umount \
		notify \
		cmp \
		dmesg \
		route \
		hd \
		dd \
		df \
		watchprops \
		log \
		sleep \
		renice \
		printenv \
		smd \
		chmod \
		chown \
		newfs_msdos \
		netstat \
		ioctl \
		mv \
		schedtop \
		top \
		iftop \
		id \
		uptime \
		vmstat \
		nandread \
		ionice \
		touch \
		lsof \
		du \
		md5 \
		clear \
		swapon \
		swapoff \
		mkswap \
		readlink
	ifneq ($(TWHAVE_SELINUX), true)
		TOOLS += ls
	endif
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

include $(CLEAR_VARS)
LOCAL_MODULE := toolbox_symlinks
LOCAL_MODULE_TAGS := optional
LOCAL_ADDITIONAL_DEPENDENCIES := $(SYMLINKS)
include $(BUILD_PHONY_PACKAGE)
SYMLINKS :=
