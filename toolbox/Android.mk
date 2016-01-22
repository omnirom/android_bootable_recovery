TWRP_TOOLBOX_PATH := $(call my-dir)
LOCAL_PATH := system/core/toolbox
include $(CLEAR_VARS)

OUR_TOOLS := \
    start \
    stop \
    
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
    OUR_TOOLS += \
        getprop \
        setprop
endif

# If busybox does not have SELinux support, provide these tools with toolbox.
# Note that RECOVERY_BUSYBOX_TOOLS will be empty if TW_USE_TOOLBOX == true.
ifeq ($(TWHAVE_SELINUX), true)
    TOOLS_FOR_SELINUX := \
        ls

    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
        TOOLS_FOR_SELINUX += \
            load_policy \
            getenforce \
            chcon \
            restorecon \
            runcon \
            getsebool \
            setsebool
    endif

    OUR_TOOLS += $(filter-out $(RECOVERY_BUSYBOX_TOOLS), $(TOOLS_FOR_SELINUX))

    # toolbox setenforce is used during init, so it needs to be included here
    # symlink is omitted at the very end if busybox already provides this
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -lt 23; echo $$?),0)
       OUR_TOOLS += setenforce
    endif
endif

ifeq ($(TW_USE_TOOLBOX), true)
    ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
        # These are the only toolbox tools in M. The rest are now in toybox.
        BSD_TOOLS := \
            dd \
            du \

        OUR_TOOLS := \
            df \
            iftop \
            ioctl \
            ionice \
            log \
            ls \
            lsof \
            mount \
            nandread \
            newfs_msdos \
            ps \
            prlimit \
            renice \
            sendevent \
            start \
            stop \
            top \
            uptime \
            watchprops \

    else
        ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
            OUR_TOOLS += \
                mknod \
                nohup
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
            mkswap \
            mount \
            nandread \
            netstat \
            newfs_msdos \
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
    endif
endif

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
    ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
        OUR_TOOLS += r
    endif
endif

LOCAL_SRC_FILES := \
    toolbox.c \
    $(patsubst %,%.c,$(OUR_TOOLS))

ifneq ($(wildcard system/core/toolbox/dynarray.c),)
    LOCAL_SRC_FILES += dynarray.c
endif

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
    LOCAL_SRC_FILES += \
        pwcache.c \
        upstream-netbsd/lib/libc/gen/getbsize.c \
        upstream-netbsd/lib/libc/gen/humanize_number.c \
        upstream-netbsd/lib/libc/stdlib/strsuftoll.c \
        upstream-netbsd/lib/libc/string/swab.c \
        upstream-netbsd/lib/libutil/raise_default_signal.c
endif

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22 23))
    LOCAL_CFLAGS += \
        -std=gnu99 \
        -Werror -Wno-unused-parameter \
        -I$(LOCAL_PATH)/upstream-netbsd/include \
        -include bsd-compatibility.h
endif

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
    LOCAL_C_INCLUDES += external/openssl/include
else
    LOCAL_C_INCLUDES += bionic/libc/bionic
endif

LOCAL_SHARED_LIBRARIES += libcutils

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22))
    ifeq ($(TW_USE_TOOLBOX), true)
        LOCAL_SHARED_LIBRARIES += libcrypto
    endif
else
    LOCAL_SHARED_LIBRARIES += \
        libc \
        liblog
endif

ifeq ($(TWHAVE_SELINUX), true)
    LOCAL_SHARED_LIBRARIES += libselinux
endif

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22 23))
    # libusbhost is only used by lsusb, and that isn't usually included in toolbox.
    # The linker strips out all the unused library code in the normal case.
    LOCAL_STATIC_LIBRARIES := libusbhost
    LOCAL_WHOLE_STATIC_LIBRARIES := $(patsubst %,libtoolbox_%,$(BSD_TOOLS))
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -gt 22; echo $$?),0)
    # Rule to make getprop and setprop in M trees where toybox normally
    # provides these tools. Toybox does not allow for easy dynamic
    # configuration, so we would have to include the entire toybox binary
    # which takes up more space than is necessary so long as we are still
    # including busybox.
    LOCAL_SRC_FILES += \
        ../../../$(TWRP_TOOLBOX_PATH)/getprop.c \
        ../../../$(TWRP_TOOLBOX_PATH)/setprop.c
    OUR_TOOLS += getprop setprop
    ifneq ($(TW_USE_TOOLBOX), true)
        LOCAL_SRC_FILES += ls.c
    endif
endif

LOCAL_MODULE := toolbox_recovery
LOCAL_MODULE_STEM := toolbox
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_MODULE_TAGS := optional

# Including this will define $(intermediates) below
include $(BUILD_EXECUTABLE)

$(LOCAL_PATH)/toolbox.c: $(intermediates)/tools.h

ifneq (,$(filter $(PLATFORM_SDK_VERSION), 21 22 23))
    ALL_TOOLS := $(BSD_TOOLS) $(OUR_TOOLS)
else
    ALL_TOOLS := $(OUR_TOOLS)
endif

TOOLS_H := $(intermediates)/tools.h
$(TOOLS_H): PRIVATE_TOOLS := $(ALL_TOOLS)
$(TOOLS_H): PRIVATE_CUSTOM_TOOL = echo "/* file generated automatically */" > $@ ; for t in $(PRIVATE_TOOLS) ; do echo "TOOL($$t)" >> $@ ; done
$(TOOLS_H): $(LOCAL_PATH)/Android.mk
$(TOOLS_H):
	$(transform-generated-source)

ifeq ($(TWHAVE_SELINUX), true)
    # toolbox setenforce is used during init in non-symlink form, so it was
    # required to be included as part of the suite above. if busybox already
    # provides setenforce, we can omit the toolbox symlink
    TEMP_TOOLS := $(filter-out $(RECOVERY_BUSYBOX_TOOLS), $(ALL_TOOLS))
    ALL_TOOLS := $(TEMP_TOOLS)
endif

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

SYMLINKS :=
ALL_TOOLS :=
BSD_TOOLS :=
OUR_TOOLS :=
TEMP_TOOLS :=
TOOLS_FOR_SELINUX :=
