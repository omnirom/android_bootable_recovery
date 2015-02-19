LOCAL_PATH := system/core/toolbox
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
    ifeq ($(PLATFORM_SDK_VERSION), 21)
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
            sync
    else
        OUR_TOOLS += \
            cat \
            chown \
            dd \
            du \
            kill \
            ln \
            mv \
            printenv \
            rm \
            rmdir \
            setconsole \
            sleep \
            sync
    endif

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
            OUR_TOOLS += ls
        endif
        LOCAL_SHARED_LIBRARIES += libcrypto
    endif

ifeq ($(PLATFORM_SDK_VERSION), 21)
    ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
        OUR_TOOLS += r
    endif
endif

LOCAL_SRC_FILES := \
    toolbox.c \
    $(patsubst %,%.c,$(OUR_TOOLS))

ifeq ($(PLATFORM_SDK_VERSION), 21)
    LOCAL_SRC_FILES += \
        upstream-netbsd/lib/libc/gen/getbsize.c \
        upstream-netbsd/lib/libc/gen/humanize_number.c \
        upstream-netbsd/lib/libc/stdlib/strsuftoll.c \
        upstream-netbsd/lib/libc/string/swab.c \
        upstream-netbsd/lib/libutil/raise_default_signal.c \
        dynarray.c \
        pwcache.c
endif

ifeq ($(PLATFORM_SDK_VERSION), 21)
    ifeq (,$(filter $(LOCAL_SRC_FILES),setenforce.c))
        LOCAL_SRC_FILES += setenforce.c
    endif
else
    ifneq ($(wildcard system/core/toolbox/dynarray.c),)
        LOCAL_SRC_FILES += dynarray.c
    endif
endif

ifeq ($(PLATFORM_SDK_VERSION), 21)
    LOCAL_CFLAGS += \
        -std=gnu99 \
        -Werror -Wno-unused-parameter \
        -I$(LOCAL_PATH)/upstream-netbsd/include \
        -include bsd-compatibility.h
endif

ifeq ($(PLATFORM_SDK_VERSION), 21)
    LOCAL_C_INCLUDES += external/openssl/include
else
    LOCAL_C_INCLUDES += bionic/libc/bionic
endif

LOCAL_SHARED_LIBRARIES += libcutils

ifneq ($(PLATFORM_SDK_VERSION), 21)
LOCAL_SHARED_LIBRARIES += \
    liblog \
    libc
endif

ifeq ($(TWHAVE_SELINUX), true)
    LOCAL_SHARED_LIBRARIES += libselinux
    ifeq ($(PLATFORM_SDK_VERSION), 21)
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
endif

ifeq ($(PLATFORM_SDK_VERSION), 21)
    # libusbhost is only used by lsusb, and that isn't usually included in toolbox.
    # The linker strips out all the unused library code in the normal case.
    LOCAL_STATIC_LIBRARIES := libusbhost
    LOCAL_WHOLE_STATIC_LIBRARIES := $(patsubst %,libtoolbox_%,$(BSD_TOOLS))
endif

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin

# Including this will define $(intermediates) below
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

ifeq ($(PLATFORM_SDK_VERSION), 21)
    ALL_TOOLS := $(BSD_TOOLS) $(OUR_TOOLS) setenforce
else
    ALL_TOOLS := $(OUR_TOOLS)
endif

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(ALL_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

# Make /sbin/toolbox launchers for each tool
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

ifneq (,$(filter $(PLATFORM_SDK_VERSION),16 17 18))
    # Only needed if the build system lacks support for LOCAL_ADDITIONAL_DEPENDENCIES
    ALL_DEFAULT_INSTALLED_MODULES += $(SYMLINKS)
    ALL_MODULES.$(LOCAL_MODULE).INSTALLED := \
        $(ALL_MODULES.$(LOCAL_MODULE).INSTALLED) $(SYMLINKS)
endif

common_cflags :=
SYMLINKS :=
ALL_TOOLS :=
BSD_TOOLS :=
OUR_TOOLS :=
TOOLS_FOR_SELINUX :=
