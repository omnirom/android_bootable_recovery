LOCAL_PATH:= system/core/toolbox/


ifeq ($(PLATFORM_VERSION), 5.0)

# Rule for lollipop
common_cflags := \
    -std=gnu99 \
    -Werror -Wno-unused-parameter \
    -I$(LOCAL_PATH)/upstream-netbsd/include/ \
    -include bsd-compatibility.h

include $(CLEAR_VARS)

OUR_TOOLS := \
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
	OUR_TOOLS += $(filter-out $(RECOVERY_BUSYBOX_TOOLS), $(TOOLS_FOR_SELINUX))
endif


ifeq ($(TW_USE_TOOLBOX), true)
BSD_TOOLS := \
    cat \
    chown \
    cp \
    dd \
    du \
    grep \
    kill \
    ln \
    mv \
    printenv \
    rm \
    rmdir \
    sleep \
    sync \

OUR_TOOLS += \
    chmod \
    clear \
    cmp \
    date \
    df \
    dmesg \
    getevent \
    hd \
    id \
    ifconfig \
    iftop \
    insmod \
    ioctl \
    ionice \
    log \
    lsmod \
    lsof \
    md5 \
    mkdir \
    mknod \
    mkswap \
    mount \
    nandread \
    netstat \
    newfs_msdos \
    nohup \
    notify \
    ps \
    readlink \
    renice \
    rmmod \
    route \
    schedtop \
    sendevent \
    smd \
    swapoff \
    swapon \
    top \
    touch \
    umount \
    uptime \
    vmstat \
    watchprops \
    wipe
    ifneq ($(TWHAVE_SELINUX), true)
        TOOLS += ls
    endif
    LOCAL_SHARED_LIBRARIES += libcrypto
endif

ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
OUR_TOOLS += r
endif

ALL_TOOLS = $(BSD_TOOLS) $(OUR_TOOLS)

LOCAL_SRC_FILES := \
    upstream-netbsd/lib/libc/gen/getbsize.c \
    upstream-netbsd/lib/libc/gen/humanize_number.c \
    upstream-netbsd/lib/libc/stdlib/strsuftoll.c \
    upstream-netbsd/lib/libc/string/swab.c \
    upstream-netbsd/lib/libutil/raise_default_signal.c \
    dynarray.c \
    pwcache.c \
    $(patsubst %,%.c,$(OUR_TOOLS)) \
    toolbox.c \

LOCAL_CFLAGS += $(common_cflags)

LOCAL_C_INCLUDES += external/openssl/include

LOCAL_SHARED_LIBRARIES += libcutils

ifeq ($(TWHAVE_SELINUX), true)
    LOCAL_SHARED_LIBRARIES += libselinux
    LOCAL_STATIC_LIBRARIES += \
        libtoolbox_cat \
        libtoolbox_chown \
        libtoolbox_cp \
        libtoolbox_dd \
        libtoolbox_grep \
        libtoolbox_kill \
        libtoolbox_ln \
        libtoolbox_mv \
        libtoolbox_printenv \
        libtoolbox_rm \
        libtoolbox_rmdir \
        libtoolbox_sleep \
        libtoolbox_sync
endif

# libusbhost is only used by lsusb, and that isn't usually included in toolbox.
# The linker strips out all the unused library code in the normal case.
LOCAL_STATIC_LIBRARIES := \
    libusbhost \

LOCAL_WHOLE_STATIC_LIBRARIES := $(patsubst %,libtoolbox_%,$(BSD_TOOLS))

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_ADDITIONAL_DEPENDENCIES += $(LOCAL_PATH)/Android.mk

# Including this will define $(intermediates).
#
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(ALL_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

# Make #!/system/bin/toolbox launchers for each tool.
#
SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(ALL_TOOLS))
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

# We need this so that the installed files could be picked up based on the
# local module name
ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
    $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)

else

# Rule for older trees
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

endif
