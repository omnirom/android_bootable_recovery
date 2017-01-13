LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(TW_INCLUDE_INJECTTWRP), true)
	LOCAL_SRC_FILES:= \
		injecttwrp.c
	LOCAL_CFLAGS:= -g -c -W
	LOCAL_MODULE:=injecttwrp
	LOCAL_MODULE_TAGS:= eng
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
	include $(BUILD_EXECUTABLE)
endif

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
	listxattr.c
LOCAL_CFLAGS:= -g -c -W
LOCAL_MODULE:=listxattr
LOCAL_MODULE_TAGS:= eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
ifneq ($(TARGET_ARCH), arm64)
    ifneq ($(TARGET_ARCH), x86_64)
        LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker
    else
        LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
    endif
else
    LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
endif
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= \
	setcaps.c
LOCAL_CFLAGS:= -g -c -W
LOCAL_MODULE:=setcaps
LOCAL_MODULE_TAGS:= eng
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
ifneq ($(TARGET_ARCH), arm64)
    ifneq ($(TARGET_ARCH), x86_64)
        LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker
    else
        LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
    endif
else
    LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker64
endif
include $(BUILD_EXECUTABLE)
