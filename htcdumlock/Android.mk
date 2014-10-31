LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(TW_INCLUDE_DUMLOCK), true)
	LOCAL_SRC_FILES:= \
		htcdumlock.c
	LOCAL_CFLAGS:= -g -c -W
	LOCAL_LDFLAGS += -Wl,-dynamic-linker,/sbin/linker
	LOCAL_MODULE:=htcdumlock
	LOCAL_MODULE_TAGS:= eng
	LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
	LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
	include $(BUILD_EXECUTABLE)
endif